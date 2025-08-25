#!/usr/bin/env python3
"""Binary trace parser.

Reads trace.bin and prints per-frame headers and events as text.

Format (see README_ltrace.md):
- Frame header (16 bytes): b'FRME' + timestamp_us (=Q) + frame_id (=I)
- Entries (2 bytes each): [ id(15b) | event(1b) ], little-endian

Usage:
  python3 scripts/parse_trace_bin.py | tee trace.bin-decoded.txt

Output:
- FRAME id=<n> ts=<ISO8601>
- [index] event=call|return trace_id=<id>
"""
import struct
from datetime import datetime, timezone

ENTRY_SIZE = 2
MAGIC = b'FRME'

EVENT_NAMES = {
    0: "call",
    1: "return",
}

TRACE_EVENT_BITS = 1
TRACE_ID_BITS = 15
ID_MASK = (1 << TRACE_ID_BITS) - 1


def parse_entries(buf: bytes):
    """Decode a buffer of frames and 2-byte event entries and print text."""
    i = 0
    index = 0
    n = len(buf)
    while i < n:
        # Frame header: MAGIC + timestamp_us + frame_id
        if i + 4 <= n and buf[i:i+4] == MAGIC:
            if i + 16 > n:
                raise SystemExit("Corrupt trace.bin: incomplete frame header")
            ts_us = struct.unpack_from("=Q", buf, i + 4)[0]
            frame_id = struct.unpack_from("=I", buf, i + 12)[0]
            iso = datetime.fromtimestamp(ts_us / 1_000_000, tz=timezone.utc).isoformat(timespec='microseconds').replace('+00:00', 'Z')
            print(f"FRAME_id={frame_id} ts={iso}")
            i += 16
            continue

        # 2-byte event entry
        if i + ENTRY_SIZE > n:
            raise SystemExit("Corrupt trace.bin: incomplete entry")
        b0, b1 = buf[i], buf[i + 1]
        v = b0 | (b1 << 8)
        event_bit = v & 0x1
        trace_id = (v >> TRACE_EVENT_BITS) & ID_MASK
        event_str = EVENT_NAMES.get(event_bit, str(event_bit))
        print(f"[{index:010d}] event={event_str} trace_id={trace_id}")
        index += 1
        i += ENTRY_SIZE


def main():
    """Read trace.bin and print decoded text events."""
    with open("trace.bin", "rb") as f:
        data = f.read()
    parse_entries(data)


if __name__ == "__main__":
    main()
