#!/usr/bin/env python3
"""Zip one release into an offline bundle.

A bundle is the manifest plus the files it names, both at the zip root, so an
installer resolves the manifest's relative urls against zip entries exactly as
it resolves them against a website. See spec/06-bundle.md in
https://github.com/slash-proc/gwrg-dist-spec

Usage:
  python3 scripts/make_bundle.py --manifest release/manifest.json --dir release \
      --out release/minesweeper-v0.1.2-bundle.zip
"""

from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path


def declared_files(manifest: dict) -> list[str]:
    """Every file the manifest names, in a stable order.

    symbols[] included: a bundle is the archival copy of a release, and a crash
    in a project that has since disappeared is exactly when its symbols matter
    most. Being in the bundle does not put it on the card -- it is published,
    not installed.
    """
    names = []
    for target in manifest["targets"]:
        for artifact in target["artifacts"]:
            names.append(artifact["url"])
        for sym in target.get("symbols", []):
            names.append(sym["url"])
        # A BIOS the project ships is published like an artifact; one the user
        # supplies has no url and nothing to fetch.
        for system in target.get("systems", []):
            for b in system.get("bios", []):
                if b.get("url"):
                    names.append(b["url"])
            # A game the project ships is published like a BIOS it ships, and
            # a bundle without it is a core with nothing to run.
            for g in system.get("games", []):
                if g.get("url"):
                    names.append(g["url"])
    if manifest.get("cover"):
        names.append(manifest["cover"]["url"])
    for tool in manifest["tools"]:
        names.append(tool["binary"]["url"])
    return sorted(set(names))


def expected_sizes(manifest: dict) -> dict[str, tuple[int, str]]:
    sizes = {}
    for target in manifest["targets"]:
        for artifact in target["artifacts"]:
            sizes[artifact["url"]] = (artifact["bytes"], artifact["sha256"])
        for sym in target.get("symbols", []):
            sizes[sym["url"]] = (sym["bytes"], sym["sha256"])
        for system in target.get("systems", []):
            for b in system.get("bios", []):
                if b.get("url"):
                    sizes[b["url"]] = (b["bytes"], b["sha256"])
            for g in system.get("games", []):
                if g.get("url"):
                    sizes[g["url"]] = (g["bytes"], g["sha256"])
    if manifest.get("cover"):
        cover = manifest["cover"]
        sizes[cover["url"]] = (cover["bytes"], cover["sha256"])
    for tool in manifest["tools"]:
        sizes[tool["binary"]["url"]] = (tool["binary"]["bytes"], tool["binary"]["sha256"])
    return sizes


def bundle_name(manifest: dict) -> str:
    return f"{manifest['project']}-{manifest['source']['ref']}-bundle.zip"


def build(*, manifest_path: Path, src_dir: Path, out: Path) -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    sizes = expected_sizes(manifest)

    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.write(manifest_path, "manifest.json")
        for name in declared_files(manifest):
            source = src_dir / name
            if not source.is_file():
                raise SystemExit(f"manifest names {name!r}, which is not in {src_dir}")

            # A bundle that disagrees with its own manifest is worse than none:
            # it fails at install time, on someone else's machine.
            data = source.read_bytes()
            want_bytes, want_hash = sizes[name]
            if len(data) != want_bytes:
                raise SystemExit(f"{name}: manifest says {want_bytes} bytes, file is {len(data)}")
            got_hash = hashlib.sha256(data).hexdigest()
            if got_hash != want_hash:
                raise SystemExit(f"{name}: sha256 does not match the manifest")

            zf.write(source, name)

    print(f"make_bundle: wrote {out} ({out.stat().st_size} bytes)")
    print(f"  manifest.json + {', '.join(declared_files(manifest))}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest", type=Path, required=True)
    ap.add_argument("--dir", dest="src_dir", type=Path, required=True,
                    help="directory holding the files the manifest names")
    ap.add_argument("--out", type=Path, help="output zip (default: <project>-<ref>.zip in --dir)")
    ap.add_argument("--print-name", action="store_true",
                    help="print the conventional bundle name and exit")
    args = ap.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if args.print_name:
        print(bundle_name(manifest))
        return

    out = args.out or (args.src_dir / bundle_name(manifest))
    build(manifest_path=args.manifest, src_dir=args.src_dir, out=out)


if __name__ == "__main__":
    main()
