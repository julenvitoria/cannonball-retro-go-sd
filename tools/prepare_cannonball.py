#!/usr/bin/env python3
"""Copy Cannonball's engine into external/cannonball and apply the minimal Game & Watch patch.

usage: tools/prepare_cannonball.py /path/to/cannonball      (git clone https://github.com/djyt/cannonball)

The engine is used unmodified except for WHERE the big converted graphics live (they cannot sit
inside the C++ objects on a 724 KB core):
  - hwtiles:   uint32_t tiles[0x10000] -> uint32_t *tiles   (256 KB, external flash)
               tiles_backup + patch_tiles()/restore_tiles() removed (custom-music tile patch only)
  - hwsprites: uint32_t sprites[0x40000] -> uint32_t *sprites (1 MB, external flash)
  - hwroad:    uint8_t roads[0x40200] -> uint8_t *roads     (257 KB, RAM)
  - omusic.cpp: the two calls to patch_tiles()/restore_tiles() are compiled out.
  - each of those classes gets `friend struct GwPort;` so the port can set the pointers.
  - oinitengine.cpp: the HUD's L/H gear letter is also drawn with automatic gears (upstream only
    draws it for manual modes, so switching to automatic in the pause menu froze the last letter).
  - video.cpp: the hwtiles object (68 KB: tile + text RAM) is placed with gw_fast_alloc() (DTCM,
    zero-wait) instead of plain new, which would put it in RAM_EMU before the game starts and
    leave no room there for the 140 KB frame buffer.
Every replacement must match exactly once, so a different Cannonball revision fails loudly
instead of being patched wrongly. Tested against djyt/cannonball 27493eb.
"""
import os, re, shutil, subprocess, sys

root = os.path.abspath(sys.argv[1])
src = os.path.join(root, 'src', 'main')
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'external', 'cannonball')

def sub(path, old, new, count=1):
    p = os.path.join(dst, path); s = open(p, encoding='latin-1').read()
    n = s.count(old)
    if n != count:
        sys.exit(f"patch failed: {path}: expected {count} match(es) of {old[:60]!r}, found {n}")
    open(p, 'w', encoding='latin-1').write(s.replace(old, new))

def sub_re(path, pattern, new):
    p = os.path.join(dst, path); s = open(p, encoding='latin-1').read()
    s2, n = re.subn(pattern, new, s, flags=re.S)
    if n != 1: sys.exit(f"patch failed: {path}: regex {pattern[:60]!r} matched {n} times")
    open(p, 'w', encoding='latin-1').write(s2)

if os.path.exists(dst): shutil.rmtree(dst)
shutil.copytree(src, dst, ignore=shutil.ignore_patterns('*.o', 'windirent.h'))
shutil.copy(os.path.join(root, 'docs', 'license.txt'), os.path.join(dst, 'license.txt'))
try:
    rev = subprocess.check_output(['git', '-C', root, 'rev-parse', 'HEAD'], text=True).strip()
except Exception:
    rev = 'unknown'
open(os.path.join(dst, 'UPSTREAM'), 'w').write(f"https://github.com/djyt/cannonball {rev}\n"
                                               "Patched by tools/prepare_cannonball.py\n")

FRIEND = '\n    friend struct GwPort;   // Game & Watch port: points the ROM data at flash/RAM\n'
# hwtiles
sub('hwvideo/hwtiles.hpp', 'class hwtiles\n{\n', 'class hwtiles\n{' + FRIEND)
sub('hwvideo/hwtiles.hpp', '    uint32_t tiles[TILES_LENGTH];        // Converted tiles\n',
    '    uint32_t *tiles;                     // G&W: converted tiles, in external flash\n')
sub('hwvideo/hwtiles.hpp', '    uint32_t tiles_backup[TILES_LENGTH]; // Converted tiles (backup without patch)\n', '')
sub('hwvideo/hwtiles.hpp', '    void patch_tiles(RomLoader* patch);\n', '')
sub('hwvideo/hwtiles.hpp', '    void restore_tiles();\n', '')
sub('hwvideo/hwtiles.cpp', '        memcpy(tiles_backup, tiles, TILES_LENGTH * sizeof(uint32_t));\n', '')
sub_re('hwvideo/hwtiles.cpp', r'// Patch Tileset with new data\nvoid hwtiles::patch_tiles.*?\nvoid hwtiles::restore_tiles\(\)\n\{.*?\n\}\n',
       '// G&W: patch_tiles()/restore_tiles() removed (they needed a second 256 KB tile copy).\n')
sub('engine/omusic.cpp', '        video.tile_layer->patch_tiles(tile_patch);\n',
    '        // G&W: video.tile_layer->patch_tiles(tile_patch); (no custom music tiles)\n')
sub('engine/omusic.cpp', '        video.tile_layer->restore_tiles();\n',
    '        // G&W: video.tile_layer->restore_tiles();\n')
# hwsprites
sub('hwvideo/hwsprites.hpp', 'class hwsprites\n{\n', 'class hwsprites\n{' + FRIEND)
sub('hwvideo/hwsprites.hpp', '    uint32_t sprites[SPRITES_LENGTH]; // Converted sprites\n',
    '    uint32_t *sprites;                // G&W: converted sprites, in external flash\n')
# hwroad
sub('hwvideo/hwroad.hpp', 'class HWRoad\n{\n', 'class HWRoad\n{' + FRIEND)
sub('hwvideo/hwroad.hpp', '    uint8_t roads[0x40200];\n', '    uint8_t *roads;               // G&W: decoded road, in RAM\n')
# oinitengine.cpp: L/H gear letter with automatic gears too
sub('engine/oinitengine.cpp',
    '        if ((config.controls.gear == config.controls.GEAR_BUTTON ||\n'
    '            config.controls.gear == config.controls.GEAR_SEPARATE)\n',
    '        if ((config.controls.gear == config.controls.GEAR_BUTTON ||\n'
    '            config.controls.gear == config.controls.GEAR_SEPARATE ||\n'
    '            config.controls.gear == config.controls.GEAR_AUTO)   // G&W: show it in auto too\n')
# video.cpp: hwtiles in DTCM
sub('video.cpp', '    tile_layer   = new hwtiles();\n',
    '    tile_layer   = new (gw_fast_alloc(sizeof(hwtiles))) hwtiles();   // G&W: DTCM (gw_alloc.hpp)\n')
sub('video.cpp', '#include "video.hpp"\n', '#include "video.hpp"\n#include "gw_alloc.hpp"            // G&W port\n')
print(f"prepared {os.path.normpath(dst)} from {rev}")
