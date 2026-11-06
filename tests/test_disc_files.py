"""Disc tier: the player's own disc image, read offline.

Checks that it is the dump the project was mapped on, that its file system
has the expected layout, and that every Bouze (.BZZ) file decodes to the
same bytes with the C port the plugin uses (src/mods/bouze.c, through
bbt_disc_probe) and with the Python tools (tools/bouze/), with every record
stream parsing exactly to its end marker. Skipped when the disc is not
found (game.toml [game] disc, or BBT_DISC). Nothing read from the disc is
written anywhere; only sizes and hashes are compared."""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.iso9660 import Iso  # noqa: E402

sys.path.insert(0, str(config.PROJECT / "tools" / "bouze"))
import bouze  # noqa: E402
import records  # noqa: E402


def fnv64(b: bytes) -> str:
    h = 1469598103934665603
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


def known_digests() -> tuple[list[int], list[str]]:
    text = config.game_toml().read_text(encoding="utf-8")
    sizes = re.search(r"known_sizes\s*=\s*\[([^\]]*)\]", text)
    md5 = re.search(r"known_md5\s*=\s*\[([^\]]*)\]", text, re.S)
    return ([int(x) for x in re.findall(r"\d+", sizes.group(1))] if sizes else [],
            re.findall(r"[0-9a-fA-F]{32}", md5.group(1)) if md5 else [])


class DiscCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = config.disc_image()
        if cls.image is None:
            raise unittest.SkipTest("disc image not found (game.toml [game] disc or BBT_DISC)")
        cls.iso = Iso(cls.image)

    @classmethod
    def tearDownClass(cls):
        cls.iso.close()


class Identity(DiscCase):
    def test_is_the_known_dump(self):
        sizes, md5s = known_digests()
        self.assertTrue(sizes and md5s, "game.toml [prepare_disc] has no known size / MD5")
        self.assertIn(self.image.stat().st_size, sizes)
        m = hashlib.md5()
        with open(self.image, "rb") as f:
            for block in iter(lambda: f.read(1 << 22), b""):
                m.update(block)
        self.assertIn(m.hexdigest(), [x.lower() for x in md5s],
                      "not the dump the project's addresses were verified on")


class Layout(DiscCase):
    def test_boot_files(self):
        self.assertIn("SLUS_011.44", self.iso.files)
        cnf = self.iso.read_file("SYSTEM.CNF").decode("ascii", "replace")
        self.assertIn("SLUS_011.44", cnf)

    def test_bouze_files(self):
        bzz = [n for n in self.iso.files if n.endswith(".BZZ")]
        self.assertEqual(len(bzz), 63)
        for n in ("LEVEL1/TITLE.BZZ", "LEVEL1/LANGUAGE.BZZ", "LEVEL1/GRANNY.BZZ",
                  "LEVEL5/TRCA01.BZZ", "CC/CCINTRO.BZZ"):
            self.assertIn(n, self.iso.files)
        self.assertEqual({n.split("/")[0] for n in bzz}, {"CC", "LEVEL1", "LEVEL2", "LEVEL3", "LEVEL5", "LOADING"})


class Decoding(DiscCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        if not config.probe_exe().is_file():
            raise unittest.SkipTest(f"{config.probe_exe()} not built")
        cls.bzz = sorted(n for n in cls.iso.files if n.endswith(".BZZ"))

    def probe(self, name: str) -> dict:
        lba, size = self.iso.files[name]
        r = subprocess.run([str(config.probe_exe()), "bzz", str(self.image), str(lba), str(size)],
                           capture_output=True, text=True, timeout=120)
        return json.loads(r.stdout.splitlines()[0])

    def test_every_file_decodes_identically_in_c_and_python(self):
        for name in self.bzz:
            with self.subTest(file=name):
                c = self.probe(name)
                self.assertTrue(c["ok"] and c["decoded"], c)
                self.assertTrue(c["stream_end"], "record stream does not end at 0x2E")
                data = self.iso.read_file(name)
                h = bouze.header(data)
                self.assertEqual([t for t, _, _ in h["chunks"]], [x["type"] for x in c["chunks"]])
                self.assertEqual(h["chunks"][0][0], 1, "chunk 0 is the record stream")
                for i, off in enumerate(bouze.chunk_offsets(h)):
                    py = bouze.decompress(data, off, h["chunks"][i][1])
                    self.assertEqual((len(py), fnv64(py)), (c["chunks"][i]["len"], c["chunks"][i]["fnv"]),
                                     f"chunk {i}")
                    if i == 0:
                        recs, unknown, end = records.parse(py)
                        self.assertIsNotNone(end, "Python parser found no end marker")
                        self.assertEqual(len(recs), c["records"])
                        # Tags the loader's tables give no payload. Only tag
                        # 0x62 of kinds 0x20 and 0x63 occurs (177 times in 60
                        # files); the streams still end exactly at 0x2E, so it
                        # carries none. A new one would mean a grammar gap.
                        self.assertLessEqual({k for k in unknown if k[0] != "kind"},
                                             {(0x20, 0x62), (0x63, 0x62)}, dict(unknown))


if __name__ == "__main__":
    unittest.main()
