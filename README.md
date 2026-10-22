# Cannonball (OutRun) for Game & Watch — Retro-Go SD homebrew

**English** | [Español](README.es.md)

Port of [Cannonball](https://github.com/djyt/cannonball), Chris White's OutRun engine, as a GWHB
homebrew for the Retro-Go SD 2.x firmware (`external_cores` branch).

- The original game at 30 fps, like the arcade. Sound from the real chips (SegaPCM + YM2151) at
  22,050 Hz.
- Contains nothing from the game: it uses your own OutRun rev. B ROMs and converts them on the
  console the first time.

## Installation

On the SD card:

    /homebrews/Cannonball.bin        <- this program
    /homebrews/cannonball/           <- the OutRun rev. B ROMs (the contents of outrun.zip, unzipped)

The ROMs go directly inside `/homebrews/cannonball/`, with no subfolders. The firmware hides that
folder in the menu, because it contains no homebrew.

In the menu: Homebrew tab → Cannonball.

The first time, it checks the ROMs, creates `/homebrews/cannonball/outrun.dat` (2.3 MB, with the
ROMs already sorted and converted) and copies it to the external flash. After that it starts
straight away. Once `outrun.dat` exists, the ROMs are no longer needed.

ROMs are looked up by name and, if the name does not match, by CRC (as Cannonball does), so the
MAME `outrun` set (rev. B) works. If one is missing or is not revision B, a message names the
file.

If the external flash is ever erased or rewritten (for example by an update), the program notices
and copies `outrun.dat` again. If something still fails, delete `outrun.dat` and it is rebuilt
from the ROMs.

## Required ROMs

The 31 files of OutRun rev. B, MAME's `outrun` set: the loose files you get when unzipping
`outrun.zip`. The zip's subfolders (`outrunb`, `outrunra`, `outrundx`, …) are other versions and
are not needed. Note: `outrunb` is a bootleg from the time, not rev. B.

Together they take 2,064,384 bytes (1.97 MB).

| File | Size | CRC32 | Contents |
|---|---:|---|---|
| `epr-10380b.133` | 64 KB (65,536 B) | `1f6cadad` | Main CPU |
| `epr-10382b.118` | 64 KB (65,536 B) | `c4c3fa1a` | Main CPU |
| `epr-10381b.132` | 64 KB (65,536 B) | `be8c412b` | Main CPU |
| `epr-10383b.117` | 64 KB (65,536 B) | `10a2014a` | Main CPU |
| `epr-10327a.76` | 64 KB (65,536 B) | `e28a5baf` | Sub CPU |
| `epr-10329a.58` | 64 KB (65,536 B) | `da131c81` | Sub CPU |
| `epr-10328a.75` | 64 KB (65,536 B) | `d5ec5e5d` | Sub CPU |
| `epr-10330a.57` | 64 KB (65,536 B) | `ba9ec82a` | Sub CPU |
| `epr-10187.88` | 32 KB (32,768 B) | `a10abaa9` | Sound program |
| `opr-10193.66` | 32 KB (32,768 B) | `bcd10dde` | Sound samples |
| `opr-10192.67` | 32 KB (32,768 B) | `770f1270` | Sound samples |
| `opr-10191.68` | 32 KB (32,768 B) | `20a284ab` | Sound samples |
| `opr-10190.69` | 32 KB (32,768 B) | `7cab70e2` | Sound samples |
| `opr-10189.70` | 32 KB (32,768 B) | `01366b54` | Sound samples |
| `opr-10188.71` | 32 KB (32,768 B) | `bad30ad9` | Sound samples |
| `opr-10185.11` | 32 KB (32,768 B) | `22794426` | Road |
| `opr-10186.47` | 32 KB (32,768 B) | `22794426` | Road |
| `opr-10268.99` | 32 KB (32,768 B) | `95344b04` | Backgrounds (tiles) |
| `opr-10232.102` | 32 KB (32,768 B) | `776ba1eb` | Backgrounds (tiles) |
| `opr-10267.100` | 32 KB (32,768 B) | `a85bb823` | Backgrounds (tiles) |
| `opr-10231.103` | 32 KB (32,768 B) | `8908bcbf` | Backgrounds (tiles) |
| `opr-10266.101` | 32 KB (32,768 B) | `9f6f1a74` | Backgrounds (tiles) |
| `opr-10230.104` | 32 KB (32,768 B) | `686f5e50` | Backgrounds (tiles) |
| `mpr-10371.9` | 128 KB (131,072 B) | `7cc86208` | Sprites |
| `mpr-10373.10` | 128 KB (131,072 B) | `b0d26ac9` | Sprites |
| `mpr-10375.11` | 128 KB (131,072 B) | `59b60bd7` | Sprites |
| `mpr-10377.12` | 128 KB (131,072 B) | `17a1b04a` | Sprites |
| `mpr-10372.13` | 128 KB (131,072 B) | `b557078c` | Sprites |
| `mpr-10374.14` | 128 KB (131,072 B) | `8051e517` | Sprites |
| `mpr-10376.15` | 128 KB (131,072 B) | `f3b8f318` | Sprites |
| `mpr-10378.16` | 128 KB (131,072 B) | `a1062984` | Sprites |

Names may differ: if a file does not match by name, it is found by its CRC.

## Controls

| Button | Action |
|---|---|
| Left / right | Steering (and choosing the song / route) |
| A | Accelerate |
| B | Brake |
| Up / down | High / low gear |
| GAME | Start |
| TIME | Coin (not needed: it runs in *free play*) |
| PAUSE/SET | Retro-Go menu |

## Menu

The game starts on Cannonball's menu (OutRun logo over the scrolling road):

- **PLAY GAME**: goes to the game (attract mode; GAME starts a race).
- **SETTINGS**: left/right or A change the value, B goes back.
  - **GEARS**: manual (up/down = high/low) or automatic.
  - **TIME**: easy, normal, hard, hardest or infinite.
  - **TRAFFIC**: easy, normal, hard, hardest or none.
  - **STEERING**: 1 to 9, how fast the car turns while left/right is held (the D-pad is digital,
    so the wheel turns step by step; 3 is Cannonball's default, higher answers sooner).
- **EXIT**: back to Retro-Go.

In the menu: up/down to move, A or GAME to choose. To go back to the menu from the game: PAUSE/SET
→ Options → **Cannonball menu**.

The menu follows the Retro-Go language for English, Spanish, Portuguese, French, Italian, German and
Norwegian, without accents (the OutRun font has none); other languages use English. The settings
are kept by the firmware with the homebrew's other settings, so they survive restarts.

High scores are saved in `/homebrews/cannonball/hiscores*.bin`.

## Building

With the Arm GNU Toolchain 15.2.rel1 (the same one retro-go-sd uses):

    make GCC_PATH=/path/to/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin

Or with Docker: `make docker`. The result is `build/Cannonball.bin`.

`external/cannonball` is djyt/cannonball 27493eb with a minimal patch applied by
`tools/prepare_cannonball.py` (to regenerate it: `tools/prepare_cannonball.py /path/to/cannonball`).
The patch only changes *where* the large data lives; game logic, rendering and sound are untouched.

## How it is laid out

| What | Where | Size |
|---|---|---|
| Engine code + data | RAM_EMU | 130 KB of code, 400 KB of BSS |
| Sprites, tiles, CPU ROMs, PCM samples | External flash (`outrun.dat`) | 2.3 MB |
| Decoded road | RAM_EMU | 257 KB |
| Frame buffer (palette indices) | RAM_EMU | 140 KB |
| Tile and text RAM (`hwtiles`) | DTCM | 68 KB |

About 15 KB of RAM_EMU and 36 KB of DTCM are left free. This layout was the fastest in the
on-console benchmark (`cannonball_bench`): the road in RAM and the tiles in flash.

Port files (`src/gw/`):

- `main_cannonball.c`: firmware side (startup, conversion, input, timing, audio, menu).
- `gw_engine.cpp`: what Cannonball's `main.cpp` and SDL layer used to do.
- `romconv.cpp`: builds `outrun.dat` from the ROMs.
- `roms_gw.cpp`, `config_gw.cpp`, `utils_gw.cpp`: replace Cannonball's (no XML, no iostreams,
  ROMs read straight from flash).
- `cxx_runtime_gw.cpp`: the minimal part of libstdc++ the engine uses.
- `shim/`: replacement headers (SDL types, empty `iostream`).

## Status

- Tested in the gwemu emulator with the `external_cores` firmware (b791f15):
  - ROM conversion and copy to flash;
  - the converted data is bit-identical to what Cannonball produces;
  - front-end menu (settings saved across restarts, Spanish and German labels), back to the menu
    from the pause menu, EXIT;
  - attract mode, music selection, racing, crashing, pause menu, options, exit to menu,
    power-off, missing-ROM message and recovery from a stale flash copy.
- Also tested on the console. The earlier benchmark, on the console at 280 MHz, averaged about
  19 ms per frame against a 33 ms budget.

## License

Cannonball is by Chris White and is distributed under its own license
(`external/cannonball/license.txt`): non-commercial use. OutRun is SEGA's; the ROMs are not
included.
