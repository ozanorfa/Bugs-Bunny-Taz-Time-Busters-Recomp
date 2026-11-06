#!/usr/bin/env python3
"""Bouze (.BZZ) pack reader — a Python port of the game's own routines.

Ported from the Ghidra decompile of `Bouze_Decompress` (0x8006B67C) and the
header handling in `Level_LoadParse` (0x80068174) of SLUS_011.44. See
NOTES.md ("Bouze files"). Cross-platform; standard library only.

This tool reads the player's own disc image. Its output is game data and
must only be written to gitignored locations (e.g. analysis/).

Usage:
  bouze.py info  <disc.bin> <lba> <size>          print header + chunk table
  bouze.py unpack <disc.bin> <lba> <size> <outdir> write decompressed chunks
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

SECTOR_RAW, USER_OFF, USER = 2352, 24, 2048


def read_file(disc: Path, lba: int, size: int) -> bytes:
    """Read a file's user data from a MODE2/2352 image."""
    out = bytearray()
    with open(disc, "rb") as f:
        for i in range((size + USER - 1) // USER):
            f.seek((lba + i) * SECTOR_RAW + USER_OFF)
            out += f.read(USER)
    return bytes(out[:size])


def header(data: bytes) -> dict:
    """Header sector as parsed by Level_LoadParse: +0 version, +4 offset of
    the data part, +8 chunk count, then (type, size, aligned) per chunk;
    +0x7FC holds a checksum of bytes 0..0x7FB (not verified here)."""
    version, data_off, count = struct.unpack_from("<III", data, 0)
    chunks = [struct.unpack_from("<III", data, 0x0C + 12 * i) for i in range(count)]
    return {"version": version, "data_off": data_off, "chunks": chunks,
            "checksum": struct.unpack_from("<I", data, 0x7FC)[0]}


def decompress(src: bytes, pos: int = 0, size: int | None = None) -> bytes:
    """LZ decoder equivalent to Bouze_Decompress.

    Stream: byte0 = parameters, bytes1..3 = token count (24-bit, order
    b3 | b1<<16 | b2<<8), then flag bytes (LSB first; 1 = literal,
    0 = 2-byte match). The game's loop decodes one token more than the
    count, which reads the padding after the compressed data and writes a
    few junk bytes past the useful end; `size` (the chunk's compressed size
    from the header) stops decoding at the real end of the input."""
    end = pos + size if size is not None else len(src)
    b0 = src[pos]
    k = b0 & 7                       # distance/length split
    max_code = (0x7F >> k) & 0xFFFF
    thresh = 0x13
    if max_code < 0x1F:
        thresh = max_code >> 1
    shift = (b0 >> 3) & 3
    table = []
    for c in range(max_code + 1):
        v = c if c <= thresh else thresh + ((c - thresh) << shift)
        table.append((v + 2) & 0xFFFF)
    remaining = src[pos + 3] | (src[pos + 1] << 16) | (src[pos + 2] << 8)
    p = pos + 4
    out = bytearray()
    len_mask = 0x7F >> k
    dist_shift = 7 - k
    while True:
        if p >= end:
            return bytes(out)
        flags = src[p] | 0x100
        p += 1
        while True:
            bit = flags & 1
            flags >>= 1
            if flags == 0:
                break
            if p >= end:                     # the extra padding token
                return bytes(out)
            if bit:
                out.append(src[p])
                p += 1
            else:
                v = (src[p] << 8) | src[p + 1]
                p += 2
                dist = v >> dist_shift
                n = table[v & len_mask] + 1
                start = len(out) - dist
                for i in range(n):           # byte-wise: overlaps allowed
                    out.append(out[start + i])
            if remaining == 0:
                return bytes(out)
            remaining -= 1


def chunk_offsets(h: dict) -> list[int]:
    """File offset of each chunk: the header sector, then each chunk at its
    sector-aligned size."""
    offs, at = [], USER
    for _typ, _size, aligned in h["chunks"]:
        offs.append(at)
        at += aligned
    return offs


def main(argv: list[str]) -> None:
    if len(argv) < 5:
        raise SystemExit(__doc__)
    cmd, disc, lba, size = argv[1], Path(argv[2]), int(argv[3], 0), int(argv[4], 0)
    data = read_file(disc, lba, size)
    h = header(data)
    offs = chunk_offsets(h)
    if cmd == "info":
        print(f"version={h['version']} data_off=0x{h['data_off']:X} chunks={len(h['chunks'])}")
        for i, ((t, s, a), o) in enumerate(zip(h["chunks"], offs)):
            print(f"  chunk {i}: type={t} size=0x{s:X} aligned=0x{a:X} file_off=0x{o:X}")
    elif cmd == "unpack":
        out = Path(argv[5])
        out.mkdir(parents=True, exist_ok=True)
        for i in (0, 1):                     # the two compressed chunks
            raw = decompress(data, offs[i], h["chunks"][i][1])
            (out / f"chunk{i}.bin").write_bytes(raw)
            print(f"chunk {i}: {len(raw)} bytes -> {out / f'chunk{i}.bin'}")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
