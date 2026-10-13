#!/usr/bin/env python3
"""Emit the dist manifest for one release.

Describes this release per the GWRG distribution spec
(https://github.com/slash-proc/gwrg-dist-spec). One script serves every
project -- homebrew and core alike -- and it contains no facts about
any of them. It is meant to be vendored unchanged; a copy that had to be
edited on the way in is a copy that drifts.

Everything that can be derived is derived, from the built bytes:

  homebrew  the "GWHB" envelope and gwhb_meta_t  -> display name, firmware ABI
  core      the "CORE" envelope and gnw_core_meta_t -> core name, ABI, systems[]

What cannot be derived lives in gwrg.json at the repo root: the converter
description for a project that ships one, the per-system extras a core binary
does not carry (shortName, compression, BIOS, extension grouping), and any
artifact that has to be placed at a real address rather than in a filesystem
("artifacts": {"<filename>": {"mapped": true, "relocBase": "0xDEC00000"}}).
Where the two overlap they are cross-checked, and a disagreement is fatal --
the binary wins arguments, gwrg.json only adds what the binary cannot say.

A project-specific invariant belongs in that project's own tests, not here:
this file is vendored by every repo, so a check that means something to only
one of them is dead weight in the other eight.

Usage:
  python3 scripts/make_manifest.py --bin "Zelda 3.bin" --artifact zelda3.ro \\
      --wasm tools/extractor/target/.../zelda3_restool.wasm \\
      --elf build/homebrew/zelda3_core.elf \\
      --tag v1.0.0 --repo slash-proc/zelda3-retro-go-sd --commit "$GITHUB_SHA" \\
      --out release/manifest.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"
DECLARED = ROOT / "gwrg.json"
EXTRACTOR = ROOT / "tools" / "extractor"

SCHEMA_VERSION = 1

TARGET = {
    "id": "gnw-retro-go",
    "platform": "game-and-watch",
    "label": "Game & Watch (Retro-Go SD)",
}

# --- the two envelopes -------------------------------------------------------
#
# Both start magic(4) + header_version(u16) + header_length(u16), then a struct.
# The layouts mirror the SDK headers exactly and the asserts below are the only
# thing standing between a silent misparse and a wrong manifest.

GWHB_MAGIC = b"GWHB"
# gwhb_meta_t, from sdk/include/Core/Inc/retro-go/gwhb.h. Exactly 96 bytes.
# Two GWHB metadata layouts exist, and both call themselves version 1.
#
# The original is one code_size/bss_size pair. The current one replaces that
# with the same multi-segment model CORE uses (segments_count + segments[4],
# 12 bytes each) and shrinks reserved[32] to reserved[16]. GWHB_META_VERSION
# was not bumped when that happened, so the version field cannot tell them
# apart and this file has to work it out from the sizes -- see pick_gwhb().
#
# Both are supported deliberately: homebrew that predates the change is still
# installed, still published, and must keep producing a correct manifest.
GWHB_V1_FORMAT = "<7I32s4B32s"
GWHB_V1_SIZE = struct.calcsize(GWHB_V1_FORMAT)
GWHB_SEGMENTS = 4
GWHB_V2_FORMAT = "<4I" + ("III" * GWHB_SEGMENTS) + "2I32s4B16s"
GWHB_V2_SIZE = struct.calcsize(GWHB_V2_FORMAT)
# Kept for the length checks shared with CORE; the smaller of the two is the
# least a file must carry to be a GWHB binary at all.
GWHB_FORMAT = GWHB_V1_FORMAT
GWHB_SIZE = GWHB_V1_SIZE
assert GWHB_V1_SIZE == 96, GWHB_V1_SIZE
assert GWHB_V2_SIZE == 124, GWHB_V2_SIZE

CORE_MAGIC = b"CORE"
# gnw_core_segment_t / gnw_core_system_t / gnw_core_meta_t, from
# sdk/include/Core/Inc/retro-go/gnw_core_meta.h, mirrored by sdk/tools/pack_core.py.
CORE_MAX_SEGMENTS = 4
CORE_MAX_SYSTEMS = 4
SEGMENT_FORMAT = "<III"
SEGMENT_SIZE = struct.calcsize(SEGMENT_FORMAT)
SYSTEM_FORMAT = "<32s16s32sIIIII8s8s"
SYSTEM_SIZE = struct.calcsize(SYSTEM_FORMAT)
assert SEGMENT_SIZE == 12 and SYSTEM_SIZE == 116, (SEGMENT_SIZE, SYSTEM_SIZE)
CORE_META_SIZE = (4 * 4 + CORE_MAX_SEGMENTS * SEGMENT_SIZE
                  + 4 + CORE_MAX_SYSTEMS * SYSTEM_SIZE
                  + 3 + 24 + 5)
assert CORE_META_SIZE == 564, CORE_META_SIZE

# gnw_parse_type_t. The firmware calls these rom/cdrom because it is thinking
# about media; the manifest calls them file/directory because an installer is
# thinking about whether a folder is one game or many.
BROWSE = {0: "file", 1: "directory"}

# The SDK and the spec use the same two words, so this maps a kind to itself
# and exists only to reject anything that is neither. It used to translate
# "core" into "emulator": that boundary is gone, because a core that emulates
# nothing -- Doom -- showed that "emulator" named a subset, not the set.
KIND = {"homebrew": "homebrew", "core": "core"}

# A core still wearing the template's clothes. Publishing one would put a
# phantom "Example Core" tab in front of users, reading roms/example/.
TEMPLATE_CORE_NAME = "example"
TEMPLATE_DIRNAME = "example"


def make_var(name: str) -> str:
    out = subprocess.check_output(
        ["make", "-f", str(MAKEFILE), "--no-print-directory", f"print-{name}"],
        cwd=ROOT,
        text=True,
    )
    return out.strip()


def cstr(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def digest(path: Path) -> tuple[int, str]:
    payload = path.read_bytes()
    return len(payload), hashlib.sha256(payload).hexdigest()


def envelope(path: Path, magic: bytes, meta_size: int) -> bytes:
    """The header_data of a packed binary, checked down to its length."""
    data = path.read_bytes()
    if len(data) < 8 + meta_size:
        raise SystemExit(f"{path}: too short to be a {magic.decode()} binary")
    if data[:4] != magic:
        raise SystemExit(
            f"{path}: expected {magic.decode()} magic, found {data[:4]!r} — "
            f"is this the packed binary?"
        )
    _header_version, header_length = struct.unpack_from("<HH", data, 4)
    if header_length < meta_size:
        raise SystemExit(
            f"{path}: header_length {header_length} is smaller than the "
            f"{meta_size}-byte metadata struct — this binary predates it"
        )
    return data[8:8 + meta_size]


def pick_gwhb(data: bytes, header_length: int, path: Path) -> int:
    """Which gwhb_meta_t layout this binary carries: GWHB_V1_SIZE or V2.

    `header_length` is the metadata struct *plus* any embedded cover, so it
    cannot simply be compared against a struct size.

    Two signals, in order:

    1. No cover -- header_length is exactly the struct size, which is decisive.
    2. A cover -- `cover_offset` is absolute from the start of the file, so it
       reads 8 + sizeof(meta) for whichever layout is real. Reading it at the
       wrong offset lands in the segment array (v2) or the reserved tail (v1),
       which will not match either candidate.

    Anything else is refused rather than guessed at: a wrong layout produces a
    manifest with a plausible-looking title read out of the middle of another
    field, which is worse than a failed release.
    """
    for size in (GWHB_V1_SIZE, GWHB_V2_SIZE):
        if header_length == size:
            return size
    cover_offset_at = {GWHB_V1_SIZE: 8 + 20, GWHB_V2_SIZE: 8 + 64}
    for size, off in cover_offset_at.items():
        if len(data) < off + 4:
            continue
        if struct.unpack_from("<I", data, off)[0] == 8 + size:
            return size
    raise SystemExit(
        f"{path}: cannot tell which gwhb_meta_t layout this is "
        f"(header_length {header_length}, no usable cover_offset). Both "
        f"layouts claim version 1; rebuild with a current SDK."
    )


def read_gwhb(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 8 + GWHB_V1_SIZE or data[:4] != GWHB_MAGIC:
        # Reuse the shared checks for the message they produce.
        envelope(path, GWHB_MAGIC, GWHB_V1_SIZE)
    _version, header_length = struct.unpack_from("<HH", data, 4)
    size = pick_gwhb(data, header_length, path)

    if size == GWHB_V1_SIZE:
        fields = struct.unpack(GWHB_V1_FORMAT, envelope(path, GWHB_MAGIC, size))
        (abi_version, abi_min_size, _flags, _code, _bss, _cover_off, _cover_size,
         display_name, _maj, _min, _pat, _r0, _reserved) = fields
    else:
        fields = struct.unpack(GWHB_V2_FORMAT, envelope(path, GWHB_MAGIC, size))
        abi_version, abi_min_size, _flags, segments_count = fields[:4]
        display_name = fields[4 + 3 * GWHB_SEGMENTS + 2]
        if not 1 <= segments_count <= GWHB_SEGMENTS:
            raise SystemExit(
                f"{path}: segments_count {segments_count} is outside 1..{GWHB_SEGMENTS}"
            )
    return {
        "abi": {"version": abi_version, "minSize": abi_min_size},
        "title": cstr(display_name),
        "systems": None,
    }


def read_core(path: Path) -> dict:
    meta = envelope(path, CORE_MAGIC, CORE_META_SIZE)
    abi_version, abi_min_size, _flags, _segments_count = struct.unpack_from("<IIII", meta, 0)

    off = 16 + CORE_MAX_SEGMENTS * SEGMENT_SIZE
    (systems_count,) = struct.unpack_from("<I", meta, off)
    off += 4
    if not 1 <= systems_count <= CORE_MAX_SYSTEMS:
        raise SystemExit(f"{path}: systems_count {systems_count} out of range")

    systems = []
    for i in range(systems_count):
        (name, dirname, exts, parse_type, _plo, _pls, _hlo, _hls,
         cheat_ext, _reserved) = struct.unpack_from(SYSTEM_FORMAT, meta, off + i * SYSTEM_SIZE)
        if parse_type not in BROWSE:
            raise SystemExit(f"{path}: unknown parse_type {parse_type}")
        entry = {
            "id": cstr(dirname),
            "longName": cstr(name),
            # The struct stores a space-separated list without dots, because
            # that is what the launcher matches on. A manifest is read by
            # installers and humans, so it gets the dots.
            "extensions": [f".{e}" for e in cstr(exts).split()],
            "browse": BROWSE[parse_type],
        }
        cheat = cstr(cheat_ext)
        if cheat:
            entry["cheatExt"] = cheat
        systems.append(entry)

    off += CORE_MAX_SYSTEMS * SYSTEM_SIZE + 3
    core_name = cstr(struct.unpack_from("<24s", meta, off)[0])

    return {
        "abi": {"version": abi_version, "minSize": abi_min_size},
        "title": core_name,
        "systems": systems,
    }


def check_not_template(header: dict) -> None:
    if header["title"].strip().lower() == TEMPLATE_CORE_NAME:
        raise SystemExit(
            "refusing to publish: this core still declares the template's "
            f"core name ({header['title']!r}). Set --core-name in the Makefile."
        )
    for s in header["systems"] or []:
        if s["id"].strip().lower() == TEMPLATE_DIRNAME:
            raise SystemExit(
                "refusing to publish: this core still declares the template's "
                f"system dirname ({s['id']!r}). Set --system dirname= in the Makefile."
            )


def image_size(path: Path) -> tuple[int, int] | None:
    """(width, height) of a PNG or JPEG, or None if the format is unfamiliar.

    Read here rather than declared, because a dimension a human types is a
    dimension that goes stale the first time the artwork is re-exported. Only
    the two formats a project actually keeps its cover art in are understood;
    anything else simply publishes no dimensions, which the schema allows.
    """
    data = path.read_bytes()
    if data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR":
        return struct.unpack_from(">II", data, 16)
    if data[:2] == b"\xff\xd8":
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                return None
            marker = data[i + 1]
            length = struct.unpack_from(">H", data, i + 2)[0]
            # SOF0..SOF15, excluding the four that are not frame headers.
            if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
                height, width = struct.unpack_from(">HH", data, i + 5)
                return width, height
            i += 2 + length
    return None


def slug(name: str) -> str:
    """A `project` identifier from a human-written CORE_NAME."""
    out = re.sub(r"[^a-z0-9]+", "-", name.strip().lower()).strip("-")
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", out or ""):
        raise SystemExit(
            f"CORE_NAME {name!r} yields no usable project identifier"
        )
    return out


def strip_comments(node):
    """Drop every "$comment" key, at any depth.

    gwrg.json is where a human writes down why a number is what it is, and that
    reasoning is worth keeping next to the number rather than in a commit
    message nobody will find. None of it belongs in a published manifest, whose
    shape is closed: the schema refuses unknown fields wherever it can, so a
    stray note would fail validation at release time.
    """
    if isinstance(node, dict):
        return {k: strip_comments(v) for k, v in node.items() if k != "$comment"}
    if isinstance(node, list):
        return [strip_comments(v) for v in node]
    return node


def load_declared() -> dict:
    """gwrg.json: the hand-written half, and only that."""
    if not DECLARED.is_file():
        return {}
    try:
        doc = json.loads(DECLARED.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(f"{DECLARED}: {exc}") from exc
    unknown = set(doc) - {
        "$comment", "tool", "systems", "uses", "originalSystem", "originalName",
        "storage",
        "runtime", "dataDir", "artifacts",
    }
    if unknown:
        raise SystemExit(f"{DECLARED}: unknown key(s): {', '.join(sorted(unknown))}")
    return strip_comments(doc)


def u32(value, what: str) -> int:
    """A 32-bit address, written however a human finds it readable.

    Addresses are read in hex and stored as numbers, and the conversion belongs
    here rather than in whoever writes gwrg.json: "0xDEC00000" is a value a
    person can check against a linker script at a glance, and 3737124864 is
    not. The manifest carries the integer, because the schema does.
    """
    if isinstance(value, bool):
        raise SystemExit(f"gwrg.json: {what} must be a 32-bit address, not a boolean")
    if isinstance(value, str):
        try:
            value = int(value, 0)
        except ValueError:
            raise SystemExit(f"gwrg.json: {what} is not a number: {value!r}") from None
    if not isinstance(value, int):
        raise SystemExit(f"gwrg.json: {what} must be a 32-bit address, got {value!r}")
    if not 0 <= value <= 0xFFFFFFFF:
        raise SystemExit(f"gwrg.json: {what} does not fit in 32 bits: {value}")
    return value


def declared_artifact(declared: dict, name: str) -> dict:
    """The hand-written half of one artifact, keyed by the file's own name.

    Artifacts arrive as --artifact paths, so their basename is the only handle
    a declaration can key off. Everything derivable -- size, hash, url -- is
    still read off the file; what lives here is the one thing the bytes cannot
    say, which is that the device runs this file where it lies rather than
    reading it out of a filesystem.
    """
    entry = declared.get("artifacts", {}).get(name)
    if entry is None:
        return {}
    unknown = set(entry) - {"mapped", "relocBase"}
    if unknown:
        raise SystemExit(
            f"gwrg.json: artifacts[{name!r}]: unknown key(s): {', '.join(sorted(unknown))}"
        )
    out = {}
    if "mapped" in entry:
        mapped = entry["mapped"]
        if not isinstance(mapped, bool):
            raise SystemExit(f"gwrg.json: artifacts[{name!r}].mapped must be true or false")
        out["mapped"] = mapped
    if "relocBase" in entry:
        if out.get("mapped") is not True:
            raise SystemExit(
                f"gwrg.json: artifacts[{name!r}].relocBase without mapped: the address "
                "the blob was linked at means nothing unless the file is mapped"
            )
        out["relocBase"] = u32(entry["relocBase"], f"artifacts[{name!r}].relocBase")
    return out


# --- the converter, when a project ships one ---------------------------------


def verify_module(wasm_path: Path) -> None:
    """Refuse to publish a module that fails the gate a host applies.

    A manifest is a claim that these bytes are runnable, so the check that
    decides it runs here rather than being asserted. It is the same verify.mjs
    a browser loads, not a reimplementation of it.
    """
    proc = subprocess.run(
        ["node", "verify.mjs", str(wasm_path.resolve())],
        cwd=EXTRACTOR, text=True, capture_output=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"{wasm_path}: refusing to publish a non-conformant module")


def module_memory_ceiling() -> int:
    """The declared ceiling, read from the verifier's own policy.

    Duplicating the number would let the manifest and the gate drift, and the
    manifest is the half nobody would notice was wrong.
    """
    out = subprocess.check_output(
        ["node", "-e",
         'import("./verify.mjs").then(m => console.log(m.DEFAULT_POLICY.maxMemoryPages))'],
        cwd=EXTRACTOR, text=True,
    )
    return int(out.strip())


def build_tool(declared: dict, wasm_path: Path) -> dict:
    verify_module(wasm_path)
    size, sha256 = digest(wasm_path)

    max_output = declared.get("maxOutputBytes")
    if not isinstance(max_output, int):
        raise SystemExit("gwrg.json: tool.maxOutputBytes must be an integer")

    outputs = declared.get("outputs", [])
    for out in outputs:
        # Stated per output, never defaulted from the tool's ceiling: how big a
        # particular file may get is a judgement about that file, and a default
        # would quietly make every output as permissive as the loosest one.
        if not isinstance(out.get("maxBytes"), int):
            raise SystemExit(
                f"gwrg.json: output {out.get('id')!r} must state an integer maxBytes"
            )

    return {
        "id": declared["id"],
        "processor": {"type": "wasm", "version": 1},
        "title": declared["title"],
        # The four fields a host checks before instantiating. `url` is a plain
        # filename resolved beside this manifest, so the same manifest works
        # from the Pages mirror and from an offline bundle.
        "binary": {
            "file": wasm_path.name,
            "url": wasm_path.name,
            "bytes": size,
            "sha256": sha256,
        },
        "limits": {
            "maxMemoryPages": module_memory_ceiling(),
            "maxOutputBytes": max_output,
        },
        "options": declared.get("options", []),
        "inputs": declared["inputs"],
        "outputs": outputs,
    }


# --- systems -----------------------------------------------------------------


def attach_shipped_bios(systems: list[dict], bios_paths: list[Path]) -> None:
    """Fill in url/bytes/sha256 for BIOS files this project ships.

    The hash is read off the file, never declared, for the same reason every
    other hash here is: a number a human types is a number that goes stale.
    A file that matches nothing declared is an error rather than an extra
    artifact -- it would install to /bios/<key>/ and no manifest would say so.
    """
    remaining = {p.name: p for p in bios_paths}
    for system in systems:
        for entry in system.get("bios", []):
            names = entry["filename"]
            names = [names] if isinstance(names, str) else names
            for name in names:
                path = remaining.pop(name, None)
                if path is None:
                    continue
                size, sha256 = digest(path)
                entry["url"] = name
                entry["bytes"] = size
                entry["sha256"] = sha256
                break
    if remaining:
        raise SystemExit(
            "--bios names file(s) no system declares: "
            + ", ".join(sorted(remaining))
        )


def attach_shipped_games(systems: list[dict], game_paths: list[Path]) -> None:
    """Fill in url/bytes/sha256 for games this project ships.

    Unlike a BIOS, a game entry is only ever a published file: "a game the user
    already has" is just a game, and needs no manifest entry. So every declared
    game must be matched by a --game file, and every --game file by a
    declaration -- neither half means anything alone.
    """
    remaining = {p.name: p for p in game_paths}
    for system in systems:
        for entry in system.get("games", []):
            path = remaining.pop(entry["filename"], None)
            if path is None:
                raise SystemExit(
                    f"system {system['id']!r} declares game "
                    f"{entry['filename']!r} but no --game supplied it"
                )
            size, sha256 = digest(path)
            entry["url"] = entry["filename"]
            entry["bytes"] = size
            entry["sha256"] = sha256
    if remaining:
        raise SystemExit(
            "--game names file(s) no system declares: "
            + ", ".join(sorted(remaining))
        )


def merge_systems(derived: list[dict], declared: dict) -> list[dict]:
    """Derived systems plus the parts the binary cannot state.

    The binary is the authority on which systems exist, what they are called,
    which folder they use and which extensions they take. gwrg.json adds the
    short name, whether compressed ROMs work, any BIOS, and the grouping of
    extensions that travel together -- and may not contradict the binary.
    """
    out = []
    for system in derived:
        extra = declared.get(system["id"])
        if extra is None:
            raise SystemExit(
                f"gwrg.json: the binary declares system {system['id']!r} and "
                f"gwrg.json says nothing about it. Every system needs at least "
                f"shortName and compression."
            )
        unknown = set(extra) - {
            "shortName", "compression", "extensions", "bios", "games", "runtime",
            "biosDir", "$comment",
        }
        if unknown:
            raise SystemExit(
                f"gwrg.json: system {system['id']!r} has unknown key(s): "
                f"{', '.join(sorted(unknown))}"
            )

        merged = dict(system)
        # Working space this system needs while running, over and above the
        # files it installs. Declared, not derived: neither gnw_core_meta_t nor
        # the GWHB header carries a savestate size today.
        if "runtime" in extra:
            merged["runtime"] = extra["runtime"]
        # The BIOS folder key, when it is not the ROM folder's. col vs
        # bios/coleco, pcecd vs bios/pce. The struct carries dirname for ROMs
        # and has nowhere to say this, so it is declared.
        if "biosDir" in extra:
            merged["biosDir"] = extra["biosDir"]
        if "extensions" in extra:
            # Grouping is the one thing gwrg.json may add to, because the struct
            # has nowhere to put it: it knows a PC Engine CD game is a .cue, not
            # that .bin tracks come with it.
            #
            # So the rule is not equality. The *launcher-visible* extensions --
            # plain strings, and the first entry of each group -- must match the
            # binary exactly, because those are what the firmware scans for and
            # a typo there would advertise a system that ignores the file.
            # Everything after the first entry of a group is a companion, which
            # by definition the binary cannot know about.
            primary, companions = [], []
            for item in extra["extensions"]:
                if isinstance(item, list):
                    primary.append(item[0])
                    companions.extend(item[1:])
                else:
                    primary.append(item)
            if sorted(set(primary)) != sorted(set(system["extensions"])):
                raise SystemExit(
                    f"gwrg.json: system {system['id']!r} lists launcher extensions "
                    f"{sorted(set(primary))}, but the binary declares "
                    f"{sorted(set(system['extensions']))} — a group's first entry "
                    f"is the file the launcher lists; the rest travel with it"
                )
            merged["extensions"] = extra["extensions"]

        for key in ("shortName", "compression"):
            if key not in extra:
                raise SystemExit(f"gwrg.json: system {system['id']!r} is missing {key}")
            merged[key] = extra[key]
        if "bios" in extra:
            merged["bios"] = extra["bios"]
        if "games" in extra:
            merged["games"] = extra["games"]

        # Field order for readability: identity, names, media, then extras.
        out.append({
            "id": merged["id"],
            "longName": merged["longName"],
            "shortName": merged["shortName"],
            "extensions": merged["extensions"],
            "browse": merged["browse"],
            "compression": merged["compression"],
            **({"cheatExt": merged["cheatExt"]} if "cheatExt" in merged else {}),
            **({"biosDir": merged["biosDir"]} if "biosDir" in merged else {}),
            **({"runtime": merged["runtime"]} if "runtime" in merged else {}),
            **({"bios": merged["bios"]} if "bios" in merged else {}),
            **({"games": merged["games"]} if "games" in merged else {}),
        })

    strays = set(declared) - {s["id"] for s in derived}
    if strays:
        raise SystemExit(
            f"gwrg.json: describes system(s) the binary does not declare: "
            f"{', '.join(sorted(strays))}"
        )
    return out


# --- assembly ----------------------------------------------------------------


def build_manifest(*, bin_path: Path, artifacts: list[Path], wasm_path: Path | None,
                   elf_path: Path | None, cover_path: Path | None,
                   bios_paths: list[Path], game_paths: list[Path],
                   tag: str, repo: str, commit: str) -> dict:
    project_kind = make_var("PROJECT_KIND")
    if project_kind not in KIND:
        raise SystemExit(
            f"unsupported PROJECT_KIND for a dist manifest: {project_kind!r} "
            f"(expected one of {', '.join(sorted(KIND))})"
        )
    kind = KIND[project_kind]

    header = read_core(bin_path) if kind == "core" else read_gwhb(bin_path)
    check_not_template(header)

    declared = load_declared()
    # `project` is an identifier, not a display name: the schema demands
    # ^[a-z0-9][a-z0-9-]*$, it becomes part of the bundle filename, and
    # versions.json must agree with it. CORE_NAME is written for humans and
    # may carry capitals (PokeMini) or punctuation, so derive a slug rather
    # than publishing something a validator refuses. The display name lives
    # in `title`, which comes from the binary.
    project = slug(make_var("CORE_NAME"))
    title = header["title"] or project

    tools = []
    if "tool" in declared:
        if wasm_path is None:
            raise SystemExit("gwrg.json declares a tool; pass --wasm")
        tools.append(build_tool(declared["tool"], wasm_path))
    elif wasm_path is not None:
        raise SystemExit("--wasm given but gwrg.json declares no tool")

    target = {
        **TARGET,
        "kind": kind,
        "requiresAbi": header["abi"],
        "artifacts": [
            {"filename": p.name, "bytes": n, "sha256": h, "url": p.name,
             **declared_artifact(declared, p.name)}
            for p, (n, h) in ((p, digest(p)) for p in [bin_path, *artifacts])
        ],
    }

    # A declaration naming a file this build did not produce is a mistake worth
    # stopping for: it means either a typo or an artifact that was meant to be
    # passed and was not, and both publish a manifest missing the one field
    # that says how the file has to be installed.
    shipped = {p.name for p in [bin_path, *artifacts]}
    orphans = sorted(set(declared.get("artifacts", {})) - shipped)
    if orphans:
        raise SystemExit(
            "gwrg.json: artifacts[] names files this build does not ship: "
            + ", ".join(orphans)
        )

    # Where this homebrew reads its data from, when that is a folder of its own
    # rather than beside the binary. The name is compiled into the binary, so
    # nothing here can derive it -- see spec/03-manifest.md.
    if "dataDir" in declared:
        if kind != "homebrew":
            raise SystemExit(
                "gwrg.json: dataDir is homebrew-only; a core's output goes to "
                "roms/<system id>/, which systems[] already decides"
            )
        target["dataDir"] = declared["dataDir"]

    if "runtime" in declared:
        if kind != "homebrew":
            raise SystemExit(
                "gwrg.json: runtime belongs to a system for a core; "
                "declare it under systems[<id>].runtime"
            )
        target["runtime"] = declared["runtime"]

    if kind == "core":
        target["systems"] = merge_systems(header["systems"], declared.get("systems", {}))
        attach_shipped_bios(target["systems"], bios_paths)
        attach_shipped_games(target["systems"], game_paths)
    elif bios_paths:
        raise SystemExit("--bios given but this is a homebrew, which has no systems[]")
    elif game_paths:
        raise SystemExit("--game given but this is a homebrew, which has no systems[]")
    elif declared.get("systems"):
        raise SystemExit("gwrg.json: systems[] belongs to a core, not a homebrew")

    if tools:
        # Every output of every tool, installed. A project whose target wants
        # only some of them says so in gwrg.json; none does today.
        target["uses"] = declared.get("uses") or [
            {"tool": t["id"], "outputs": [o["id"] for o in t["outputs"]], "required": True}
            for t in tools
        ]

    if elf_path is not None:
        size, sha256 = digest(elf_path)
        # Published and hashed like an artifact, and deliberately not one: the
        # install set is every artifact, and an ELF on the card is dead weight.
        # It rides in dist/<tag>/ so an offline bundle can still symbolicate a
        # crash from a project that has since disappeared.
        target["symbols"] = [{
            "filename": elf_path.name,
            "url": elf_path.name,
            "bytes": size,
            "sha256": sha256,
        }]

    manifest = {
        "schemaVersion": SCHEMA_VERSION,
        "project": project,
        "title": title,
        # Where a human reads about this project. Derived from the repo rather
        # than written down, so it cannot name a repository this is not.
        "docs": f"https://github.com/{repo}#readme",
        "source": {"repo": repo, "commit": commit, "ref": tag},
        "tools": tools,
        "targets": [target],
    }

    # Which installs this project works on. Absent means both: a project only
    # says so when one of them is genuinely out of reach.
    storage = declared.get("storage")
    if storage is not None:
        manifest["storage"] = storage

    # Where this project's work came from. A port says nothing about its origin
    # the way a ROM under roms/<system>/ does, so anything looking up box art
    # would otherwise have to search blind by name.
    #
    # It describes one work, not the software that runs it. A homebrew is always
    # one work. A core usually is not -- an emulator runs whatever the user
    # supplies -- but a core that ships a single game (Doom, from DOS) is a port
    # like any homebrew, and its provenance is just as real. Declaring more than
    # one system is the one case that is certainly wrong: that is an emulator,
    # and it has no single origin to name.
    # originalName is the title the work was published under, which is often not
    # this project's title: the title is the port's name, and a port is
    # frequently named after its engine. "OpenLara" is not in any box-art
    # library; "Tomb Raider" is. Omit it when the title is already the work's
    # own name, which is the common case.
    for field in ("originalSystem", "originalName"):
        value = declared.get(field)
        if value is None:
            continue
        if kind == "core" and len(target.get("systems", [])) > 1:
            raise SystemExit(
                f"gwrg.json: {field} describes one work, but this core declares "
                f"{len(target['systems'])} systems. A core that emulates several "
                "systems is not a port of any one of them"
            )
        manifest[field] = value

    # Full-size box art. The GWHB header can carry a cover too, but that one is
    # bounded by what the device decodes and caches (186x100, 10 KiB); this is
    # the source image, for anything with a larger screen than the device.
    if cover_path is not None:
        size, sha256 = digest(cover_path)
        cover = {
            "filename": cover_path.name,
            "url": cover_path.name,
            "bytes": size,
            "sha256": sha256,
        }
        dimensions = image_size(cover_path)
        if dimensions is not None:
            cover["width"], cover["height"] = dimensions
        manifest["cover"] = cover

    return manifest


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin", dest="bin_path", type=Path, required=True,
                    help="the packed binary (GWHB for a homebrew, CORE for a core)")
    ap.add_argument("--artifact", dest="artifacts", type=Path, action="append", default=[],
                    help="another file installed beside it (zelda3.ro, gba.xip); repeatable")
    ap.add_argument("--wasm", dest="wasm_path", type=Path,
                    help="the converter module, when gwrg.json declares a tool")
    ap.add_argument("--elf", dest="elf_path", type=Path,
                    help="the linked ELF, published for crash symbolication")
    ap.add_argument("--cover", dest="cover_path", type=Path,
                    help="full-size box art, published beside the manifest")
    ap.add_argument("--bios", dest="bios_paths", type=Path, action="append", default=[],
                    help="a BIOS file this project ships rather than asking the "
                         "user for; repeatable. Installs to /bios/<biosDir or id>/")
    ap.add_argument("--game", dest="game_paths", type=Path, action="append", default=[],
                    help="a game this project ships, freely redistributable; "
                         "repeatable. Installs to roms/<system id>/")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--repo", required=True, help="owner/name")
    ap.add_argument("--commit", required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    for label, path in [("packed binary", args.bin_path), ("extractor module", args.wasm_path),
                        ("ELF", args.elf_path), ("cover", args.cover_path),
                        *[("bios", b) for b in args.bios_paths],
                        *[("game", g) for g in args.game_paths],
                        *[("artifact", a) for a in args.artifacts]]:
        if path is not None and not path.is_file():
            raise SystemExit(f"{label} not found: {path}")

    manifest = build_manifest(
        bin_path=args.bin_path, artifacts=args.artifacts, wasm_path=args.wasm_path,
        elf_path=args.elf_path, cover_path=args.cover_path,
        bios_paths=args.bios_paths, game_paths=args.game_paths, tag=args.tag, repo=args.repo, commit=args.commit,
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    target = manifest["targets"][0]
    print(f"make_manifest: wrote {args.out}")
    print(f"  project={manifest['project']!r} title={manifest['title']!r} "
          f"kind={target['kind']} ref={args.tag}")
    print(f"  requiresAbi version={target['requiresAbi']['version']} "
          f"minSize={target['requiresAbi']['minSize']}")
    for a in target["artifacts"]:
        print(f"  artifact {a['filename']!r} {a['bytes']}B sha256={a['sha256'][:16]}…")
    for s in target.get("systems", []):
        shipped = sum(1 for b in s.get("bios", []) if "url" in b)
        print(f"  system {s['id']}: {s['longName']!r} {s['extensions']} "
              f"browse={s['browse']} bios={len(s.get('bios', []))} "
              f"(shipped {shipped}, into /bios/{s.get('biosDir', s['id'])}/)")
        for g in s.get("games", []):
            print(f"  game {g['filename']} {g['bytes']}B -> roms/{s['id']}/")
    for t in manifest["tools"]:
        print(f"  tool {t['id']} {t['binary']['file']} sha256={t['binary']['sha256'][:16]}…")
        # A derived output has no filename here: the host names each produced
        # file after the file it converted, so the most this can show is the
        # extension every one of them will end in.
        produces = [o.get("filename") or f"*{o['extension']}" for o in t["outputs"]]
        print(f"  produces {', '.join(produces)}")
    for s in target.get("symbols", []):
        print(f"  symbols {s['filename']} {s['bytes']}B")
    if "originalSystem" in manifest:
        print(f"  originalSystem={manifest['originalSystem']}")
    if "originalName" in manifest:
        print(f"  originalName={manifest['originalName']!r}")
    if "cover" in manifest:
        c = manifest["cover"]
        size = f" {c['width']}x{c['height']}" if "width" in c else ""
        print(f"  cover {c['filename']} {c['bytes']}B{size}")


if __name__ == "__main__":
    main()
