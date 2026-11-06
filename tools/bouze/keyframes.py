#!/usr/bin/env python3
"""Animation keyframe streams of one character in a level file.

Usage: keyframes.py <disc.bin> <lba> <size> <actor_type_hex> [anim_id_hex ...]

Lists every animation of the actors whose header type (tag 0x16 word 0) is
<actor_type> (e.g. 05000190 = Taz, 01008381 = the TRCA01 enemy): frame
count, play mode, the command opcodes used, and the structural commands
(node push/pop, hide/show). With animation ids, also prints each frame's
commands.

Stream format, as read by the game's keyframe interpreter FUN_800563EC
(decompiled, see NOTES.md "Keyframe stream format"): per frame an 8-byte
header (u16[1] = command count), then commands. A command is a u16 —
bits 0-7 node index (1-based, into the actor's node list +0x9C), bits
8-11 opcode, bits 12-15 flags — followed by an opcode/flag dependent number
of u16 arguments (nargs below). The parse is checked by requiring it to
consume each blob exactly.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bouze      # noqa: E402
import records    # noqa: E402


def nargs(cmd):
    op, fl = cmd & 0xF00, cmd & 0xF000

    def a3(bit):
        return 3 if fl & bit else 0
    if op in (0x100, 0xD00):
        return a3(0x2000) + a3(0x4000) + (3 if fl & 0x8000 else (2 if fl & 0x1000 else 0))
    if op in (0x200, 0x300, 0xC00):
        return 1
    if op in (0x400, 0x700):
        return 4
    if op in (0x600, 0xE00):
        return 2
    if op == 0x900:
        return 4 if fl & 0x1000 else 7
    if op == 0xB00:
        return (a3(0x2000) + a3(0x4000) + a3(0x8000) + 1) >> 1
    return 0


def parse(blob):
    """Returns (frames, mode, per-frame command lists, bytes consumed)."""
    frames = struct.unpack_from("<H", blob, 4)[0]
    mode = blob[2] >> 1
    kf, p, out = blob[8:], 0, []
    for _ in range(frames):
        if p + 8 > len(kf):
            out.append(None)
            break
        count = struct.unpack_from("<4H", kf, p)[1]
        p += 8
        cmds = []
        for _ in range(count):
            cmd = struct.unpack_from("<H", kf, p)[0]
            n = nargs(cmd)
            cmds.append((cmd, struct.unpack_from(f"<{n}H", kf, p + 2) if n else ()))
            p += 2 + 2 * n
        out.append(cmds)
    return frames, mode, out, p + 8


def structural(cmd):
    op, fl, node = cmd & 0xF00, cmd & 0xF000, cmd & 0xFF
    if op not in (0x800, 0xC00):
        return None
    kind = (fl >> 13) & 7
    if fl & 0x1000:
        return f"{'hide' if kind == 5 else 'pop'}(n{node})"
    return f"{'show' if kind == 5 else 'push'}(n{node},k{kind})"


def anim_blobs(disc, lba, size, actor_type):
    data = bouze.read_file(disc, lba, size)
    h = bouze.header(data)
    offs = bouze.chunk_offsets(h)
    stream = bouze.decompress(data, offs[0], h["chunks"][0][1])
    i4 = [c[0] for c in h["chunks"]].index(4)
    blobs = bouze.decompress(data, offs[i4], h["chunks"][i4][1])
    recs, _, _ = records.parse(stream)
    res = {}
    for _, kind, tags in recs:
        if kind == 0x22:
            r = records.resource_info(kind, tags)
            if "id" in r:
                res[r["id"]] = r
    ids = []
    for _, kind, tags in recs:
        if kind in (7, 8, 9, 10, 0x63):
            a = records.actor_info(tags)
            if a.get("t08") == actor_type:
                ids += [i for i in a["res"] if i not in ids]
    out = []
    for i in ids:
        for b in res.get(i, {}).get("blobs", []):
            if b[0] == "anim":
                out.append((res[i]["sub"], i, blobs[b[1]:b[1] + b[2]]))
    return sorted(out)


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    disc, lba, size, atype = Path(argv[1]), int(argv[2]), int(argv[3]), int(argv[4], 16)
    detail = {int(x, 16) for x in argv[5:]}
    bad = 0
    for sub, rid, blob in anim_blobs(disc, lba, size, atype):
        frames, mode, fr, used = parse(blob)
        ok = used == len(blob) and None not in fr
        bad += not ok
        ops = sorted({c & 0xF00 for cmds in fr if cmds for c, _ in cmds})
        ev = [f"f{i}:{s}" for i, cmds in enumerate(fr) if cmds for c, _ in cmds if (s := structural(c))]
        print(f"{sub:04X} res {rid:4d} frames {frames:3d} mode {mode} size {len(blob):5d}"
              f"{'' if ok else ' PARSE-MISMATCH'} ops {' '.join(f'{o:03X}' for o in ops)}")
        if ev:
            print("     " + " ".join(ev[:30]) + (" …" if len(ev) > 30 else ""))
        if sub in detail:
            for i, cmds in enumerate(fr):
                print(f"     frame {i}: " + " ".join(
                    f"{c:04X}" + (f"[{','.join(f'{a:X}' for a in args)}]" if args else "") for c, args in cmds or []))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
