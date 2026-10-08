#!/usr/bin/env python3
"""
BLE gateway for the nRF5 + Memfault example (stands in for a phone app).

Drains Memfault data from the device over the Diagnostics Control Point (DCP) and forwards
it to Memfault:
  1. read the device serial from the Device Information Service
  2. write START_SYNC (0x02) to the DCP characteristic
  3. collect data-frame notifications (0x10); each payload is an opaque Memfault chunk
  4. POST each chunk to https://chunks.memfault.com/api/v0/chunks/<serial>

Usage:
  export MEMFAULT_PROJECT_KEY=...       # Memfault project settings -> Project Key
  ./gateway.py                          # one sync
  ./gateway.py --interval 120           # sync every 120 s (reconnects each time)
  ./gateway.py --dry-run --save out.bin # don't upload; keep the chunks
"""

import argparse
import asyncio
import os
import struct
import sys
import time
import urllib.error
import urllib.request

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "nRF5-MFLT-Demo"

# See app/src/diag_service.c
DCP_CHAR_UUID = "7a0c0002-e3c5-218a-4f9c-64a7521d8e3b"
DIS_SERIAL_UUID = "00002a25-0000-1000-8000-00805f9b34fb"
DIS_FW_REV_UUID = "00002a26-0000-1000-8000-00805f9b34fb"

OP_GET_STATUS, OP_START_SYNC, OP_DATA_FRAME = 0x01, 0x02, 0x10
STATUS_BUSY, STATUS_COMPLETE = 0x01, 0x02

CHUNKS_URL = "https://chunks.memfault.com/api/v0/chunks/{serial}"


def u24(b):
    return b[0] | (b[1] << 8) | (b[2] << 16)


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


class DcpSession:
    """Request/response over the DCP characteristic, plus collection of data frames."""

    def __init__(self, client):
        self.client = client
        self.responses = asyncio.Queue()
        self.chunks = []
        self.frames_reported = None
        self.sync_done = asyncio.Event()

    def on_notify(self, _char, data: bytearray):
        opcode = data[0]
        if opcode == OP_DATA_FRAME:
            self.chunks.append(bytes(data[4:]))
            return
        if opcode == OP_START_SYNC and len(data) >= 5 and data[1] == STATUS_COMPLETE:
            self.frames_reported = u24(data[2:5])
            self.sync_done.set()
            return
        self.responses.put_nowait(bytes(data))

    async def request(self, opcode, timeout=5.0):
        await self.client.write_gatt_char(DCP_CHAR_UUID, bytes([opcode]), response=True)
        deadline = time.monotonic() + timeout
        while True:  # skip unrelated notifications
            rsp = await asyncio.wait_for(self.responses.get(), max(0.0, deadline - time.monotonic()))
            if rsp[0] == opcode:
                return rsp

    async def sync(self, timeout=60.0):
        rsp = await self.request(OP_START_SYNC)
        if rsp[1] == STATUS_BUSY:
            raise RuntimeError("device reports a sync already in progress")
        await asyncio.wait_for(self.sync_done.wait(), timeout)
        if self.frames_reported != len(self.chunks):
            log(f"Warning: device reported {self.frames_reported} frames, received {len(self.chunks)}")
        return self.chunks


def post_chunks(serial, chunks, project_key):
    """POSTs each chunk (one retry); returns the number that failed."""
    url = CHUNKS_URL.format(serial=serial)
    failed = 0
    for i, chunk in enumerate(chunks):
        req = urllib.request.Request(
            url,
            data=chunk,
            method="POST",
            headers={
                "Memfault-Project-Key": project_key,
                "Content-Type": "application/octet-stream",
            },
        )
        for attempt in (1, 2):
            try:
                with urllib.request.urlopen(req, timeout=15) as resp:
                    if 200 <= resp.status < 300:
                        break
                    err = f"HTTP {resp.status}"
            except urllib.error.HTTPError as e:
                err = f"HTTP {e.code} {e.read()[:200]!r}"
            except OSError as e:
                err = str(e)
            if attempt == 2:
                failed += 1
                log(f"Chunk {i + 1}/{len(chunks)} not uploaded: {err}")
    return failed


async def sync_once(args):
    log(f"Scanning for {args.name} ...")
    device = await BleakScanner.find_device_by_name(args.name, timeout=15.0)
    if device is None:
        raise RuntimeError(f"{args.name} not found (is it advertising? LED2 on the DK)")

    serial = None
    session = None
    try:
        async with BleakClient(device, timeout=20.0) as client:
            log(f"Connected to {device.address}, MTU {client.mtu_size}")
            serial = (await client.read_gatt_char(DIS_SERIAL_UUID)).decode()
            fw_rev = (await client.read_gatt_char(DIS_FW_REV_UUID)).decode()
            log(f"Device serial {serial}, firmware {fw_rev}")

            session = DcpSession(client)
            # With DIAG_REQUIRE_BOND=1 the CCCD needs an encrypted link: the OS pairs here
            await client.start_notify(DCP_CHAR_UUID, session.on_notify)

            status = await session.request(OP_GET_STATUS)
            pending, used_pct = status[1], status[2]
            log(f"Memfault data pending: {'yes' if pending else 'no'}, event storage {used_pct}% used")
            if not pending:
                return

            t0 = time.monotonic()
            await session.sync()
            total = sum(len(c) for c in session.chunks)
            log(f"Received {len(session.chunks)} chunks / {total} bytes in {time.monotonic() - t0:.2f} s")
            await client.stop_notify(DCP_CHAR_UUID)
    finally:
        # Upload whatever arrived, even after a failure: the device has already released every
        # complete message it sent (only an interrupted one is re-sent next time). Done after
        # disconnecting, the way a phone app would queue and send in the background.
        chunks = session.chunks if session else []
        if chunks and serial:
            if args.save:
                with open(args.save, "ab") as f:
                    for c in chunks:
                        f.write(struct.pack("<H", len(c)) + c)
                log(f"Appended {len(chunks)} chunks to {args.save}")
            if args.dry_run:
                log("Dry run, not uploading")
            else:
                try:
                    failed = post_chunks(serial, chunks, args.project_key)
                    log(f"Uploaded {len(chunks) - failed}/{len(chunks)} chunks to Memfault for device {serial}")
                except Exception as e:  # don't mask an earlier BLE error
                    log(f"Upload failed: {e}")


async def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--name", default=DEVICE_NAME)
    parser.add_argument("--interval", type=int, default=0, help="repeat every N seconds")
    parser.add_argument("--project-key", default=os.environ.get("MEMFAULT_PROJECT_KEY"))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--save", help="append received chunks (u16 length-prefixed) to a file")
    args = parser.parse_args()

    if not args.dry_run and not args.project_key:
        sys.exit("Set MEMFAULT_PROJECT_KEY or pass --project-key (or use --dry-run)")

    while True:
        try:
            await sync_once(args)
        except Exception as e:  # keep looping through transient BLE errors
            log(f"Sync failed: {e}")
        if not args.interval:
            break
        await asyncio.sleep(args.interval)


if __name__ == "__main__":
    asyncio.run(main())
