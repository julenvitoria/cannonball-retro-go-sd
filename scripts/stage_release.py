#!/usr/bin/env python3
"""Stage Retro-Go SD release assets for the active project kind.

Reads PROJECT_KIND and PACKED_BIN from the root Makefile, builds:
  1. SD install zip:   <stem>-<tag>.zip       → homebrews|cores/<packed.bin>
                                               (+ any SIDECARS)
  2. Debug symbols zip: <stem>-<tag>-debug.zip → ELF, map, README

Extracts release notes from CHANGELOG.md for the requested tag.

Usage:
  python3 scripts/stage_release.py --bin example.bin --tag v1.0.0 --out release \\
      --elf build/core/example_core.elf --map build/core/example_core.map
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"
DEFAULT_CHANGELOG = ROOT / "CHANGELOG.md"
DEBUG_README = ROOT / "scripts" / "DEBUG_README.md"

MAKE_VARS = (
    "PROJECT_KIND",
    "PACKED_BIN",
    "SIDECARS",
    "RO_BIN",
    "CORE_NAME",
    "DOCKER_IMAGE",
    "TARGET_ELF",
    "TARGET_MAP",
)

# Read separately, and tolerated when absent. MAKE_VARS is positional and
# strict: a project whose Makefile lacks one of those targets fails outright,
# which is right for a variable every project must define. A late optional
# addition cannot use that path without breaking every Makefile that has not
# been updated yet, and this file is vendored verbatim into every project.
# REDIST_DOCS: files a licence obliges this project to pass on with the binary,
# space separated. They are not device files -- they go in the install zip beside
# the SD folder and are attached to the release, so whoever redistributes the
# thing has the terms in hand.
OPTIONAL_MAKE_VARS = ("COVER_FULL", "REDIST_DOCS")

HEADING_RE = re.compile(
    r"^##\s*(?:\[(?P<bracket>[^\]]+)\]|(?P<plain>[^\s#]+))(?:\s*-\s*(?P<date>.+))?\s*$",
    re.MULTILINE,
)


def read_make_vars() -> dict[str, str]:
    cmd = ["make", "-f", str(MAKEFILE), "--no-print-directory"]
    for var in MAKE_VARS:
        cmd.append(f"print-{var}")

    try:
        # Keep stderr separate — Makefile $(warning) lines must not pollute values.
        proc = subprocess.run(
            cmd,
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
    except OSError as exc:
        raise SystemExit(f"failed to run make: {exc}") from exc

    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or proc.stdout or "")
        raise SystemExit(f"failed to read Makefile variables from {MAKEFILE}")

    if proc.stderr:
        sys.stderr.write(proc.stderr)

    # Positional, and blank lines are kept: a variable a project does not set
    # -- SIDECARS in most projects -- prints as an empty line, and dropping it
    # would shift every value after it onto the wrong name.
    values = proc.stdout.splitlines()
    # Defensive: if anything else leaked to stdout, keep the last N lines.
    if len(values) > len(MAKE_VARS):
        values = values[-len(MAKE_VARS) :]
    if len(values) != len(MAKE_VARS):
        raise SystemExit(
            f"expected {len(MAKE_VARS)} Makefile values, got {len(values)}:\n{proc.stdout}"
        )
    cfg = dict(zip(MAKE_VARS, values, strict=True))

    for var in OPTIONAL_MAKE_VARS:
        proc = subprocess.run(
            ["make", "-f", str(MAKEFILE), "--no-print-directory", f"print-{var}"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        # A Makefile without the target is not an error: the project simply
        # does not have the thing.
        if proc.returncode == 0:
            lines = proc.stdout.splitlines()
            cfg[var] = lines[-1] if lines else ""

    return cfg


def resolve_sidecars(cfg: dict[str, str], explicit: list[str] | None) -> list[str]:
    """Extra device files installed beside the packed binary, in declared order.

    SIDECARS is a space-separated list. RO_BIN is the older single-slot spelling
    and still works: it was named for Zelda 3's .rodata dump, but gba puts an
    execute-in-place blob through it, PICO-8 needs two files and a Quake port
    needs one per level -- so the name was wrong and one slot was not enough.
    """
    if explicit:
        return list(explicit)
    names = (cfg.get("SIDECARS") or "").split()
    if names:
        return names
    legacy = (cfg.get("RO_BIN") or "").strip()
    return [legacy] if legacy else []


def sd_subdir(project_kind: str) -> str:
    if project_kind == "core":
        return "cores"
    if project_kind == "homebrew":
        return "homebrews"
    raise SystemExit(f"unsupported PROJECT_KIND: {project_kind!r} (expected core or homebrew)")


def slug(tag: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "-", tag).strip("-") or "release"


def extract_changelog_section(changelog_path: Path, tag: str) -> str:
    if not changelog_path.is_file():
        raise SystemExit(f"CHANGELOG not found: {changelog_path}")

    text = changelog_path.read_text(encoding="utf-8")
    matches = list(HEADING_RE.finditer(text))
    for index, match in enumerate(matches):
        version = (match.group("bracket") or match.group("plain") or "").strip()
        if version != tag:
            continue
        start = match.end()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[start:end].strip()
        if not body:
            raise SystemExit(f"CHANGELOG section for {tag!r} is empty")
        return body

    raise SystemExit(
        "no CHANGELOG section for tag "
        f"{tag!r}; add '## [{tag}] - YYYY-MM-DD' before pushing the tag"
    )


def build_release_notes(
    *,
    tag: str,
    changelog_body: str,
    project_kind: str,
    packed_name: str,
    sd_dir: str,
    core_name: str,
    docker_image: str,
    archive_name: str,
    debug_archive_name: str,
    sidecar_names: list[str] | None = None,
) -> str:
    sdk_version = (ROOT / "SDK_VERSION").read_text(encoding="utf-8").strip()
    install_path = f"/{sd_dir}/{packed_name}"

    lines = [
        f"# {tag}",
        "",
        changelog_body,
        "",
        "---",
        "",
        f"- Project kind: `{project_kind}`",
        f"- Packed binary: `{packed_name}`",
        f"- SD install path: `{install_path}`",
    ]
    # Named, not described: this used to say "Rodata sidecar" for every file,
    # which was a lie the moment gba put a .xip through the same slot.
    for name in sidecar_names or []:
        lines.append(f"- Also installed: `/{sd_dir}/{name}` (included in the install zip)")
    lines.extend(
        [
            f"- Release archive: `{archive_name}` (unzip onto the SD root)",
            f"- Debug archive: `{debug_archive_name}` (ELF + linker map)",
            f"- Built with: `{docker_image}`",
            "",
            "Crash PC/LR → source (needs `arm-none-eabi-addr2line`):",
            "",
            "```bash",
            f"unzip {debug_archive_name}",
            "arm-none-eabi-addr2line -e <name>_core.elf -f -C -a 0x<PC> 0x<LR>",
            "```",
            "",
            "Or from a checkout of this repo: `python3 scripts/resolve_addr.py --elf …`",
            "",
            "```",
            sdk_version,
            "```",
        ]
    )
    if project_kind == "core":
        lines.append(f"- Test ROMs: `/roms/{core_name}/`")
    else:
        stem = Path(packed_name).stem
        lines.append(f"- Optional cover: `/covers/homebrew/{stem}.img`")
        lines.append(
            "- Game assets (`zelda3_assets.dat`) are not in this zip — extract from a "
            "ROM (see README) and place under `/homebrews/`."
        )

    return "\n".join(lines) + "\n"


def write_zip(archive_path: Path, members: list[tuple[Path, str]]) -> None:
    if archive_path.exists():
        archive_path.unlink()
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for src, arcname in members:
            zf.write(src, arcname)


def stage_release(
    *,
    bin_path: Path,
    tag: str,
    out_dir: Path,
    changelog_path: Path,
    docker_image: str | None,
    elf_path: Path | None,
    map_path: Path | None,
    sidecar_paths: list[Path] | None,
) -> None:
    cfg = read_make_vars()
    project_kind = cfg["PROJECT_KIND"]
    packed_name = cfg["PACKED_BIN"]
    sidecar_names = resolve_sidecars(cfg, [p.name for p in sidecar_paths or []])
    core_name = cfg["CORE_NAME"]
    resolved_docker = docker_image or cfg.get("DOCKER_IMAGE") or "sylverb/retro-go-sd-builder:v1.5"

    if not bin_path.is_file():
        raise SystemExit(f"packed binary not found: {bin_path}")

    elf = elf_path or (ROOT / cfg["TARGET_ELF"])
    if not elf.is_absolute():
        elf = ROOT / elf
    map_file = map_path or (ROOT / cfg["TARGET_MAP"])
    if not map_file.is_absolute():
        map_file = ROOT / map_file

    if not elf.is_file():
        raise SystemExit(f"ELF not found: {elf}")
    if not map_file.is_file():
        raise SystemExit(f"linker map not found: {map_file}")
    if not DEBUG_README.is_file():
        raise SystemExit(f"debug readme not found: {DEBUG_README}")

    explicit = {p.name: p for p in sidecar_paths or []}
    resolved_sidecars: list[tuple[str, Path]] = []
    for name in sidecar_names:
        src = explicit.get(name) or (ROOT / name)
        if not src.is_absolute():
            src = ROOT / src
        if not src.is_file():
            raise SystemExit(f"sidecar {name!r} not found: {src}")
        resolved_sidecars.append((name, src))

    # Full-size box art, when the project keeps any. Not a device file -- it is
    # never installed and never enters the SD zip -- but the manifest declares
    # it, so the mirror has to be able to fetch it loose off the release.
    cover = (cfg.get("COVER_FULL") or "").strip()
    cover_src: Path | None = None
    if cover:
        cover_src = Path(cover)
        if not cover_src.is_absolute():
            cover_src = ROOT / cover_src
        if not cover_src.is_file():
            raise SystemExit(f"COVER_FULL not found: {cover_src}")

    changelog_body = extract_changelog_section(changelog_path, tag)

    sd_dir = sd_subdir(project_kind)
    out_dir.mkdir(parents=True, exist_ok=True)
    sd_root = out_dir / sd_dir
    sd_root.mkdir(parents=True, exist_ok=True)

    sd_bin = sd_root / packed_name
    shutil.copy2(bin_path, sd_bin)

    # The loose device files, attached alongside the zips. The dist mirror
    # fetches every file the manifest names straight off the release, so each
    # one has to be there as a file and not only as a zip member. GitHub
    # rewrites the spaces in an asset name; build_dist.py restores the declared
    # name.
    flat_files = [out_dir / packed_name]
    shutil.copy2(bin_path, flat_files[0])

    zip_members: list[tuple[Path, str]] = [(sd_bin, f"{sd_dir}/{packed_name}")]
    for name, src in resolved_sidecars:
        staged = sd_root / name
        shutil.copy2(src, staged)
        zip_members.append((staged, f"{sd_dir}/{name}"))
        # Each sidecar is an artifact in its own right: the manifest declares it
        # with its own hash, so the mirror must be able to fetch it loose too.
        flat = out_dir / name
        shutil.copy2(src, flat)
        flat_files.append(flat)

    # Licence and attribution documents that must travel with the binary. Named
    # after the binary so two projects' files cannot collide on a card, and put
    # at the zip root rather than in the SD folder: the device never reads them.
    for doc in (cfg.get("REDIST_DOCS") or "").split():
        doc_src = (ROOT / doc).resolve()
        if not doc_src.is_file():
            raise SystemExit(f"REDIST_DOCS not found: {doc_src}")
        arcname = f"{Path(packed_name).stem}-{doc_src.name}"
        staged_doc = out_dir / arcname
        shutil.copy2(doc_src, staged_doc)
        zip_members.append((staged_doc, arcname))
        flat_files.append(staged_doc)

    cover_path: Path | None = None
    if cover_src is not None:
        cover_path = out_dir / cover_src.name
        shutil.copy2(cover_src, cover_path)

    stem = Path(packed_name).stem
    tag_slug = slug(tag)

    archive_name = f"{stem}-{tag_slug}.zip"
    archive_path = out_dir / archive_name
    write_zip(archive_path, zip_members)

    debug_archive_name = f"{stem}-{tag_slug}-debug.zip"
    debug_archive_path = out_dir / debug_archive_name
    write_zip(
        debug_archive_path,
        [
            (elf, elf.name),
            (map_file, map_file.name),
            (DEBUG_README, "README.md"),
        ],
    )

    notes_path = out_dir / "RELEASE_NOTES.md"
    notes_path.write_text(
        build_release_notes(
            tag=tag,
            changelog_body=changelog_body,
            project_kind=project_kind,
            packed_name=packed_name,
            sd_dir=sd_dir,
            core_name=core_name,
            docker_image=resolved_docker,
            archive_name=archive_name,
            debug_archive_name=debug_archive_name,
            sidecar_names=sidecar_names,
        ),
        encoding="utf-8",
    )

    # GitHub Release assets: the loose files the manifest declares, plus the
    # install and debug zips for people who install by hand.
    release_files = [*flat_files, archive_path, debug_archive_path]
    if cover_path is not None:
        release_files.append(cover_path)
    files_path = out_dir / "release-files.txt"
    files_path.write_text(
        "\n".join(p.name for p in release_files) + "\n",
        encoding="utf-8",
    )

    print(f"release_title={tag}")
    print(f"project_kind={project_kind}")
    print(f"packed_bin={packed_name}")
    print(f"sd_path=/{sd_dir}/{packed_name}")
    if sidecar_names:
        print(f"sidecars={' '.join(sidecar_names)}")
        # Kept for callers written against the single-slot spelling.
        if len(sidecar_names) == 1:
            print(f"ro_bin={sidecar_names[0]}")
            print(f"ro_path=/{sd_dir}/{sidecar_names[0]}")
    if cover_path is not None:
        print(f"cover={cover_path}")
    print(f"archive={archive_path}")
    print(f"debug_archive={debug_archive_path}")
    print(f"notes={notes_path}")
    print(f"files={files_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bin",
        dest="bin_path",
        type=Path,
        help="packed .bin to stage (default: PACKED_BIN from Makefile, relative to repo root)",
    )
    parser.add_argument(
        "--elf",
        dest="elf_path",
        type=Path,
        help="linked ELF with debug symbols (default: TARGET_ELF from Makefile)",
    )
    parser.add_argument(
        "--map",
        dest="map_path",
        type=Path,
        help="linker map (default: TARGET_MAP from Makefile)",
    )
    parser.add_argument(
        "--sidecar",
        dest="sidecar_paths",
        type=Path,
        action="append",
        help="extra device file to install beside the binary; repeatable "
             "(default: SIDECARS from the Makefile)",
    )
    parser.add_argument(
        "--ro",
        dest="sidecar_paths",
        type=Path,
        action="append",
        help=argparse.SUPPRESS,  # deprecated spelling of --sidecar
    )
    parser.add_argument("--tag", required=True, help="release tag (e.g. v1.0.0)")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("release"),
        help="output directory (default: release/)",
    )
    parser.add_argument(
        "--changelog",
        type=Path,
        default=DEFAULT_CHANGELOG,
        help="changelog file (default: CHANGELOG.md)",
    )
    parser.add_argument(
        "--docker-image",
        help="builder image string for release notes (default: Makefile DOCKER_IMAGE)",
    )
    args = parser.parse_args()

    cfg = read_make_vars()
    bin_path = args.bin_path or (ROOT / cfg["PACKED_BIN"])
    if not bin_path.is_absolute():
        bin_path = ROOT / bin_path

    changelog_path = args.changelog
    if not changelog_path.is_absolute():
        changelog_path = ROOT / changelog_path

    stage_release(
        bin_path=bin_path,
        tag=args.tag,
        out_dir=(ROOT / args.out).resolve() if not args.out.is_absolute() else args.out,
        changelog_path=changelog_path,
        docker_image=args.docker_image,
        elf_path=args.elf_path,
        map_path=args.map_path,
        sidecar_paths=args.sidecar_paths,
    )


if __name__ == "__main__":
    main()
