#!/usr/bin/env python3
"""Assemble the dist/ tree that GitHub Pages serves.

Releases are the source of truth. This rebuilds the whole tree from them every
time, so the site is a derived view that can be deleted and regenerated rather
than state edited in place.

Browsers cannot fetch GitHub release assets cross-origin, which is the entire
reason this mirror exists. `gh` runs server-side in CI, where that restriction
does not apply.

Usage:
  python3 scripts/build_dist.py --repo owner/name --out _site/dist --retain 5
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SCHEMA_VERSION = 1


def gh_json(args: list[str]) -> object:
    out = subprocess.check_output(["gh", *args], text=True)
    return json.loads(out)


def list_releases(repo: str, require_tag: str | None = None, attempts: int = 6) -> list[dict]:
    """Newest first. Drafts are skipped; they are not published.

    A release created moments ago may not be listed yet, so this retries until
    it appears. `require_tag` is the tag currently being published: without it
    an empty answer is indistinguishable from a project that genuinely has no
    releases.
    """
    for attempt in range(1, attempts + 1):
        raw = gh_json(
            ["release", "list", "--repo", repo, "--limit", "100",
             "--json", "tagName,publishedAt,isPrerelease,isDraft"]
        )
        rels = [r for r in raw if not r["isDraft"] and r.get("publishedAt")]
        rels.sort(key=lambda r: r["publishedAt"], reverse=True)

        have = {r["tagName"] for r in rels}
        if rels and (require_tag is None or require_tag in have):
            return rels

        if attempt == attempts:
            print(
                f"gh returned {len(raw)} release(s); "
                f"{len(rels)} published: {sorted(have) or 'none'}",
                file=sys.stderr,
            )
            return rels

        missing = f"{require_tag!r} not listed yet" if require_tag else "no releases listed yet"
        delay = attempt * 5
        print(f"{missing}; retrying in {delay}s", file=sys.stderr)
        time.sleep(delay)

    return []


def asset_name(filename: str) -> str:
    """What GitHub calls a file once it is a release asset.

    GitHub rewrites characters it will not put in an asset name -- a space
    becomes a dot -- so "Super Mario World.bin" is attached, and must be
    downloaded, as "Super.Mario.World.bin". The mirror stores it under the name
    the manifest declares, because that is the name the device wants and the
    name an installer resolves. The rewrite is an artifact of the archival
    copy, not something the manifest should have to know about.
    """
    return re.sub(r"[^A-Za-z0-9._-]", ".", filename)


def fetch_asset(repo: str, tag: str, name: str, dest: Path, *, save_as: str | None = None) -> bool:
    """Download one release asset. False when the release does not carry it.

    `name` is the file as the manifest names it; the asset is fetched under
    whatever GitHub renamed it to and then restored to `save_as` (default
    `name`), so the mirror matches the manifest rather than the upload.
    """
    remote = asset_name(name)
    try:
        subprocess.run(
            ["gh", "release", "download", tag, "--repo", repo,
             "--pattern", remote, "--dir", str(dest), "--clobber"],
            check=True, capture_output=True, text=True,
        )
    except subprocess.CalledProcessError:
        return False

    want = save_as or name
    if remote != want:
        (dest / remote).replace(dest / want)
    return True


def download(repo: str, tag: str, dest: Path) -> bool:
    """Fetch a release's manifest and every file it declares. False if it has none."""
    dest.mkdir(parents=True, exist_ok=True)
    if not fetch_asset(repo, tag, "manifest.json", dest):
        return False

    manifest_path = dest / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    for name in declared_files(manifest):
        if "/" in name or name.startswith("."):
            raise SystemExit(f"{tag}: manifest declares a suspicious url {name!r}")
        if not fetch_asset(repo, tag, name, dest):
            raise SystemExit(f"{tag}: manifest names {name!r}, which the release does not carry")
    return True


