"""Read-only ISO 9660 access to a disc image: raw 2352-byte sectors
(MODE1, or MODE2 form 1 as on PlayStation discs) or 2048-byte .iso.
Standard library only."""
from __future__ import annotations

import struct
from pathlib import Path

SYNC = bytes([0x00] + [0xFF] * 10 + [0x00])


class Iso:
    def __init__(self, path: Path):
        self.path = Path(path)
        self._f = open(self.path, "rb")
        self._f.seek(16 * 2352)
        head = self._f.read(16)
        if head[:12] == SYNC:
            self.sector_size, self.user_offset = 2352, (24 if head[15] == 2 else 16)
        else:
            self.sector_size, self.user_offset = 2048, 0
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001" or pvd[0] != 1:
            raise ValueError(f"{self.path}: no ISO 9660 primary volume descriptor")
        self.volume_id = pvd[40:72].decode("ascii", "replace").strip()
        root = pvd[156:156 + 34]
        self.files: dict[str, tuple[int, int]] = {}
        self.dirs: dict[str, tuple[int, int]] = {}
        self._walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")

    def close(self) -> None:
        self._f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def sector(self, lba: int) -> bytes:
        self._f.seek(lba * self.sector_size + self.user_offset)
        data = self._f.read(2048)
        if len(data) != 2048:
            raise ValueError(f"sector {lba} is past the end of the image")
        return data

    def read(self, lba: int, size: int) -> bytes:
        return b"".join(self.sector(lba + i) for i in range((size + 2047) // 2048))[:size]

    def read_file(self, name: str) -> bytes:
        lba, size = self.files[name.upper()]
        return self.read(lba, size)

    def _walk(self, lba: int, size: int, prefix: str, depth: int = 0) -> None:
        if depth > 8:
            return
        data = self.read(lba, size)
        i = 0
        while i < len(data):
            n = data[i]
            if n == 0:                       # records never cross a sector
                i = (i // 2048 + 1) * 2048
                continue
            rec = data[i:i + n]
            i += n
            if len(rec) < 34:
                break
            ext, ln = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
            flags, nl = rec[25], rec[32]
            raw = rec[33:33 + nl]
            if raw in (b"\x00", b"\x01"):
                continue
            name = raw.decode("ascii", "replace").split(";")[0].upper()
            full = f"{prefix}{name}"
            if flags & 2:
                self.dirs[full] = (ext, ln)
                self._walk(ext, ln, full + "/", depth + 1)
            else:
                self.files[full] = (ext, ln)
