// Cannonball for the Game & Watch (Retro-Go SD homebrew): what the port's files share.
// Usable from C (main_cannonball.c) and C++ (the engine side).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// SD card layout
// Everything lives next to the homebrew: /homebrews/ subfolders without a .bin are hidden from
// the launcher. The ROM files go straight into this folder, beside outrun.dat and the scores.
#define CB_DIR        "/homebrews/cannonball"
#define CB_ROM_DIR    CB_DIR
#define CB_DATA_FILE  CB_DIR "/outrun.dat"

// outrun.dat: the OutRun rev. B ROM set, already interleaved/decoded the way Cannonball's
// loaders leave it in memory, so the game can read it straight from memory-mapped flash.
#define CB_DAT_MAGIC   0x524f4243u      // "CBOR"
#define CB_DAT_VERSION 1u
enum { SEC_ROM0, SEC_ROM1, SEC_Z80, SEC_PCM, SEC_ROAD, SEC_TILES, SEC_SPRITES, SEC_COUNT };
typedef struct
{
    uint32_t magic, version, header_size, total_size;
    uint32_t offset[SEC_COUNT], length[SEC_COUNT];
    uint32_t reserved[2];
} cb_dat_header_t;
#ifdef __cplusplus
static_assert(sizeof(cb_dat_header_t) == 16 + 8 * SEC_COUNT + 8, "outrun.dat header");
extern "C" {
#else
_Static_assert(sizeof(cb_dat_header_t) == 16 + 8 * SEC_COUNT + 8, "outrun.dat header");
#endif

// romconv.cpp: builds outrun.dat from the ROM files in CB_ROM_DIR (once). `scratch` must hold
// 0x40000 bytes. Returns NULL on success, or a message for the user.
const char* cb_convert_roms(uint8_t* scratch, size_t scratch_size,
                            void (*progress)(const char* what, int percent));
// True if CB_ROM_DIR has at least one file that could be a ROM (so a conversion can be attempted).
bool cb_roms_present(void);
// Checks a mapped (or read) outrun.dat header against the expected layout.
bool cb_dat_header_ok(const cb_dat_header_t* h, uint32_t file_size);

#ifdef __cplusplus
}
#endif