def declared_files(manifest: dict) -> list[str]:
    """Every file the manifest names, in a stable order.

    Including the ELF in symbols[], which is published but never installed. The
    mirror still has to carry it: the manifest names it, the spec's checker
    verifies it is reachable, and an offline bundle is this directory zipped --
    so a bundle without it cannot symbolicate a crash.
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
            # A game this project ships is published exactly like a BIOS it
            # ships: named by the manifest, fetched off the release, mirrored.
            for g in system.get("games", []):
                if g.get("url"):
                    names.append(g["url"])
    if manifest.get("cover"):
        names.append(manifest["cover"]["url"])
    for tool in manifest["tools"]:
        names.append(tool["binary"]["url"])
    return sorted(set(names))


def manifest_problem(manifest: dict) -> str | None:
    """Why this manifest cannot be published today, or None if it can.

    Checks the fields the spec's schema constrains and that a past revision
    might have allowed. Not a full validator: it exists so an old release
    drops out of the mirror instead of failing the conformance run.
    """
    for target in manifest.get("targets", []):
        for artifact in target.get("artifacts", []):
            extra = set(artifact) - {
                "filename", "bytes", "sha256", "url", "mapped", "relocBase",
            }
            if extra:
                return f"artifact has fields the spec removed: {', '.join(sorted(extra))}"
    for tool in manifest.get("tools", []):
        for output in tool.get("outputs", []):
            extra = set(output) - {
                "id", "filename", "extension", "subdir", "maxBytes", "label",
                "description",
            }
            if extra:
                return f"output has fields the spec removed: {', '.join(sorted(extra))}"
        for inp in tool.get("inputs", []):
            extra = set(inp) - {
                "id", "required", "allowMultiple", "runPerFile", "maxCount",
                "label", "description", "extensions", "maxBytes", "variants",
                "strict",
            }
            if extra:
                return f"input has fields the spec removed: {', '.join(sorted(extra))}"
    return None


def index_entry(tag: str, release: dict, manifest: dict, bundle: str | None) -> dict:
    targets = manifest["targets"]
    # Duplicated into the index so a version picker needs one fetch, not N+1.
    # The checker verifies these against the manifest they came from.
    # "You must supply something" -- whatever the something is. A converter
    # input, or a BIOS a core cannot run without: most cores ship tools: [],
    # so counting only inputs would tell a picker that PC Engine CD needs
    # nothing when it will not start without a System Card.
    #
    # A BIOS the project publishes itself carries a url and is installed from
    # the release like any other file, so it asks nothing of the user. Counting
    # it would make every MSX install warn about the eleven ROMs it is about to
    # install for you.
    # A tool the target does not require cannot make the user fetch anything:
    # uses[].required is defined as "false if the install works without it",
    # which is exactly a project that ships a game and offers the converter for
    # the user's own. inputs[].required is a different question -- can the tool
    # run at all -- and answering this one with it conflated the two.
    tools_by_id = {t["id"]: t for t in manifest["tools"]}
    needs_user_files = any(
        i["required"]
        for target in manifest["targets"]
        for use in target.get("uses", [])
        if use.get("required")
        for i in tools_by_id.get(use["tool"], {}).get("inputs", [])
    ) or any(
        not b.get("url") and (b.get("required") or b.get("requiredFor"))
        for target in manifest["targets"]
        for system in target.get("systems", [])
        for b in system.get("bios", [])
    )
    return {
        "tag": tag,
        "manifest": f"{tag}/manifest.json",
        "publishedAt": release["publishedAt"],
        "prerelease": bool(release["isPrerelease"]),
        "kind": targets[0]["kind"],
        "requiresAbi": dict(targets[0]["requiresAbi"]),
        "needsUserFiles": needs_user_files,
        **({"bundle": bundle} if bundle else {}),
    }


def build(*, repo: str, out: Path, retain: int, bundles: bool = True,
          require_tag: str | None = None) -> None:
    releases = list_releases(repo, require_tag)
    if not releases:
        raise SystemExit(f"{repo} has no published releases")
    if require_tag and require_tag not in {r["tagName"] for r in releases}:
        raise SystemExit(f"{repo} does not list {require_tag}, the tag being published")

    staging = Path(tempfile.mkdtemp(prefix="gwrg-dist-"))
    versions: list[dict] = []
    project = title = None

    for release in releases:
        if len(versions) >= retain:
            break
        tag = release["tagName"]
        dest = staging / tag
        if not download(repo, tag, dest):
            print(f"skip {tag}: no manifest.json attached", file=sys.stderr)
            shutil.rmtree(dest, ignore_errors=True)
            continue

        manifest = json.loads((dest / "manifest.json").read_text(encoding="utf-8"))
        if manifest["schemaVersion"] != SCHEMA_VERSION:
            print(f"skip {tag}: schemaVersion {manifest['schemaVersion']}", file=sys.stderr)
            shutil.rmtree(dest, ignore_errors=True)
            continue
        # A retained release was published against whatever the spec said at the
        # time. If a later revision made its manifest invalid, mirroring it
        # anyway makes the whole project non-conformant over one old version
        # nobody is installing. Leave it in the releases, out of the mirror.
        problem = manifest_problem(manifest)
        if problem is not None:
            print(f"skip {tag}: {problem}", file=sys.stderr)
            shutil.rmtree(dest, ignore_errors=True)
            continue
        if manifest["source"]["ref"] != tag:
            raise SystemExit(f"{tag}: manifest says it was built for {manifest['source']['ref']!r}")

        # The newest release settles the project identity; older ones must agree.
        if project is None:
            project, title = manifest["project"], manifest["title"]
        elif manifest["project"] != project:
            raise SystemExit(f"{tag}: project {manifest['project']!r} != {project!r}")

        versions.append((tag, release, manifest, dest))
        print(f"include {tag}")

    if not versions:
        raise SystemExit("no release carries a manifest.json — nothing to publish")

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    entries = []
    for tag, release, manifest, dest in versions:
        shutil.copytree(dest, out / tag)

        # The bundle is a release asset like any other; the mirror never builds
        # one, so what a user downloads offline is what the project published.
        bundle = None
        if bundles:
            name = f"{manifest['project']}-{tag}-bundle.zip"
            if fetch_asset(repo, tag, name, out):
                bundle = name
            else:
                print(f"note {tag}: no offline bundle attached to the release", file=sys.stderr)

        entries.append(index_entry(tag, release, manifest, bundle))

    index = {
        "schemaVersion": SCHEMA_VERSION,
        "project": project,
        "title": title,
        "repo": repo,
        "releasesUrl": f"https://github.com/{repo}/releases",
        "retained": retain,
        "versions": entries,
    }

    (out / "versions.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    shutil.rmtree(staging, ignore_errors=True)

    print(f"\nbuild_dist: wrote {out} with {len(entries)} version(s), latest {entries[0]['tag']}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repo", required=True, help="owner/name")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--retain", type=int, default=5, help="how many versions to keep (default 5)")
    ap.add_argument(
        "--no-bundles",
        action="store_true",
        help="do not mirror the per-version offline bundle",
    )
    ap.add_argument(
        "--require-tag",
        help="wait for this tag to appear in the release list before building",
    )
    args = ap.parse_args()
    build(repo=args.repo, out=args.out, retain=args.retain,
          bundles=not args.no_bundles, require_tag=args.require_tag)


if __name__ == "__main__":
    main()
