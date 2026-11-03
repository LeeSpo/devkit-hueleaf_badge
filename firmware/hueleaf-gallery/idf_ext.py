"""Project idf.py extension: block whole-chip erase (it would delete the stock
firmware in `factory`, its settings and image cache). Override with
HUELEAF_ALLOW_ERASE=1 idf.py erase-flash  (then restore the stock app, see README)."""
import os
import sys


def action_extensions(base_actions, project_path):
    def guarded_erase(action, ctx, args):
        if os.environ.get("HUELEAF_ALLOW_ERASE") != "1":
            sys.stderr.write(
                "\n*** HueLeaf: erase-flash is BLOCKED - it would wipe the stock firmware at 0x10000.\n"
                "*** Use dist/install/flash_install.sh to (re)install, or set HUELEAF_ALLOW_ERASE=1\n"
                "*** and afterwards flash dist/hueleaf_full_0x0.bin or the official factory image.\n\n")
            raise SystemExit(2)
        original(action, ctx, args)

    acts = base_actions.get("actions", {})
    original = acts["erase-flash"]["callback"]
    out = {}
    for name in ("erase-flash", "erase_flash"):
        if name in acts:
            spec = dict(acts[name])
            spec["callback"] = guarded_erase
            out[name] = spec
    return {"actions": out}
