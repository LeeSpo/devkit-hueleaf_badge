"""Project idf.py extension.

* Blocks whole-chip erase (it would delete the stock firmware in `factory`, its
  settings and image cache). Override: HUELEAF_ALLOW_ERASE=1 idf.py erase-flash
* `idf.py flash` / `app-flash` / `partition-table-flash` first run
  tools/hl_preflight.py (full flash backup to backups/, partition table check,
  stock-app size <= 0x170000) and abort before writing if it fails.
  Override (not recommended): HUELEAF_SKIP_PREFLIGHT=1
"""
import os
import subprocess
import sys


def action_extensions(base_actions, project_path):
    acts = base_actions.get("actions", {})
    out = {}

    def guarded_erase(action, ctx, args, **kw):
        if os.environ.get("HUELEAF_ALLOW_ERASE") != "1":
            sys.stderr.write(
                "\n*** HueLeaf: erase-flash is BLOCKED - it would wipe the stock firmware at 0x10000.\n"
                "*** Use dist/install/flash_install.sh to (re)install, or set HUELEAF_ALLOW_ERASE=1\n"
                "*** and afterwards flash dist/hueleaf_full_0x0.bin or the official factory image.\n\n")
            raise SystemExit(2)
        erase_original(action, ctx, args, **kw)

    def make_preflight(original):
        def run(action, ctx, args, **kw):
            if os.environ.get("HUELEAF_SKIP_PREFLIGHT") != "1":
                ptable = os.path.join(args.build_dir, "partition_table", "partition-table.bin")
                if not os.path.exists(ptable):
                    sys.stderr.write("\n*** HueLeaf: build first (idf.py build), partition table missing.\n")
                    raise SystemExit(2)
                cmd = [sys.executable, os.path.join(project_path, "tools", "hl_preflight.py"), "check",
                       "--ptable", ptable, "--backup-dir", os.path.join(project_path, "backups")]
                if getattr(args, "port", None):
                    cmd += ["--port", args.port]
                if subprocess.run(cmd).returncode != 0:
                    sys.stderr.write("\n*** HueLeaf: preflight failed, nothing was written.\n\n")
                    raise SystemExit(3)
            original(action, ctx, args, **kw)
        return run

    if "erase-flash" in acts:
        erase_original = acts["erase-flash"]["callback"]
        for name in ("erase-flash", "erase_flash"):
            if name in acts:
                spec = dict(acts[name]); spec["callback"] = guarded_erase; out[name] = spec
    for name in ("flash", "app-flash", "app_flash", "partition-table-flash", "partition_table-flash"):
        if name in acts:
            spec = dict(acts[name]); spec["callback"] = make_preflight(acts[name]["callback"]); out[name] = spec
    return {"actions": out}
