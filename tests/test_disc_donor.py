"""Disc tier: the TRCA01 enemy as the plugin builds it from the player's
disc, and what the plugin would do to every level that has Taz.

bbt_disc_probe runs the plugin's own host-side code with its own role
table (src/mods/trca01_profile.h). The expected values were verified in the
game (NOTES.md "M2", "M3", "M4"): the enemy's model, its 13 texture blocks
and CLUTs as they sit in TRCA01's VRAM, the tornado's attach node 23.
Skipped without the disc."""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.iso9660 import Iso  # noqa: E402

sys.path.insert(0, str(config.PROJECT / "tools" / "bouze"))
import keyframes  # noqa: E402

TAZ = 0x05000190
DONOR = 0x01008381

# The enemy's texel blocks in TRCA01's VRAM by CLUT field (x, y, w, h),
# checked against VRAM in TRCA01 and after relocation in Granwich/VICA01.
TRCA01_BLOCKS = {
    0x7C06: (280, 464, 8, 32), 0x7C04: (280, 432, 8, 32), 0x7BCE: (272, 467, 8, 29),
    0x7C46: (272, 400, 8, 32), 0x7C8E: (272, 496, 4, 16), 0x7C40: (276, 496, 4, 16),
    0x7C44: (288, 256, 4, 20), 0x7BCC: (272, 432, 8, 32), 0x7B80: (280, 336, 7, 32),
    0x7C00: (280, 368, 8, 32), 0x7C02: (280, 400, 8, 32), 0x7C42: (272, 368, 8, 32),
    0x7C0C: (276, 256, 4, 16),
}

# Levels where Taz's VRAM cannot hold the enemy's textures (BUGS.md #20).
# The enemy then keeps TRCA01's texture fields there.
KNOWN_NO_SPACE = {"LEVEL1/AZR01.BZZ"}


class ProbeCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = config.disc_image()
        if cls.image is None:
            raise unittest.SkipTest("disc image not found (game.toml [game] disc or BBT_DISC)")
        if not config.probe_exe().is_file():
            raise unittest.SkipTest(f"{config.probe_exe()} not built")
        cls.iso = Iso(cls.image)
        cls.donor_file = cls.iso.files["LEVEL5/TRCA01.BZZ"]

    @classmethod
    def tearDownClass(cls):
        cls.iso.close()

    @classmethod
    def probe(cls, *args) -> list[dict]:
        r = subprocess.run([str(config.probe_exe()), *map(str, args)], capture_output=True,
                           text=True, timeout=300)
        return [json.loads(line) for line in r.stdout.splitlines() if line.strip()]


