"""The GameCube fingerprints are written from a disc image's files alone, as hashes only, a set
for each revision; synthetic images, no game data."""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[2] / "scripts" / "assets"
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gamecube_fingerprints
from test_installer import synthetic_ciso, synthetic_iso

# Where a disc's header keeps its revision.
REVISION_AT = 7


def run(*arguments) -> tuple[int, str, str]:
    """The script's exit code and what it wrote to its two streams."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = gamecube_fingerprints.main([str(argument) for argument in arguments])
    return code, out.getvalue(), err.getvalue()


class GameCubeFingerprints(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.folder = Path(scratch.name)
        self.output = self.folder / "out" / "gamecube-us.json"
        self.iso = self.folder / "game.iso"
        self.iso.write_bytes(synthetic_iso())

    def written(self) -> dict:
        return json.loads(self.output.read_text(encoding="utf-8"))

    def test_an_iso_and_its_ciso_give_the_same_hashes_and_nothing_else(self):
        self.assertEqual(run(self.iso, "--output", self.output)[0], 0)
        held = self.written()
        self.assertEqual({key: held[key] for key in ("format", "version", "gameId")},
                         {"format": "gdl-gamecube-fingerprints", "version": 1, "gameId": "GUNE5D"})
        (only,) = held["revisions"]
        self.assertEqual(only["revision"], synthetic_iso()[REVISION_AT])
        rows = only["files"]
        self.assertTrue(rows)
        self.assertEqual(rows, sorted(rows, key=lambda row: row["pathSha256"]))
        for row in rows:
            self.assertEqual(set(row), {"pathSha256", "sha256"})
            for digest in row.values():
                self.assertRegex(digest, "^[0-9a-f]{64}$")
        # The war's WAD, by the digest of its path as the disc spells it.
        key = hashlib.sha256(b"Gauntlet/PDATA/WAR.WAD").hexdigest()
        self.assertIn(key, {row["pathSha256"] for row in rows})
        # No name of the disc's tree is written.
        self.assertNotIn("WAD", self.output.read_text(encoding="utf-8").upper())

        ciso = self.folder / "game.ciso"
        ciso.write_bytes(synthetic_ciso(synthetic_iso()))
        code, out, _ = run(ciso, "--output", self.output, "--check")
        self.assertEqual(code, 0)
        self.assertIn(f"{len(rows)} fingerprints match", out)

    def test_a_changed_file_fails_the_check(self):
        self.assertEqual(run(self.iso, "--output", self.output)[0], 0)
        before = self.output.read_bytes()
        image = bytearray(synthetic_iso())
        with gamecube_fingerprints.DiscImage(self.iso) as disc:
            entry = next(entry for entry in disc.entries if not entry.directory and entry.size)
        image[entry.offset] ^= 1
        self.iso.write_bytes(image)
        code, _, err = run(self.iso, "--output", self.output, "--check")
        self.assertEqual(code, 1)
        self.assertIn("does not hold these fingerprints", err)
        self.assertEqual(self.output.read_bytes(), before)

    def test_a_revision_is_kept_beside_another_and_replaced_only_when_asked(self):
        self.assertEqual(run(self.iso, "--output", self.output)[0], 0)
        first = synthetic_iso()[REVISION_AT]
        code, _, err = run(self.iso, "--output", self.output)
        self.assertEqual(code, 1)
        self.assertIn("--replace", err)
        self.assertEqual(run(self.iso, "--output", self.output, "--replace")[0], 0)

        other = bytearray(synthetic_iso())
        other[REVISION_AT] = first ^ 1
        self.iso.write_bytes(other)
        self.assertEqual(run(self.iso, "--output", self.output)[0], 0)
        revisions = [one["revision"] for one in self.written()["revisions"]]
        self.assertEqual(revisions, sorted({first, first ^ 1}))

    def test_another_game_and_another_output_are_refused(self):
        image = bytearray(synthetic_iso())
        image[0:6] = b"GUNP5D"
        self.iso.write_bytes(image)
        code, _, err = run(self.iso, "--output", self.output)
        self.assertEqual(code, 1)
        self.assertIn("GUNE5D", err)
        self.assertFalse(self.output.exists())

        self.iso.write_bytes(synthetic_iso())
        self.output.parent.mkdir(parents=True)
        self.output.write_text('{"format": "other"}', encoding="utf-8")
        code, _, err = run(self.iso, "--output", self.output)
        self.assertEqual(code, 1)
        self.assertIn("is not of version 1 of the format", err)


if __name__ == "__main__":
    unittest.main()
