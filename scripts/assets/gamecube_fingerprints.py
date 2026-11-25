#!/usr/bin/env python3
"""Write the fingerprints the launcher can hold a GameCube disc's files to, from a trusted image.

    python scripts/assets/gamecube_fingerprints.py <image> [--output <file>] [--check | --replace]

The image is the player's own USA Gauntlet Dark Legacy disc (GUNE5D), an ISO or a CISO, read as
the installer reads one. A fingerprint is the SHA-256 of a disc path beside the SHA-256 of the
file at it, for every file of the `Gauntlet` and `carddemo` trees: the files an install copies,
so an ISO, a CISO and a scrubbed image of one disc give the same fingerprints. The output, by
default the launcher's copy in src/launcher/crates/gdl-install/fingerprints, keeps one set for
each revision of the disc: this run writes the set of the image's revision and keeps the others
as they are. It holds those digests and the game ID and revision they are for: no name, no size
and no other byte of the disc. --check compares the image's set with the output instead of writing
it, and --replace writes over a set of the image's revision that is there already.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "release"))
from installer.disc import CHUNK, DiscImage  # noqa: E402

DEFAULT_OUTPUT = (ROOT / "src" / "launcher" / "crates" / "gdl-install" / "fingerprints"
                  / "gamecube-us.json")

# What the output is: its format, the version of that format, and the disc it fingerprints.
FORMAT = "gdl-gamecube-fingerprints"
FORMAT_VERSION = 1
GAME_ID = "GUNE5D"


class Refused(Exception):
    """An image the fingerprints cannot be taken from, or an output they cannot be written to."""


def file_digest(disc: DiscImage, offset: int, size: int) -> str:
    """The SHA-256 of `size` bytes of the disc from `offset`, read a chunk at a time."""
    digest = hashlib.sha256()
    done = 0
    while done < size:
        count = min(CHUNK, size - done)
        digest.update(disc.read(offset + done, count))
        done += count
    return digest.hexdigest()


def fingerprints(image: Path) -> tuple[int, list[dict[str, str]]]:
    """The revision of the disc in `image` and the fingerprint of every file an install copies
    from it, in the order of their path digests."""
    try:
        with DiscImage(image) as disc:
            rows = [{"pathSha256": hashlib.sha256(entry.path.as_posix().encode("utf-8")).hexdigest(),
                     "sha256": file_digest(disc, entry.offset, entry.size)}
                    for entry in disc.entries if not entry.directory]
            return disc.revision, sorted(rows, key=lambda row: row["pathSha256"])
    except (OSError, ValueError) as error:
        raise Refused(f"{image}: {error}") from error


def written(output: Path) -> dict:
    """What `output` holds, or an empty set of revisions when it is not there."""
    try:
        text = output.read_text(encoding="utf-8")
    except FileNotFoundError:
        return {"format": FORMAT, "version": FORMAT_VERSION, "gameId": GAME_ID, "revisions": []}
    except OSError as error:
        raise Refused(f"{output}: {error.strerror or error}") from error
    try:
        held = json.loads(text)
    except ValueError as error:
        raise Refused(f"{output} is not JSON: {error}") from error
    if (not isinstance(held, dict) or held.get("format") != FORMAT
            or held.get("version") != FORMAT_VERSION or held.get("gameId") != GAME_ID
            or not isinstance(held.get("revisions"), list)):
        raise Refused(f"{output} is not of version {FORMAT_VERSION} of the format {FORMAT}")
    return held


def write(output: Path, held: dict) -> None:
    """Writes `held` to `output` through a file beside it, so that a failed write leaves the old
    output whole."""
    text = json.dumps(held, indent=2) + "\n"
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        handle, name = tempfile.mkstemp(dir=output.parent, prefix=output.name + ".")
        try:
            with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as stream:
                stream.write(text)
            os.replace(name, output)
        except BaseException:
            Path(name).unlink(missing_ok=True)
            raise
    except OSError as error:
        raise Refused(f"{output}: {error.strerror or error}") from error


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("image", type=Path, help="the trusted disc image, an ISO or a CISO")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="compare with the output instead of writing it")
    mode.add_argument("--replace", action="store_true",
                      help="write over a set of the image's revision that is there already")
    args = parser.parse_args(argv)
    try:
        revision, rows = fingerprints(args.image)
        held = written(args.output)
        sets = [one for one in held["revisions"]
                if isinstance(one, dict) and one.get("revision") == revision]
        if args.check:
            if not sets or sets[0].get("files") != rows:
                raise Refused(f"{args.output} does not hold these fingerprints of revision {revision}")
            print(f"Revision {revision}: {len(rows)} fingerprints match {args.output}")
            return 0
        if sets and not args.replace:
            raise Refused(f"{args.output} already holds revision {revision}; pass --replace")
        others = [one for one in held["revisions"] if one not in sets]
        held["revisions"] = sorted(others + [{"revision": revision, "files": rows}],
                                   key=lambda one: one["revision"])
        write(args.output, held)
        print(f"Revision {revision}: wrote {len(rows)} fingerprints to {args.output}")
        return 0
    except Refused as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
