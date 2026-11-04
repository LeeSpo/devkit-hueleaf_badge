"""Packaging must not attach current-source provenance to stale binaries."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest

TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("candidate_build", TOOLS / "build_candidate.py")
cb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cb)


def make_build(tmp_path):
    build = tmp_path / "build"
    build.mkdir()
    for name in cb.OUTPUTS:
        p = build / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"test-output")
    layout = {"0x2000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
              "0x180000": "otadata_ota0.bin", "0x190000": "hueleaf_gallery.bin"}
    (build / "flasher_args.json").write_text(json.dumps({"flash_files": layout}))
    config = build / "sdkconfig"
    config.write_text("test config\n")
    record = {"source": str(cb.SOURCE), "source_sha256": cb.fingerprint(),
              "config_file": str(config), "sdkconfig_sha256": cb.digest(config),
              "outputs": {name: cb.digest(build / name) for name in cb.OUTPUTS}}
    (build / "build_provenance.json").write_text(json.dumps(record))
    return build


def test_provenance_binds_outputs_and_config(tmp_path):
    build = make_build(tmp_path)
    cb.check(build)
    (build / "hueleaf_gallery.bin").write_bytes(b"changed")
    with pytest.raises(RuntimeError, match="output"):
        cb.check(build)


def test_source_change_requires_rebuild(tmp_path):
    build = make_build(tmp_path)
    p = build / "build_provenance.json"
    record = json.loads(p.read_text())
    record["source_sha256"] = "stale"
    p.write_text(json.dumps(record))
    with pytest.raises(RuntimeError, match="Source changed"):
        cb.check(build)


def test_config_change_requires_rebuild(tmp_path):
    build = make_build(tmp_path)
    (build / "sdkconfig").write_text("changed\n")
    with pytest.raises(RuntimeError, match="configuration"):
        cb.check(build)


def test_package_refuses_existing_output(tmp_path):
    build = make_build(tmp_path)
    output = tmp_path / "existing"
    output.mkdir()
    marker = output / "old.bin"
    marker.write_bytes(b"must not overwrite")
    run = subprocess.run(["bash", str(TOOLS / "package.sh"), str(build), str(output)],
                         env={**os.environ, "PYTHON": sys.executable}, capture_output=True, text=True)
    assert run.returncode == 2, run.stdout + run.stderr
    assert marker.read_bytes() == b"must not overwrite"
    assert not (output / "install").exists()
