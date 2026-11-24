"""Hermetic tests of the project's Python tools and test helpers: the Bouze
decoder, record parser and keyframe parser in tools/bouze/ (cross-checked
against the C code through data the C unit tests build), the ISO 9660
reader, disc lookup, and the game-run helpers. No disc needed."""
from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config, game  # noqa: E402
from bbt.iso9660 import Iso  # noqa: E402
from bbt.png import dark_rows, row_means  # noqa: E402

ROOT = config.PROJECT
sys.path.insert(0, str(config.PROJECT / "tools" / "bouze"))
import bouze  # noqa: E402
import keyframes  # noqa: E402
import records  # noqa: E402


def store(data: bytes) -> bytes:
    """Literal-only Bouze LZ stream (same layout as tests/unit/synth.c)."""
    n = len(data) - 1
    out = bytearray([0, (n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF])
    for i in range(0, len(data), 8):
        out.append(0xFF)
        out += data[i:i + 8]
    return bytes(out)


class BouzeDecoder(unittest.TestCase):
    def test_literals(self):
        for n in (1, 7, 8, 9, 16, 17, 100):
            data = bytes((i * 37) & 0xFF for i in range(n))
            self.assertEqual(bouze.decompress(store(data)), data)

    def test_stops_at_the_token_count(self):
        s = store(b"ABC") + bytes(16)
        self.assertEqual(bouze.decompress(s, 0, len(s)), b"ABC")

    def test_overlapping_match(self):
        s = bytes([0x00, 0x00, 0x00, 0x03, 0x07]) + b"ABC" + bytes([0x01, 0x84])
        self.assertEqual(bouze.decompress(s), b"ABCABCABCA")

    def test_long_length(self):
        v = (1 << 5) | 0x15
        s = bytes([0x0A, 0x00, 0x00, 0x01, 0x01]) + b"Z" + bytes([v >> 8, v & 0xFF])
        self.assertEqual(bouze.decompress(s), b"Z" * 27)

    def test_header(self):
        sector = struct.pack("<III", 1, 0x1234, 2) + struct.pack("<III", 1, 10, 0x800)
        sector += struct.pack("<III", 4, 0x900, 0x1000)
        h = bouze.header(sector.ljust(0x800, b"\0"))
        self.assertEqual(h["data_off"], 0x1234)
        self.assertEqual(bouze.chunk_offsets(h), [0x800, 0x1000])


class CrossCheckWithC(unittest.TestCase):
    """The C unit-test binary writes its synthetic level and the synthetic
    spin; the Python tools must read them the same way the C code does."""

    @classmethod
    def setUpClass(cls):
        exe = config.unit_exe()
        if not exe.is_file():
            raise unittest.SkipTest(f"{exe} not built")
        cls.tmp = tempfile.TemporaryDirectory()
        d = Path(cls.tmp.name)
        r = subprocess.run([str(exe), "--dump-spin", str(d / "spin.bin"), "--dump-streams", str(d)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise AssertionError(f"bbt_unit_tests --dump-spin failed: {r.stdout}{r.stderr}")
        cls.dir = d

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_bzz_chunks_decode_to_the_plain_data(self):
        f = (self.dir / "synth.bzz").read_bytes()
        h = bouze.header(f)
        self.assertEqual([c[0] for c in h["chunks"]], [1, 3, 4])
        for i, off in enumerate(bouze.chunk_offsets(h)):
            got = bouze.decompress(f, off, h["chunks"][i][1])
            self.assertEqual(got, (self.dir / f"synth_chunk{i}.bin").read_bytes(), f"chunk {i}")

    def test_record_stream(self):
        s = (self.dir / "synth_chunk0.bin").read_bytes()
        recs, unknown, end = records.parse(s)
        self.assertEqual(end, len(s) - 1)
        self.assertEqual([k for _, k, _ in recs].count(0x22), 7)
        self.assertEqual(sum(1 for _, k, _ in recs if k in (7, 8, 9, 10, 0x63)), 3)
        # Each record is preceded by a 0x2C byte, which the loader skips as
        # an unhandled kind, like the stray 0x99.
        self.assertEqual(unknown[("kind", 0x99)], 1)
        self.assertEqual(unknown[("kind", 0x2C)], 11)
        self.assertEqual(set(unknown), {("kind", 0x99), ("kind", 0x2C)})
        actors = [records.actor_info(t) for _, k, t in recs if k == 7]
        self.assertEqual([a["t08"] for a in actors], [0x01008381, 0x05000190, 0x05000181])
        self.assertEqual(actors[1]["res"], [20, 21, 22])
        res = {r["id"]: r for r in (records.resource_info(k, t) for _, k, t in recs if k == 0x22)}
        self.assertEqual(res[21]["sub"], 0x0033)
        self.assertEqual(res[20]["blobs"][0][0], "model")

    def test_spin_blob_parses_with_a_root_on_node_23_every_frame(self):
        blob = (self.dir / "spin.bin").read_bytes()
        frames, mode, fr, used = keyframes.parse(blob)
        self.assertEqual(frames, 32)
        self.assertEqual(mode, 1)
        self.assertEqual(used, len(blob))
        self.assertNotIn(None, fr)
        for i, cmds in enumerate(fr):
            ops = [c for c, _ in cmds]
            self.assertIn(0x0A17, ops, f"frame {i}")
        box = fr[0][0]
        self.assertEqual(box, (0x0900, (0,) * 7))
        self.assertEqual(fr[0][1][0], 0x0D01)


def sector2352(user: bytes, lba: int) -> bytes:
    """A MODE2 form 1 raw sector around 2048 user bytes (EDC/ECC zero)."""
    m, s, f = lba // 4500, (lba // 75) % 60, lba % 75
    bcd = lambda v: (v // 10) << 4 | (v % 10)  # noqa: E731
    head = bytes([0] + [0xFF] * 10 + [0]) + bytes([bcd(m), bcd(s + 2), bcd(f), 2])
    return head + bytes(8) + user + bytes(280)


def dir_record(name: bytes, lba: int, size: int, is_dir: bool) -> bytes:
    n = 33 + len(name)
    n += n & 1
    r = bytearray(n)
    r[0] = n
    struct.pack_into("<I", r, 2, lba)
    struct.pack_into(">I", r, 6, lba)
    struct.pack_into("<I", r, 10, size)
    struct.pack_into(">I", r, 14, size)
    r[25] = 2 if is_dir else 0
    r[32] = len(name)
    r[33:33 + len(name)] = name
    return bytes(r)


def make_iso() -> tuple[list[bytes], dict[str, bytes]]:
    files = {"SYSTEM.CNF": b"BOOT = cdrom:\\SLUS_011.44;1\r\n",
             "LEVEL5/TRCA01.BZZ": bytes(range(256)) * 12}          # 3072 bytes, 2 sectors
    sectors = [bytes(2048)] * 24
    pvd = bytearray(2048)
    pvd[0], pvd[1:6], pvd[6] = 1, b"CD001", 1
    pvd[40:72] = b"SLUS_01144".ljust(32)
    pvd[156:190] = dir_record(b"\x00", 18, 2048, True)
    sectors[16] = bytes(pvd)
    sectors[17] = bytes([255]) + b"CD001" + bytes(2042)
    root = dir_record(b"\x00", 18, 2048, True) + dir_record(b"\x01", 18, 2048, True)
    root += dir_record(b"LEVEL5", 19, 2048, True) + dir_record(b"SYSTEM.CNF;1", 20, len(files["SYSTEM.CNF"]), False)
    sectors[18] = root.ljust(2048, b"\0")
    sub = dir_record(b"\x00", 19, 2048, True) + dir_record(b"\x01", 18, 2048, True)
    sub += dir_record(b"TRCA01.BZZ;1", 21, len(files["LEVEL5/TRCA01.BZZ"]), False)
    sectors[19] = sub.ljust(2048, b"\0")
    sectors[20] = files["SYSTEM.CNF"].ljust(2048, b"\0")
    t = files["LEVEL5/TRCA01.BZZ"]
    sectors[21], sectors[22] = t[:2048], t[2048:].ljust(2048, b"\0")
    return sectors, files


class Iso9660(unittest.TestCase):
    def check(self, image: Path, files: dict[str, bytes]):
        with Iso(image) as iso:
            self.assertEqual(iso.volume_id, "SLUS_01144")
            self.assertEqual(set(iso.files), set(files))
            self.assertIn("LEVEL5", iso.dirs)
            for name, data in files.items():
                self.assertEqual(iso.read_file(name), data)
            self.assertEqual(iso.files["LEVEL5/TRCA01.BZZ"], (21, 3072))

    def test_2048_byte_image(self):
        sectors, files = make_iso()
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "t.iso"
            p.write_bytes(b"".join(sectors))
            self.check(p, files)

    def test_raw_mode2_image(self):
        sectors, files = make_iso()
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "t.bin"
            p.write_bytes(b"".join(sector2352(s, i) for i, s in enumerate(sectors)))
            with Iso(p) as iso:
                self.assertEqual((iso.sector_size, iso.user_offset), (2352, 24))
            self.check(p, files)

    def test_not_an_iso(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.bin"
            p.write_bytes(bytes(2048 * 20))
            with self.assertRaises(ValueError):
                Iso(p)


class DiscLookup(unittest.TestCase):
    def test_cue_names_the_bin(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "Game (USA).bin").write_bytes(b"x")
            cue = d / "game.cue"
            cue.write_text('FILE "Game (USA).bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
            toml = d / "game.toml"
            toml.write_text('[game]\nname = "x"\ndisc = "game.cue"\n')
            import os
            old = dict(os.environ)
            try:
                os.environ.pop("BBT_DISC", None)
                os.environ["BBT_GAME_TOML"] = str(toml)
                self.assertEqual(config.disc_cue(), cue)
                self.assertEqual(config.disc_image(), d / "Game (USA).bin")
            finally:
                os.environ.clear()
                os.environ.update(old)

    def test_toml_fallback_parser(self):
        text = '[runtime]\ndisc = "wrong"\n\n[game]\nid = "SLUS-01144"\ndisc = "/a/b.cue"\n'
        self.assertEqual(config._toml_disc(text), "/a/b.cue")


class GameRunHelpers(unittest.TestCase):
    def test_three_player_toml(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            src = d / "game.toml"
            cue = (d / "x" / "y.cue").resolve().as_posix()  # absolute on every OS
            src.write_text(f'[game]\nplayers = 2\nexe = "disc/SLUS_011.44"\ndisc = "{cue}"\n\n'
                           '[controller]\ndefault_mode = "digital"\n\n[netplay]\nrequire_cue = true\n')
            out = game.three_player_toml(src, d / "out.toml")
            text = out.read_text()
            self.assertIn("players = 3", text)
            self.assertIn(f'exe = "{(d / "disc/SLUS_011.44").resolve().as_posix()}"', text)
            self.assertIn(f'disc = "{cue}"', text)
            ctrl = text.split("[controller]")[1].split("[netplay]")[0]
            self.assertIn("multitap_port = 2", ctrl)
            try:
                import tomllib
            except ModuleNotFoundError:
                return
            t = tomllib.loads(text)
            self.assertEqual(t["game"]["players"], 3)
            self.assertEqual(t["controller"]["multitap_port"], 2)

    def test_state_toml(self):
        text = game.state_toml({"trca01": True, "swap-test": False})
        try:
            import tomllib
        except ModuleNotFoundError:
            self.assertIn('id = "trca01"', text)
            return
        t = tomllib.loads(text)
        self.assertEqual(t["format_version"], 2)
        self.assertEqual(t["package"], [{"id": "bbt.third-character"}, {"id": "bbt.own-view"},
                                        {"id": "bbt.quick-start"}])
        # the features on by default are written as off unless named
        self.assertEqual({(f["package_id"], f["id"]): f["enabled"] for f in t["feature"]},
                         {("bbt.third-character", "trca01"): True, ("bbt.third-character", "swap-test"): False,
                          ("bbt.third-character", "p3"): False, ("bbt.own-view", "two-views"): False,
                          ("bbt.quick-start", "quick-start"): False})
        self.assertEqual(tomllib.loads(game.state_toml({"_defaults": True})), {"format_version": 2})
        # other packages and option values, as the runtime writes them
        t = tomllib.loads(game.state_toml({
            "trca01": True,
            "bbt.quick-start/quick-start": {"enabled": True, "values": {"skip_intro": "false"}}}))
        self.assertEqual(t["package"], [{"id": "bbt.third-character"}, {"id": "bbt.quick-start"},
                                        {"id": "bbt.own-view"}])
        qs = [f for f in t["feature"] if f["package_id"] == "bbt.quick-start"]
        self.assertEqual(qs, [{"package_id": "bbt.quick-start", "id": "quick-start", "enabled": True,
                               "values": {"language": "ask", "skip_title": "false", "auto_players": "false",
                                          "skip_intro": "false"}}])
        # options the tests were written with; a named option wins
        t = tomllib.loads(game.state_toml({"p3": True, "bbt.own-view/two-views": True}))
        vals = {f["id"]: f.get("values") for f in t["feature"]}
        self.assertEqual(vals["p3"], {"appear": "always", "comeback": "team"})
        self.assertEqual(vals["two-views"], {"offline": "split"})
        t = tomllib.loads(game.state_toml({"p3": {"enabled": True, "values": {"appear": "auto"}}}))
        self.assertEqual([f.get("values") for f in t["feature"] if f["id"] == "p3"],
                         [{"appear": "auto", "comeback": "team"}])
        t = tomllib.loads(game.state_toml({"_defaults": True, "p3": True}))
        self.assertEqual(t["feature"], [{"package_id": "bbt.third-character", "id": "p3", "enabled": True}])

    def test_game_toml_copy(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "proj" / "game.toml"
            src.parent.mkdir()
            cue = (Path(d) / "abs" / "d.cue").resolve().as_posix()  # absolute on every OS
            src.write_text(f'[game]\nplayers = 2\nexe = "disc/X.EXE"\ndisc = "{cue}"\n\n[controller]\ndefault_mode = "digital"\n')
            dst = game.game_toml_copy(src, Path(d) / "out" / "game.toml")
            text = dst.read_text()
            self.assertIn(f'exe = "{(src.parent / "disc/X.EXE").resolve().as_posix()}"', text)
            self.assertIn(f'disc = "{cue}"', text)
            self.assertIn("players = 2", text)
            self.assertIn('default_mode = "digital"', text)

    def test_state_restore_after_an_interrupted_run(self):
        with tempfile.TemporaryDirectory() as d:
            build = Path(d)
            mods = build / "mods"
            mods.mkdir()
            (mods / "state.toml").write_text("test selection\n")
            (mods / game.STATE_BACKUP).write_text("player selection\n")
            game.restore_state(build)
            self.assertEqual((mods / "state.toml").read_text(), "player selection\n")
            self.assertFalse((mods / game.STATE_BACKUP).exists())
            (mods / game.STATE_BACKUP).write_text(game.ABSENT_MARKER)
            game.restore_state(build)
            self.assertFalse((mods / "state.toml").exists())
            game.restore_state(build)                     # nothing to do

    def test_release_build_is_protected(self):
        import os
        old = os.environ.pop("BBT_ALLOW_RELEASE", None)
        try:
            self.assertTrue(game.protected_build(config.PROJECT / "build-release"))
            self.assertFalse(game.protected_build(config.PROJECT / "build-dev"))
        finally:
            if old is not None:
                os.environ["BBT_ALLOW_RELEASE"] = old


def _png(rows: list[bytes], width: int, bpp: int, filters: list[int]) -> bytes:
    """A PNG with each row stored with the given filter type (unfiltered
    pixel bytes in, filtered on the way out)."""
    import zlib

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

    raw, prev = b"", bytes(width * bpp)
    for row, f in zip(rows, filters):
        out = bytearray()
        for i, v in enumerate(row):
            left = row[i - bpp] if i >= bpp else 0
            up, up_left = prev[i], (prev[i - bpp] if i >= bpp else 0)
            pred = (0, left, up, (left + up) >> 1, paeth(left, up, up_left))[f]
            out.append((v - pred) & 0xFF)
        raw += bytes([f]) + bytes(out)
        prev = row

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    ihdr = struct.pack(">IIBBBBB", width, len(rows), 8, 2 if bpp == 3 else 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


class PngRows(unittest.TestCase):
    def test_row_means_through_every_filter(self):
        width = 4
        rows = [bytes([10, 20, 30] * width), bytes([0, 0, 0] * width), bytes([200, 100, 0] * width),
                bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]), bytes([255] * 12)]
        with tempfile.TemporaryDirectory() as d:
            for bpp in (3, 4):
                packed = [r if bpp == 3 else b"".join(r[i:i + 3] + b"\xff" for i in range(0, 12, 3))
                          for r in rows]
                path = Path(d) / f"t{bpp}.png"
                path.write_bytes(_png(packed, width, bpp, [0, 1, 2, 3, 4]))
                means = row_means(path)
                self.assertEqual([round(m, 3) for m in means], [20.0, 0.0, 100.0, 6.5, 255.0])
                self.assertEqual(dark_rows(means, 0.0, 1.0), [0.2])


class NetHelpers(unittest.TestCase):
    # a line as build-net logs it (psx_netplay.c np_log_live_digest)
    LINE = ("psxrecomp: rb live dig local sim=32 core=62c266f1 cpu=b72e1f59 clk=3ad7c244 "
            "tim=5b6ef7f4 ram=5c41366b dirty=09beedb8 mod=00000000 av=35f9278a cd=faa79c21 "
            "spu=32a339b4 mdec=12c3a1e5 aux=64791c8b")

    def test_digest_lines(self):
        from bbt import net
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / "game.log"
            log.write_text("psxrecomp: something else\n" + self.LINE + "\n"
                           + self.LINE.replace("sim=32", "sim=64").replace("core=62c266f1", "core=00000001")
                           + "\n")
            self.assertEqual(net.digests(log), {
                32: ("62c266f1", "5c41366b", "00000000", "35f9278a"),
                64: ("00000001", "5c41366b", "00000000", "35f9278a")})
            self.assertEqual(net.digests(Path(d) / "missing.log"), {})
            # a rollback from tick 64 drops 64 and later until replayed; a
            # refused one drops nothing
            at = lambda n, core: self.LINE.replace("sim=32", f"sim={n}").replace("core=62c266f1", f"core={core}")
            log.write_text("\n".join([
                at(32, "00000032"), at(64, "0000bad0"), at(96, "0000bad1"),
                "psxrecomp: rb begin epoch=3 load=64 frontier=100",
                at(64, "00000064"),
                "psxrecomp: rb follow REFUSED epoch=4 load=32 — reason",
                "psxrecomp: rb follow NACK epoch=5 load=32 frontier=90",
            ]) + "\n")
            self.assertEqual({k: v[0] for k, v in net.digests(log).items()},
                             {32: "00000032", 64: "00000064"})

    def test_peer_folder_has_its_own_mods(self):
        from bbt import net
        with tempfile.TemporaryDirectory() as d:
            build = Path(d) / "build"
            (build / "bios").mkdir(parents=True)
            (build / "bios" / "openbios.bin").write_bytes(b"\0" * 16)
            (build / "mods" / "bundled" / "pkg").mkdir(parents=True)
            (build / "mods" / "bundled" / "pkg" / "manifest.toml").write_text("id = 1\n")
            (build / "mods" / "state.toml").write_text("the player's selection\n")
            exe = build / "Game"
            exe.write_bytes(b"exe")
            link = net._peer_folder(build, exe, Path(d) / "peer0", {"trca01": True})
            self.assertEqual(link.read_bytes(), b"exe")
            self.assertEqual(link.parent, Path(d) / "peer0")
            self.assertTrue((link.parent / "bios" / "openbios.bin").is_file())
            self.assertTrue((link.parent / "mods" / "bundled" / "pkg" / "manifest.toml").is_file())
            self.assertIn('id = "trca01"', (link.parent / "mods" / "state.toml").read_text())
            self.assertEqual((build / "mods" / "state.toml").read_text(), "the player's selection\n")


class NetplayCompatibility(unittest.TestCase):
    """Every bbt.* package and VERSION carry one netplay compatibility
    number (0.<N>.0): a LAN guest without the host's package version is
    refused, which is what keeps two different builds out of one match
    (NOTES.md "Netplay compatibility")."""

    def test_one_version_everywhere(self):
        try:
            import tomllib
        except ModuleNotFoundError:
            self.skipTest("tomllib needs Python 3.11")
        root = config.PROJECT
        version = (root / "VERSION").read_text(encoding="utf-8").strip()
        manifests = sorted((root / "mods" / "preloaded" / "packages").glob("bbt.*/*/manifest.toml"))
        self.assertGreaterEqual(len(manifests), 4)
        for m in manifests:
            with self.subTest(package=m.parent.parent.name):
                data = tomllib.loads(m.read_text(encoding="utf-8"))
                self.assertEqual(data["version"], m.parent.name, "folder and version differ")
                self.assertEqual(data["version"], version, "not the shared compatibility number")
        self.assertEqual(len({m.parent.parent.name for m in manifests}), len(manifests),
                         "a package has two version folders")


class ThreePlayerConfig(unittest.TestCase):
    """game_3p.toml (the shipped 3-player config) is tools/make_3p_toml.py's
    output for the current game.toml: only players and the multitap differ,
    so a change to game.toml (e.g. its [timing] block) cannot be missing
    from it again (it was, 2026-11-08: three views ran at ~55% speed)."""

    def test_it_is_up_to_date(self):
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "make_3p_toml.py"), "--check"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_only_players_and_the_multitap_differ(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import make_3p_toml
        src = (ROOT / "game.toml").read_text(encoding="utf-8")
        out = make_3p_toml.three_player(src)
        a = [l for l in src.splitlines() if not l.startswith("#")]
        b = [l for l in out.splitlines() if not l.startswith("#")]
        extra = [l for l in b if l not in a]
        self.assertEqual(sorted(extra), ["multitap_analog = false", "multitap_port = 2", "players = 3"])


if __name__ == "__main__":
    unittest.main()