class Donor(ProbeCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.spin_path = config.out_dir() / "spin_trca01.bin"   # synthetic, not disc data
        lba, size = cls.donor_file
        cls.d = cls.probe("donor", cls.image, lba, size, cls.spin_path)[0]

    def test_extracted(self):
        self.assertTrue(self.d["ok"], self.d)
        bodies = [m for m in self.d["models"] if m["sub"] == 0x000D]
        self.assertEqual(len(bodies), 1)
        self.assertEqual((bodies[0]["id"], bodies[0]["size"]), (635, 12264))
        self.assertLessEqual(self.d["image_size"], 0x1F000, "must fit below the timing-fix table")

    def test_every_role_has_its_animation(self):
        missing = [m for m in self.d["map"] if not m["found"]]
        self.assertEqual(missing, [], "role table names donor animations the enemy does not have")
        self.assertEqual([x["found"] for x in self.d["defaults"]], [True] * len(self.d["defaults"]))
        # Standing defaults, shortest first (so a play-once animation finds
        # one at least as long as Taz's own).
        frames = [x["frames"] for x in self.d["defaults"]]
        self.assertEqual(frames, sorted(frames))
        self.assertEqual(frames, [21, 34, 71])

    def test_spin_is_built_on_the_attach_node(self):
        self.assertEqual(self.d["spin_node"], 23)
        self.assertEqual(self.d["spin_frames"], 32)
        spin_roles = [m for m in self.d["map"] if m["donor"] == 0xF001]
        self.assertEqual(sorted(m["taz"] for m in spin_roles), [0x004F, 0x0097])
        blob = self.spin_path.read_bytes()
        frames, _, fr, used = keyframes.parse(blob)
        self.assertEqual((frames, used), (32, len(blob)))
        for i, cmds in enumerate(fr):
            self.assertIn(0x0A17, [c for c, _ in cmds], f"frame {i} has no root on node 23")

    def test_spin_start_carries_a_root_command(self):
        """Taz's spin start (0x0072) must play a donor animation with root
        commands, or the game attaches no tornado (the "invisible" bug)."""
        lba, size = self.donor_file
        blobs = {sub: blob for sub, _, blob in keyframes.anim_blobs(self.image, lba, size, DONOR)}
        start = next(m["donor"] for m in self.d["map"] if m["taz"] == 0x0072)
        _, _, fr, _ = keyframes.parse(blobs[start])
        with_root = sum(1 for cmds in fr if any(c & 0xF00 == 0xA00 and c & 0xFF == 23 for c, _ in cmds))
        self.assertGreaterEqual(with_root, len(fr) - 1)

    def test_textures(self):
        t = self.d["tex"]
        self.assertTrue(t["ok"])
        self.assertEqual(t["tpage"], 0x14)
        self.assertEqual(t["groups"], 13)
        self.assertEqual(t["skipped"], 0)
        got = {c: tuple(b) for c, b in zip(t["cluts"], t["blocks"])}
        self.assertEqual(got, TRCA01_BLOCKS)
        self.assertEqual(t["pixels"], sum(w * h for _, _, w, h in TRCA01_BLOCKS.values()) + 16 * 13)

    def test_third_actor_hold(self):
        """Phase 4: the hold animation (moves without a role) is the enemy
        stance's first frame byte for byte, then frames without commands."""
        h = self.d["hold"]
        self.assertTrue(h["ok"], h)
        lba, size = self.donor_file
        blobs = {sub: blob for sub, _, blob in keyframes.anim_blobs(self.image, lba, size, DONOR)}
        stance = blobs[0x001C]
        _, _, fr, _ = keyframes.parse(stance)
        f0_bytes = 8 + sum(2 + 2 * len(args) for _, args in fr[0])
        self.assertEqual((h["frame0_cmds"], h["frame0_bytes"]), (len(fr[0]), f0_bytes))
        self.assertEqual(h["frames"], 128)
        self.assertEqual(h["empty_frames"], 127)
        self.assertEqual(h["size"], 8 + f0_bytes + 127 * 8)
        # every node posed absolutely on that frame (full / part poses), so
        # it does not depend on the pose before it
        self.assertEqual({c & 0xFF for c, _ in fr[0]} - {0}, set(range(1, 24)))
        self.assertTrue(all((c & 0xF00) in (0x100, 0x400, 0x900, 0xD00) for c, _ in fr[0]))

    def test_third_actor_strip(self):
        """Phase 4: the enemy's 13 blocks and CLUTs fit the free VRAM strip
        (x 512..1023, y 240..255), each block inside one texture page, the
        taller blocks at half height, and every patched record samples
        inside its block, on the source row it would have (rounded down)."""
        s = self.d["tex"]["strip"]
        self.assertTrue(s["ok"])
        self.assertEqual((s["texels_outside"], s["wrong_rows"]), (0, 0))
        rects = []
        for (x, y, w, h, shift), (_, _, _, sh) in zip(s["blocks"], self.d["tex"]["blocks"]):
            self.assertTrue(512 <= x and x + w <= 1024 and 240 <= y and y + h <= 256, (x, y, w, h))
            self.assertEqual(x // 64, (x + w - 1) // 64)
            self.assertEqual(shift, 0 if sh <= 16 else 1)
            self.assertEqual(h, (sh + (1 << shift) - 1) >> shift)
            rects.append((x, y, w, h))
        for x, y in s["cluts"]:
            self.assertEqual(x % 16, 0)
            self.assertTrue(512 <= x and x + 16 <= 1024 and 240 <= y < 256)
            rects.append((x, y, 16, 1))
        overlaps = [(a, b) for i, a in enumerate(rects) for b in rects[i + 1:]
                    if a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]]
        self.assertEqual(overlaps, [])


class EnemyAnimations(ProbeCase):
    def test_all_parse_exactly(self):
        lba, size = self.donor_file
        blobs = keyframes.anim_blobs(self.image, lba, size, DONOR)
        self.assertEqual(len(blobs), 52)
        for sub, rid, blob in blobs:
            frames, _, fr, used = keyframes.parse(blob)
            self.assertEqual(used, len(blob), f"anim {sub:04X} (res {rid})")
            self.assertNotIn(None, fr)

    def test_taz_in_granwich(self):
        lba, size = self.iso.files["LEVEL1/GRANNY.BZZ"]
        blobs = keyframes.anim_blobs(self.image, lba, size, TAZ)
        self.assertEqual(len(blobs), 67)
        self.assertEqual(len({sub for sub, _, _ in blobs}), 67)
        for sub, rid, blob in blobs:
            _, _, fr, used = keyframes.parse(blob)
            self.assertEqual(used, len(blob), f"anim {sub:04X} (res {rid})")


class Levels(ProbeCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        bzz = sorted(n for n in cls.iso.files if n.endswith(".BZZ"))
        lba, size = cls.donor_file
        rows = cls.probe("levels", cls.image, lba, size,
                         *[f"{n}:{cls.iso.files[n][0]}:{cls.iso.files[n][1]}" for n in bzz])
        cls.rows = {r["name"]: r for r in rows if "name" in r}
        cls.bzz = bzz

    def taz_levels(self):
        return {n: r for n, r in self.rows.items() if r.get("has_taz")}

    def test_every_file_probed(self):
        self.assertEqual(set(self.rows), set(self.bzz))
        self.assertEqual([n for n, r in self.rows.items() if not r["ok"]], [])

    def test_taz_levels(self):
        taz = self.taz_levels()
        self.assertEqual(len(taz), 38)
        for n in ("LEVEL1/GRANNY.BZZ", "LEVEL2/VICA01.BZZ", "LEVEL5/TRCA01.BZZ"):
            self.assertIn(n, taz)
        for n in ("LEVEL1/TITLE.BZZ", "LEVEL1/LANGUAGE.BZZ", "CC/CCINTRO.BZZ"):
            self.assertNotIn(n, taz)

    def test_patch_is_clean_everywhere(self):
        """Only Taz's blob and scale payloads change; the stream keeps its
        records and end; every redirected blob lies inside the donor image;
        his model and every animation are redirected."""
        for n, r in self.taz_levels().items():
            with self.subTest(level=n):
                p = r["patch"]
                self.assertGreater(p["result"], 0)
                self.assertEqual(p["outside_payloads"], 0)
                self.assertTrue(p["targets_in_image"])
                self.assertEqual(p["records"][0], p["records"][1])
                self.assertTrue(p["stream_end"])
                self.assertEqual(p["models"], 1)
                self.assertEqual(p["other"], 0)
                self.assertEqual(p["models"] + p["anims_mapped"] + p["anims_default"], p["ids"])
                self.assertEqual(p["blobs"], p["ids"])
                self.assertEqual(p["scale"], p["actors"])
                self.assertGreaterEqual(p["anims_mapped"], 1)    # at least the set-up animation

    def test_granwich_numbers(self):
        p = self.rows["LEVEL1/GRANNY.BZZ"]["patch"]
        self.assertEqual((p["actors"], p["ids"], p["anims_mapped"], p["anims_default"]), (2, 68, 10, 57))

    def test_texture_plan(self):
        for n, r in self.taz_levels().items():
            with self.subTest(level=n):
                self.assertTrue(r["pool"]["ok"])
                plan = r.get("plan", {})
                if n in KNOWN_NO_SPACE:
                    self.assertFalse(plan.get("ok"), "BUGS #20 fixed? Update KNOWN_NO_SPACE.")
                    continue
                self.assertTrue(plan.get("ok"), "no room for the enemy's textures in Taz's VRAM")
                self.assertEqual(plan["texels_outside"], 0)
                self.assertEqual(plan["overlaps"], 0)
                self.assertEqual(plan["not_uploaded"], 0)
                self.assertEqual(plan["beyond_512"], 0)


if __name__ == "__main__":
    unittest.main()
