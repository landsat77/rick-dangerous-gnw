#!/usr/bin/env python3
"""Make the Rick Dangerous pack (RickDangerous.xrk) from the xrick data.

The data: the "assets" folder of xrick (Pierluigi Vicinanza's xrick,
github.com/foolsh/xrick, data version of 2014): archives/ (filelist.dat,
graphics/, misc/, texts/) and sounds/ (8-bit mono 22050 Hz WAV).

Pack format (little endian, read in place from flash by src/port/xr_sys.c):
  "XRK1", u32 file count, u32 total size
  count x { u32 name offset, u32 data offset, u32 size }   (sorted by name)
  names (NUL-terminated, e.g. "graphics/palette.dat", "sounds/jump.wav")
  data, each file 4-byte aligned
"""
import argparse
import os
import struct
import sys
import wave


def collect(assets):
    files = {}
    archives = os.path.join(assets, "archives")
    sounds = os.path.join(assets, "sounds")
    if not os.path.isfile(os.path.join(archives, "filelist.dat")):
        sys.exit(f"{archives}/filelist.dat not found: give the xrick 'assets' folder")
    for root, _, names in os.walk(archives):
        for n in names:
            path = os.path.join(root, n)
            rel = os.path.relpath(path, archives).replace(os.sep, "/")
            files[rel] = open(path, "rb").read()
    for n in sorted(os.listdir(sounds)):
        if not n.lower().endswith(".wav"):
            continue
        path = os.path.join(sounds, n)
        with wave.open(path) as w:
            if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 1, 22050):
                sys.exit(f"{n}: expected 8-bit mono 22050 Hz")
        files["sounds/" + n] = open(path, "rb").read()
    return files


def build(files):
    names = sorted(files)
    header_size = 12 + 12 * len(names)
    name_blob = b"".join(n.encode() + b"\0" for n in names)
    offset = (header_size + len(name_blob) + 3) & ~3
    table, data, name_off = [], bytearray(), header_size
    for n in names:
        content = files[n]
        table.append(struct.pack("<III", name_off, offset + len(data), len(content)))
        name_off += len(n) + 1
        data += content
        data += b"\0" * (-len(data) & 3)
    blob = bytearray(struct.pack("<4sII", b"XRK1", len(names), 0))
    blob += b"".join(table) + name_blob
    blob += b"\0" * (offset - len(blob))
    blob += data
    struct.pack_into("<I", blob, 8, len(blob))
    return bytes(blob)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("assets", help="xrick assets folder (archives/ and sounds/)")
    ap.add_argument("out", help="output pack, e.g. RickDangerous.xrk")
    args = ap.parse_args()
    files = collect(args.assets)
    blob = build(files)
    with open(args.out, "wb") as f:
        f.write(blob)
    print(f"{args.out}: {len(files)} files, {len(blob)} bytes")


if __name__ == "__main__":
    main()
