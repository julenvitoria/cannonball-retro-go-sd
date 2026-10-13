// Game & Watch port: replaces roms.cpp + romloader.cpp. The ROM set is not loaded into RAM:
// outrun.dat (built once by romconv.cpp) is cached into external flash by the firmware and
// every RomLoader points straight at its section there. The game only ever reads ROMs.
#include <string.h>
#include "stdint.hpp"
#include "roms.hpp"
#include "gw_port.hpp"

Roms roms;

RomLoader::RomLoader()  { rom = NULL; length = 0; loaded = false; load = &RomLoader::load_rom; }
RomLoader::~RomLoader() {}
void RomLoader::init(uint32_t len) { length = len; }
void RomLoader::unload(void) { rom = NULL; }
int  RomLoader::load_rom(const char*, const int, const int, const int, const uint8_t, const bool)   { return 1; }
int  RomLoader::load_crc32(const char*, const int, const int, const int, const uint8_t, const bool) { return 1; }
int  RomLoader::load_binary(const char*) { return 1; }
int  RomLoader::create_map() { return 0; }
int  RomLoader::filesize(const char*) { return 0; }

Roms::Roms()  { jap_rom_status = -1; rom0p = NULL; rom1p = NULL; }
Roms::~Roms() {}

// Defined in gw_engine.cpp: outrun.dat as mapped by the firmware.
extern const uint8_t* g_cb_dat;

static void map(RomLoader& r, int sec)
{
    const cb_dat_header_t* h = (const cb_dat_header_t*)g_cb_dat;
    r.rom    = (uint8_t*)(g_cb_dat + h->offset[sec]);   // read-only: flash
    r.length = h->length[sec];
    r.loaded = true;
}

bool Roms::load_revb_roms(bool)
{
    if (!g_cb_dat) return false;
    map(rom0, SEC_ROM0);
    map(rom1, SEC_ROM1);
    map(z80,  SEC_Z80);
    map(pcm,  SEC_PCM);
    map(road, SEC_ROAD);
    // Tiles and sprites are stored already converted; GwPort hands them to the renderers and
    // Video::init() is given NULL for both so it does not convert them again.
    tiles.rom = NULL;   tiles.length = 0;
    sprites.rom = NULL; sprites.length = 0;
    rom0p = &rom0;
    rom1p = &rom1;
    return true;
}

bool Roms::load_japanese_roms() { jap_rom_status = 1; return false; }   // not supported on the G&W
int  Roms::load_pcm_rom(bool)   { return 0; }
bool Roms::load_ym_data(const char*) { return false; }                    // custom YM music: not supported
