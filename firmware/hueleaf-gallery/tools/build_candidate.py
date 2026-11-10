#!/usr/bin/env python3
"""Build an isolated candidate and bind binaries to the exact source tree.

source local/bin/dev-env.sh
python tools/build_candidate.py --build-dir /absolute/build/path
python tools/build_candidate.py --check /absolute/build/path
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
SOURCE = PROJECT.parents[1]
OUTPUTS = ("hueleaf_gallery.bin", "bootloader/bootloader.bin",
           "partition_table/partition-table.bin", "otadata_ota0.bin", "flasher_args.json")


def git(*args):
    return subprocess.check_output(["git", "-C", str(SOURCE), *args])


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# Only what can change the firmware binaries or the shipped installer is bound
# to a build: this project minus docs/tests, plus the stock image that
# non-remote packaging merges. Android app, PC tools and other docs must not
# invalidate a build.
UNBOUND = ("docs/", "test_host/", "tools/tests/", "README.md")
EXTRA_INPUTS = ("examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin",)


def bound(name):
    if name in EXTRA_INPUTS:
        return True
    prefix = PROJECT.relative_to(SOURCE).as_posix() + "/"
    return name.startswith(prefix) and not name[len(prefix):].startswith(UNBOUND)


def fingerprint():
    names = git("ls-files", "-z", "--cached", "--others", "--exclude-standard")
    h = hashlib.sha256()
    for name in sorted(set(names.split(b"\0")) - {b""}):
        if not bound(os.fsdecode(name)):
            continue
        p = SOURCE / os.fsdecode(name)
        h.update(name + b"\0")
        if p.is_symlink(): h.update(b"link\0" + os.fsencode(os.readlink(p)))
        elif p.is_file(): h.update(b"file\0" + p.read_bytes())
        else: h.update(b"missing\0")
    return h.hexdigest()


def check(build):
    record = json.loads((build / "build_provenance.json").read_text())
    if record["source"] != str(SOURCE) or record["source_sha256"] != fingerprint():
        raise RuntimeError("Source changed or build belongs to another checkout; rebuild before packaging")
    config = Path(record["config_file"])
    if digest(config) != record["sdkconfig_sha256"]:
        raise RuntimeError("Build configuration changed")
    for name, sha in record["outputs"].items():
        if digest(build / name) != sha:
            raise RuntimeError("Missing or changed build output: " + name)
    expected = {"0x2000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
                "0x180000": "otadata_ota0.bin", "0x190000": "hueleaf_gallery.bin"}
    args = json.loads((build / "flasher_args.json").read_text())
    if record.get("layout_id") == "remote-v1":
        from remote_layout import FLASH_FILES
        expected = FLASH_FILES
        app = (build / "hueleaf_gallery.bin").read_bytes()
        if app[80:112].split(b"\0", 1)[0] != b"hueleaf_remote_v1":
            raise RuntimeError("App layout identifier mismatch")
    if args["flash_files"] != expected:
        raise RuntimeError("Unexpected write layout; factory preservation cannot be confirmed")
    if (build / "hueleaf_gallery.bin").stat().st_size > (0x1F0000 if record.get("layout_id") == "remote-v1" else 0xB0000):
        raise RuntimeError("App exceeds OTA slot")
    return record


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir", type=Path, default=PROJECT / "build")
    p.add_argument("--check", type=Path)
    p.add_argument("--remote", action="store_true", help="Build remote-v1, replaces stock on explicit migration")
    a = p.parse_args()
    if a.check:
        check(a.check.resolve())
        return 0
    build = a.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    (build / "build_provenance.json").unlink(missing_ok=True)
    before = fingerprint()
    config = build / "sdkconfig"
    seed = build / "source-sdkconfig.sha256"
    source_hash = digest(PROJECT / "sdkconfig") + (digest(PROJECT / "sdkconfig.remote.defaults") if a.remote else "")
    if not config.exists() or not seed.exists() or seed.read_text() != source_hash:
        shutil.copy2(PROJECT / "sdkconfig", config)
        if a.remote:
            overrides = (PROJECT / "sdkconfig.remote.defaults").read_text().splitlines()
            keys = {line.split("=", 1)[0] for line in overrides if line.startswith("CONFIG_")}
            base = [line for line in config.read_text().splitlines()
                    if line.split("=", 1)[0] not in keys and not any(line == "# " + key + " is not set" for key in keys)]
            config.write_text("\n".join(base + overrides) + "\n")
        seed.write_text(source_hash)
    idf = Path(os.environ["IDF_PATH"]) / "tools/idf.py"
    subprocess.run([sys.executable, str(idf), "-C", str(PROJECT), "-B", str(build),
                    "-D", "SDKCONFIG=" + str(config), "-D", "HL_REMOTE_BUILD=" + ("ON" if a.remote else "OFF"), "build"], check=True)
    if a.remote:
        for extension in ("bin", "elf", "map"):
            shutil.copy2(build / ("hueleaf_remote_v1." + extension), build / ("hueleaf_gallery." + extension))
        # Keep installation contract stable while ESP descriptor project_name identifies layout.
        flash = json.loads((build / "flasher_args.json").read_text())
        for address, filename in flash["flash_files"].items():
            if filename == "hueleaf_remote_v1.bin": flash["flash_files"][address] = "hueleaf_gallery.bin"
        if flash.get("app", {}).get("file") == "hueleaf_remote_v1.bin": flash["app"]["file"] = "hueleaf_gallery.bin"
        (build / "flasher_args.json").write_text(json.dumps(flash, indent=2) + "\n")
    if fingerprint() != before:
        raise RuntimeError("Source changed during build; no provenance generated")
    desc = json.loads((build / "project_description.json").read_text())
    if Path(desc["project_path"]).resolve() != PROJECT:
        raise RuntimeError("Build describes another project")
    record = {"layout_id": "remote-v1" if a.remote else "gallery-stock-v1", "source": str(SOURCE), "branch": git("branch", "--show-current").decode().strip(),
              "base_commit": git("rev-parse", "HEAD").decode().strip(),
              "dirty": bool(git("status", "--porcelain")), "source_sha256": before,
              "firmware_version": (PROJECT / "main/version.h").read_text().split('"')[1],
              "built_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "idf_revision": desc["git_revision"], "target": desc["target"],
              "config_file": desc["config_file"], "sdkconfig_sha256": digest(Path(desc["config_file"])),
              "outputs": {name: digest(build / name) for name in OUTPUTS}}
    (build / "build_provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    check(build)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, KeyError, subprocess.CalledProcessError) as e:
        print("Candidate build rejected:", e, file=sys.stderr)
        sys.exit(1)
