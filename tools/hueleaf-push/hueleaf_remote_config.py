#!/usr/bin/env python3
"""USB remote-v1 provisioning. No phone App; keep key file private. Reboot applies changes."""
import argparse
import json
import os
from pathlib import Path
import secrets
import struct
from hueleaf_push import Device


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("port")
    group = p.add_mutually_exclusive_group()
    group.add_argument("--provision", type=Path, metavar="NEW_KEY_FILE")
    group.add_argument("--revoke", action="store_true")
    group.add_argument("--enable", choices=("yes", "no"))
    group.add_argument("--options", nargs=5, type=int, metavar=("WAIT_MS", "IDLE_MS", "RECONNECT_MS", "COOLDOWN_MS", "AFTER"))
    p.add_argument("--reboot", action="store_true")
    a = p.parse_args()
    payload = b"\0"
    if a.provision:
        key = secrets.token_bytes(32)
        fd = os.open(a.provision, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(json.dumps({"version": 1, "key_hex": key.hex()}) + "\n")
        payload = b"\1" + key
    elif a.revoke:
        payload = b"\2"
    elif a.enable:
        payload = bytes((4, a.enable == "yes"))
    elif a.options:
        wait, idle, reconnect, cooldown, after = a.options
        if not (5000 <= wait <= 120000 and 10000 <= idle <= 600000 and 0 <= reconnect <= 60000 and 0 <= cooldown <= 3600000 and 1 <= after <= 20):
            p.error("Options outside documented ranges")
        payload = b"\3" + struct.pack("<IIIIB", *a.options)
    d = Device(a.port)
    try:
        print(d.ok("REMOTE", payload).decode())
        if payload != b"\0":
            print("Saved; current connection/config unchanged until reboot. Protect the key file.")
        if a.reboot:
            d.ok("REBOOT")
    finally:
        d.close()


if __name__ == "__main__":
    main()
