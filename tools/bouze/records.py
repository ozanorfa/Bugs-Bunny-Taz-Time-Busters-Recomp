#!/usr/bin/env python3
"""Parse a decompressed Bouze record stream (chunk 0) the way
`Level_LoadParse` (0x80068174) does. Tag payload sizes come from the Ghidra
decompile of SLUS_011.44 (see NOTES.md "Character spawn" / "Resources").

A stream is a sequence of records ended by 0x2E. A known kind is followed
by tags up to a 0x2D terminator; an unknown kind byte is skipped on its own
(the loader's switch default). Unknown tags carry no payload in the game's
loops. Both are counted so a grammar error shows up.

Usage: records.py <chunk0.bin> [--actors] [--resources]
"""
from __future__ import annotations

import struct
import sys
from collections import Counter

SUB = {0x4D: 2, 0x4E: 4, 0x4F: 8, 0x50: 8, 0x51: 8, 0x52: 8, 0x53: 6, 0x54: 12,
       0x55: 12, 0x56: 16, 0x57: 20, 0x58: 8, 0x59: 4, 0x5A: 8, 0x5B: 8,
       0x5C: 2, 0x5D: 8, 0x5E: 12, 0x5F: 16, 0x60: 8}
ACTOR = {0x0B: 4, 0x0C: 4, 0x0F: 2, 0x10: 12, 0x11: 6, 0x12: 12, 0x13: 1,
         0x15: 1, 0x16: 28, 0x19: 4, 0x1A: 4, 0x1B: 6, 0x1C: 8, 0x1D: 4,
         0x1E: 2, 0x1F: 1, 0x27: 4, 0x2F: 48, 0x31: 12, 0x37: 10, 0x38: 2,
         0x39: 12, 0x40: 20, 0x41: 6, 0x45: 2, 0x61: 14, 0x65: 2, **SUB}
KINDS = {
    0x00: {0x04: 12, 0x01: 8, 0x02: 8, 0x4B: 56, 0x43: 12, 0x4C: 56},
    0x05: {0x06: 3},
    0x07: ACTOR, 0x08: ACTOR, 0x09: ACTOR, 0x0A: ACTOR, 0x63: ACTOR,
    0x20: {0x21: 4, 0x11: 6, 0x10: 12, 0x12: 12, 0x38: 2, 0x24: 8, 0x42: 1},
    0x22: {0x0D: 12, 0x24: 8, 0x25: 8, 0x26: 4, 0x27: 4, 0x3E: 12, 0x3F: 8, **SUB},
    0x28: {0x29: 8}, 0x35: {0x36: 8},
    0x3A: {0x3D: 36, 0x2A: 8, 0x3C: 14, 0x66: 3},
    0x46: {0x47: 8}, 0x48: {0x49: 56, 0x68: 56},
}


def parse(s: bytes):
    """Yield (offset, kind, [(tag, payload), ...]). Returns stats."""
    p, recs, unknown = 0, [], Counter()
    while p < len(s):
        b = s[p]
        if b == 0x2E:
            return recs, unknown, p
        if b == 0x2D:
            p += 1
            continue
        kind, start, p = b, p, p + 1
        table = KINDS.get(kind)
        tags = []
        if table is None:
            # The loader's switch default consumes just the kind byte and
            # reads the next byte as a new kind (no terminator expected).
            unknown[("kind", kind)] += 1
            continue
        while p < len(s) and s[p] != 0x2D:
            t = s[p]
            n = table.get(t)
            if n is None:
                unknown[(kind, t)] += 1
                n = 0
            tags.append((t, s[p + 1:p + 1 + n]))
            p += 1 + n
        p += 1                                  # the record's 0x2D
        recs.append((start, kind, tags))
    return recs, unknown, None


def actor_info(tags):
    info = {"res": []}
    for t, pl in tags:
        if t == 0x16:
            w = struct.unpack("<7I", pl)
            info.update(t08=w[0], f0C=w[1], f10=w[2], scr=w[3])
        elif t == 0x0F:
            info["res"].append(struct.unpack("<H", pl)[0])
        elif t == 0x10:
            info["pos"] = struct.unpack("<3i", pl)
    return info


def resource_info(kind, tags):
    """Kind 0x22: tag 0x27 = (u16 sub, u16 id) where sub is the entry's
    +0x0A (0x000D/0x000C for models, the animation id for animations);
    tag 0x24 = model blob, tag 0x25 = animation blob, each (offset, size)
    into the data part (chunk 1). Kind 0x20 uses tag 0x21 for the id."""
    info = {"kind": kind}
    for t, pl in tags:
        if t == 0x21:
            info["id"] = struct.unpack("<I", pl)[0] & 0xFFFF
        elif t == 0x27 and kind == 0x22:
            info["sub"], info["id"] = struct.unpack("<HH", pl)
        elif t in (0x24, 0x25):
            off, size = struct.unpack("<II", pl)
            info.setdefault("blobs", []).append(("model" if t == 0x24 else "anim", off, size))
    return info


def main(argv):
    s = open(argv[1], "rb").read()
    recs, unknown, end = parse(s)
    kinds = Counter(k for _, k, _ in recs)
    print(f"records={len(recs)} end={'0x%X' % end if end is not None else 'NOT FOUND'} "
          f"(stream {len(s)} bytes) kinds={dict(sorted(kinds.items()))}")
    if unknown:
        print("unknown:", dict(unknown.most_common(20)))
    if "--actors" in argv:
        for off, k, tags in recs:
            if k in (7, 8, 9, 10, 0x63):
                a = actor_info(tags)
                print(f"  actor@0x{off:05X} kind={k:#04x} t08={a.get('t08', 0):08X} "
                      f"f0C={a.get('f0C', 0):08X} res={a['res']} pos={a.get('pos')}")
    if "--resources" in argv:
        for off, k, tags in recs:
            if k in (0x20, 0x22):
                r = resource_info(k, tags)
                print(f"  res@0x{off:05X} kind={k:#04x} id={r.get('id')} blobs={r.get('blobs')}")


if __name__ == "__main__":
    main(sys.argv)
