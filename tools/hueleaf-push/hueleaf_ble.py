#!/usr/bin/env python3
"""Desktop diagnostic client (pip install bleak); not a phone App."""
import argparse
import asyncio
import hashlib
import hmac
import json
from pathlib import Path
import struct
import time
from hlble import SERVICE, RX, TX, CHALLENGE, Reassembler, fragments, message, inventory


async def run(args):
    from bleak import BleakClient, BleakScanner
    device = await BleakScanner.find_device_by_filter(
        lambda d, a: (args.address and d.address == args.address) or
        (not args.address and SERVICE in a.service_uuids), timeout=20)
    if device is None:
        raise RuntimeError("Device not found: double-tap the pendant and retry")
    key = bytes.fromhex(json.loads(args.key.read_text())["key_hex"])
    if len(key) != 32:
        raise ValueError("Key must be 32 bytes")
    queue, assembler = asyncio.Queue(), Reassembler()
    def on_reply(_, packet):
        try:
            reply = assembler.feed(bytes(packet))
            if reply:
                queue.put_nowait(reply)
        except ValueError as error:
            queue.put_nowait(error)
    async with BleakClient(device, pair=True, timeout=30) as client:
        await client.start_notify(TX, on_reply)
        request = 0
        async def call(op, payload=b""):
            nonlocal request
            request = request % 65535 + 1
            # MTU 23 is valid on every platform; never assume OS MTU negotiation.
            for fragment in fragments(message(op, request, payload), request):
                await client.write_gatt_char(RX, fragment, response=True)
            while True:
                reply = await asyncio.wait_for(queue.get(), 20)
                if isinstance(reply, Exception):
                    raise reply
                operation, number, status, data = reply
                if number == request and operation == op | 0x80:
                    if status:
                        raise RuntimeError(f"BLE op={op} status={status}")
                    return data
                print("event", reply)
        challenge = await client.read_gatt_char(CHALLENGE)
        if len(challenge) != 33 or challenge[0] != 1:
            raise RuntimeError("Provision via USB first")
        await call(1, hmac.new(key, challenge[1:], hashlib.sha256).digest())
        deadline = time.monotonic() + 30
        while True:
            status = json.loads(await call(2))
            if status["ready"] or time.monotonic() >= deadline:
                break
            await asyncio.sleep(0.5)
        print(json.dumps(status, ensure_ascii=False))
        if args.command == "list":
            token, start = 0, 0
            while True:
                token, total, start, flags, names = inventory(await call(3, struct.pack("<HQ", start, token)))
                for name, selected in names:
                    print(name)
                start += len(names)
                if start >= total:
                    break
                if not names:
                    raise RuntimeError("Inventory made no progress")
        elif args.command in ("next", "prev", "show"):
            if args.command == "show":
                if not args.name:
                    raise ValueError("show requires --name")
                await call(5, args.name.encode())
            else:
                await call(4, bytes((1 if args.command == "next" else 255,)))
            while True:
                event = await asyncio.wait_for(queue.get(), 120)
                if isinstance(event, Exception):
                    raise event
                if event[0] == 0xC0:
                    if event[2]:
                        raise RuntimeError("Physical refresh failed")
                    print("Refresh completed")
                    break
        await call(6)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--key", type=Path, required=True)
    p.add_argument("--address", help="Select device address/OS UUID when multiple devices present")
    p.add_argument("command", choices=("status", "list", "next", "prev", "show"))
    p.add_argument("--name")
    asyncio.run(run(p.parse_args()))


if __name__ == "__main__":
    main()
