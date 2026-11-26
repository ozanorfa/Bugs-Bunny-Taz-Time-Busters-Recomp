"""Disc tier: Daffy Duck as the third actor's plugin builds him from the
player's disc (LEVEL1/RETRY.BZZ), and where his textures go in VRAM.

bbt_disc_probe runs the plugin's own host-side code (donor.c, donor_tex.c)
with its own values (src/mods/daffy_profile.h, p3_profile.h). His model,
animations and texture blocks are in NOTES.md "Daffy Duck on the disc"; the
block sizes and the VRAM plan are regression values from the probe
(2026-11-26) and agree with the game (test_game_daffy test 1b: three groups,
two of them in the tall area, pixel hashes equal). Skipped without the disc.

(Until 2026-11-26 this tier probed the Transylvania enemy, then a third-
character choice and Taz's replacement; both were removed.)"""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.iso9660 import Iso  # noqa: E402
from bbt.p3_vram import STRIP, TALL, inside  # noqa: E402

# His texel blocks in RETRY's VRAM (x, y, w, h), one per CLUT group.
DAFFY_BLOCKS = [(128, 192, 8, 16), (128, 128, 8, 30), (128, 96, 8, 32)]
BLK_IMAGE_MAX = 0x20000 - 0x2000      # third_actor.c: the image part of his mod block


class Daffy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        image = config.disc_image()
        if image is None:
            raise unittest.SkipTest("disc image not found (game.toml [game] disc or BBT_DISC)")
        if not config.probe_exe().is_file():
            raise unittest.SkipTest(f"{config.probe_exe()} not built")
        with Iso(image) as iso:
            lba, size = iso.files["LEVEL1/RETRY.BZZ"]
        r = subprocess.run([str(config.probe_exe()), "daffy", str(image), str(lba), str(size)],
                           capture_output=True, text=True, timeout=300)
        cls.rc = r.returncode
        cls.d = json.loads(r.stdout.splitlines()[0])

    def test_extracted(self):
        """Model 19 (sub 0x0D, 18,964 bytes), the set-up animation, his one
        motion 0x00D9 and the synthetic spin, built on node 39."""
        self.assertTrue(self.d["ok"], self.d)
        self.assertEqual(self.rc, 0)
        self.assertEqual(self.d["models"], [{"sub": 0x0D, "id": 19, "size": 18964}])
        self.assertEqual(self.d["anim_subs"], [0x0001, 0x00D9, 0xF001])
        self.assertEqual((self.d["spin_node"], self.d["spin_frames"]), (39, 32))
        self.assertLessEqual(self.d["image_size"], BLK_IMAGE_MAX)

    def test_hold_pose(self):
        """The fallback hold: 0x00D9's first frame, then 127 frames without
        commands."""
        h = self.d["hold"]
        self.assertTrue(h["ok"], h)
        self.assertEqual((h["frames"], h["empty_frames"]), (128, 127))
        self.assertEqual(h["size"], 8 + h["frame0_bytes"] + 127 * 8)
        self.assertGreater(h["frame0_cmds"], 0)

    def test_textures(self):
        t = self.d["tex"]
        self.assertTrue(t["ok"], t)
        self.assertEqual((t["groups"], t["skipped"]), (3, 0))
        self.assertEqual([tuple(b) for b in t["blocks"]], DAFFY_BLOCKS)
        self.assertEqual(t["pixels"], sum(w * h for _, _, w, h in DAFFY_BLOCKS) + 16 * 3)

    def test_textures_at_full_height_in_free_vram(self):
        """BUGS #25: every block keeps every row: the 16-row one in the strip
        (x 512..1023, y 240..255), the two taller ones in the tall area below
        x = 512; each inside one texture page; the CLUTs 16-aligned in the
        strip; nothing overlaps; every patched record samples inside its
        block, on its source row."""
        s = self.d["tex"]["strip"]
        self.assertTrue(s["ok"], s)
        self.assertEqual((tuple(s["area"]), tuple(s["tall"])), (STRIP, TALL))
        self.assertEqual((s["texels_outside"], s["wrong_rows"]), (0, 0))
        rects, in_tall = [], 0
        for (x, y, w, h, shift), (_, _, _, sh) in zip(s["blocks"], DAFFY_BLOCKS):
            self.assertEqual(shift, 0)
            self.assertEqual(h, sh)
            self.assertEqual(x // 64, (x + w - 1) // 64)
            if sh > STRIP[3]:
                self.assertTrue(inside(x, y, w, h, TALL), (x, y, w, h))
                in_tall += 1
            else:
                self.assertTrue(inside(x, y, w, h, STRIP), (x, y, w, h))
            rects.append((x, y, w, h))
        self.assertEqual(in_tall, 2)
        for x, y in s["cluts"]:
            self.assertEqual(x % 16, 0)
            self.assertTrue(inside(x, y, 16, 1, STRIP))
            rects.append((x, y, 16, 1))
        overlaps = [(a, b) for i, a in enumerate(rects) for b in rects[i + 1:]
                    if a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]]
        self.assertEqual(overlaps, [])


if __name__ == "__main__":
    unittest.main()
