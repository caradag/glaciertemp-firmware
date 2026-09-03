#!/usr/bin/env python3
"""Decode a GlacierTemp LOGH capture back into CSV.

LOGH dumps the raw log as Intel HEX without interpreting it, for the case where
the logger cannot read its own log -- a build with a different channel set, and
no way to re-flash in the field. This turns that capture back into data.

The capture is self-describing: the metadata block carries the signature of the
build that WROTE the log, and the signature encodes exactly which channels are
present and how many DS18B20 sensors, so the record layout is recovered from the
file itself. Nothing has to be remembered about the deployment.

    python3 decode_logh.py capture.txt > log.csv
    python3 decode_logh.py capture.txt --signature 0x141F   # override
    python3 decode_logh.py capture.txt --raw log.bin        # also write the binary

The Arduino IDE ignores this file; it lives here so it travels with the firmware.
"""
import argparse, sys
from datetime import datetime, timedelta

# Bit assignments from the channel block in GlacierTemp_1_cell_v02_claude.ino.
# name, bit, scale (divisor), decimals
CHANNELS = [
    ("Volt",     0x0001, 1000.0, 2),
    ("Temp",     0x0002,  100.0, 2),
    ("RH",       0x0004,   10.0, 1),
    ("HAtemp",   0x0008,  100.0, 2),
]
DS_BIT   = 0x0010
ANALOG   = [("A0", 0x0020), ("A1", 0x0040), ("A2", 0x0080), ("A3", 0x0100)]
DS_SHIFT = 9
DS_MASK  = 0x0E00
EPOCH    = datetime(2000, 1, 1)          # mktime2() counts seconds from here
INVALID  = -32768


def layout(sig):
    """Channel list and record size implied by a 16-bit log signature."""
    ver = (sig >> 12) & 0x0F
    if ver != 1:
        print(f"warning: signature 0x{sig:04X} has format version {ver}, expected 1",
              file=sys.stderr)
    fields = [(n, s, d) for n, b, s, d in CHANNELS if sig & b]
    if sig & DS_BIT:
        n_ds = ((sig & DS_MASK) >> DS_SHIFT) + 1
        fields += [(f"DS{i}", 100.0, 2) for i in range(n_ds)]
    fields += [(n, 1000.0, 3) for n, b in ANALOG if sig & b]
    return fields, 4 + 2 * len(fields)


def read_ihex(path):
    """Bytes from the Intel HEX span of a capture; non-record lines are ignored."""
    mem, base, sig, seen = {}, 0, None, False
    for line in open(path, "r", errors="replace"):
        line = line.strip()
        if not seen and "signature" in line.lower() and "0x" in line:
            # "log signature: 0x141F" -- the build that WROTE the log, listed first
            try:
                sig = int(line[line.lower().index("0x"):].split()[0], 16)
                seen = True
            except ValueError:
                pass
        if not line.startswith(":"):
            continue
        try:
            raw = bytes.fromhex(line[1:])
        except ValueError:
            print(f"skipping malformed line: {line[:24]}...", file=sys.stderr)
            continue
        if len(raw) < 5 or sum(raw) & 0xFF:
            print(f"skipping bad checksum: {line[:24]}...", file=sys.stderr)
            continue
        n, addr, typ = raw[0], (raw[1] << 8) | raw[2], raw[3]
        if typ == 0x04:
            base = int.from_bytes(raw[4:6], "big") << 16
        elif typ == 0x00:
            for k, b in enumerate(raw[4:4 + n]):
                mem[base + addr + k] = b
        elif typ == 0x01:
            break
    if not mem:
        return b"", sig
    top = max(mem)
    return bytes(mem.get(i, 0xFF) for i in range(top + 1)), sig


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--signature", help="override, e.g. 0x141F")
    ap.add_argument("--raw", help="also write the reconstructed binary here")
    a = ap.parse_args()

    data, sig = read_ihex(a.capture)
    if a.signature:
        sig = int(a.signature, 16)
    if sig is None:
        sys.exit("no signature found in the capture; pass --signature")
    if a.raw:
        open(a.raw, "wb").write(data)

    fields, rec = layout(sig)
    print(f"signature 0x{sig:04X}, record {rec} B, {len(data)} B captured, "
          f"{len(data)//rec} whole records", file=sys.stderr)
    print("Time," + ",".join(n for n, _, _ in fields))

    blank = 0
    for off in range(0, len(data) - rec + 1, rec):
        r = data[off:off + rec]
        t = int.from_bytes(r[0:4], "little")
        if t in (0, 0xFFFFFFFF):        # erased flash, or a slot never written
            blank += 1
            continue
        cols = []
        for i, (name, scale, dec) in enumerate(fields):
            v = int.from_bytes(r[4 + 2 * i:6 + 2 * i], "little", signed=True)
            # Humidity is never negative, so any negative value is a failed read,
            # which also catches the -1 sentinel the older firmware wrote.
            cols.append("NaN" if v == INVALID or (name == "RH" and v < 0)
                        else f"{v/scale:.{dec}f}")
        print(f"{EPOCH + timedelta(seconds=t):%Y-%m-%d %H:%M:%S}," + ",".join(cols))
    if blank:
        print(f"{blank} blank/erased records skipped", file=sys.stderr)


if __name__ == "__main__":
    main()
