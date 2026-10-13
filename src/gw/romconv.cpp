// Game & Watch port: builds CB_DATA_FILE (outrun.dat) from the user's OutRun rev. B ROM files.
//
// Same ROM list, CRCs and layout as Cannonball's roms.cpp/romloader.cpp, and tiles/sprites are
// pre-converted exactly as hwtiles::init()/hwsprites::init() would do it, so at run time the
// game reads everything straight from external flash. Files are looked up by name first and,
// like Cannonball's CRC mode, by CRC-32 among the files in CB_ROM_DIR if the name differs.
// Runs once (first launch, or when outrun.dat is missing or stale). All its buffers live in the
// `scratch` RAM lent by the engine (the road buffer, not in use yet): nothing stays in BSS.
#include <stdio.h>
#include <string.h>
#include "gw_port.hpp"
extern "C" {
#include "ff.h"
}
extern "C" void wdog_refresh(void);

enum { NORMAL = 1, INTERLEAVE2 = 2, INTERLEAVE4 = 4 };
struct rom_file_t { const char* name; uint32_t offset, length, crc; uint8_t mode; uint8_t region; };

static const uint32_t REGION_LEN[SEC_COUNT] = { 0x40000, 0x40000, 0x10000, 0x60000, 0x10000, 0x40000, 0x100000 };
#define TILES_RAW_LEN 0x30000u

static const rom_file_t ROMS[] = {
    // Master CPU
    { "epr-10380b.133", 0x00000, 0x10000, 0x1f6cadad, INTERLEAVE2, SEC_ROM0 },
    { "epr-10382b.118", 0x00001, 0x10000, 0xc4c3fa1a, INTERLEAVE2, SEC_ROM0 },
    { "epr-10381b.132", 0x20000, 0x10000, 0xbe8c412b, INTERLEAVE2, SEC_ROM0 },
    { "epr-10383b.117", 0x20001, 0x10000, 0x10a2014a, INTERLEAVE2, SEC_ROM0 },
    // Slave CPU
    { "epr-10327a.76",  0x00000, 0x10000, 0xe28a5baf, INTERLEAVE2, SEC_ROM1 },
    { "epr-10329a.58",  0x00001, 0x10000, 0xda131c81, INTERLEAVE2, SEC_ROM1 },
    { "epr-10328a.75",  0x20000, 0x10000, 0xd5ec5e5d, INTERLEAVE2, SEC_ROM1 },
    { "epr-10330a.57",  0x20001, 0x10000, 0xba9ec82a, INTERLEAVE2, SEC_ROM1 },
    // Z80 sound program (0x8000 of the 0x10000 region; the rest is room for custom music)
    { "epr-10187.88",   0x00000, 0x08000, 0xa10abaa9, NORMAL, SEC_Z80 },
    // Sega PCM samples
    { "opr-10193.66",   0x00000, 0x08000, 0xbcd10dde, NORMAL, SEC_PCM },
    { "opr-10192.67",   0x10000, 0x08000, 0x770f1270, NORMAL, SEC_PCM },
    { "opr-10191.68",   0x20000, 0x08000, 0x20a284ab, NORMAL, SEC_PCM },
    { "opr-10190.69",   0x30000, 0x08000, 0x7cab70e2, NORMAL, SEC_PCM },
    { "opr-10189.70",   0x40000, 0x08000, 0x01366b54, NORMAL, SEC_PCM },
    { "opr-10188.71",   0x50000, 0x08000, 0xbad30ad9, NORMAL, SEC_PCM },
    // Road (two identical ROMs, one per road layer)
    { "opr-10185.11",   0x00000, 0x08000, 0x22794426, NORMAL, SEC_ROAD },
    { "opr-10186.47",   0x08000, 0x08000, 0x22794426, NORMAL, SEC_ROAD },
    // Tiles (raw 0x30000, converted below)
    { "opr-10268.99",   0x00000, 0x08000, 0x95344b04, NORMAL, SEC_TILES },
    { "opr-10232.102",  0x08000, 0x08000, 0x776ba1eb, NORMAL, SEC_TILES },
    { "opr-10267.100",  0x10000, 0x08000, 0xa85bb823, NORMAL, SEC_TILES },
    { "opr-10231.103",  0x18000, 0x08000, 0x8908bcbf, NORMAL, SEC_TILES },
    { "opr-10266.101",  0x20000, 0x08000, 0x9f6f1a74, NORMAL, SEC_TILES },
    { "opr-10230.104",  0x28000, 0x08000, 0x686f5e50, NORMAL, SEC_TILES },
    // Sprites
    { "mpr-10371.9",    0x00000, 0x20000, 0x7cc86208, INTERLEAVE4, SEC_SPRITES },
    { "mpr-10373.10",   0x00001, 0x20000, 0xb0d26ac9, INTERLEAVE4, SEC_SPRITES },
    { "mpr-10375.11",   0x00002, 0x20000, 0x59b60bd7, INTERLEAVE4, SEC_SPRITES },
    { "mpr-10377.12",   0x00003, 0x20000, 0x17a1b04a, INTERLEAVE4, SEC_SPRITES },
    { "mpr-10372.13",   0x80000, 0x20000, 0xb557078c, INTERLEAVE4, SEC_SPRITES },
    { "mpr-10374.14",   0x80001, 0x20000, 0x8051e517, INTERLEAVE4, SEC_SPRITES },
    { "mpr-10376.15",   0x80002, 0x20000, 0xf3b8f318, INTERLEAVE4, SEC_SPRITES },
    { "mpr-10378.16",   0x80003, 0x20000, 0xa1062984, INTERLEAVE4, SEC_SPRITES },
};
#define NROMS (int)(sizeof ROMS / sizeof ROMS[0])

// ------------------------------------------------------------------------------------------------
static uint32_t crc_table[256];
static void crc_init()
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        crc_table[i] = c;
    }
}
static uint32_t crc_update(uint32_t c, const uint8_t* p, size_t n)
{
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}

// Scratch layout (SCRATCH_NEEDED bytes): [0, WORK) assembly area, then the small buffers.
#define WORK        0x34000u                         // largest user: tiles, raw 0x30000 + 0x4000 out
#define IO_SIZE     0x4000u
struct dir_entry_t { char name[64]; uint32_t size, crc; bool crc_done; };
#define DIR_MAX     48
#define SCRATCH_NEEDED (WORK + IO_SIZE + DIR_MAX * sizeof(dir_entry_t) + NROMS * 96)
static uint8_t*     g_io;                            // file read chunk
static char       (*g_path)[96];                     // resolved path of each ROM

// CRC-32 and size of a whole file; false if it cannot be read
static bool file_crc(const char* path, uint32_t* crc, uint32_t* size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint32_t c = 0, n = 0; size_t r;
    while ((r = fread(g_io, 1, IO_SIZE, f)) > 0) { c = crc_update(c, g_io, r); n += r; wdog_refresh(); }
    fclose(f);
    *crc = c; *size = n;
    return true;
}

// The other files in the ROM folder, for the by-CRC lookup (filled on first need)
static dir_entry_t* g_dir;
static int g_ndir = -1;
static void scan_dir()
{
    g_ndir = 0;
    DIR d; FILINFO fi;
    if (f_opendir(&d, CB_ROM_DIR) != FR_OK) return;
    while (g_ndir < DIR_MAX && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fattrib & AM_DIR) continue;
        if (strlen(fi.fname) >= sizeof g_dir[0].name) continue;
        strcpy(g_dir[g_ndir].name, fi.fname);
        g_dir[g_ndir].size = (uint32_t)fi.fsize;
        g_dir[g_ndir].crc_done = false;
        g_ndir++;
    }
    f_closedir(&d);
}

static const char* g_progress_what;
static void (*g_progress)(const char*, int);
static void progress(int pct) { if (g_progress) g_progress(g_progress_what, pct); }

bool cb_dat_header_ok(const cb_dat_header_t* h, uint32_t file_size)
{
    if (h->magic != CB_DAT_MAGIC || h->version != CB_DAT_VERSION || h->header_size != sizeof *h) return false;
    if (h->total_size > file_size) return false;
    for (int s = 0; s < SEC_COUNT; s++)
        if (h->length[s] != REGION_LEN[s] || h->offset[s] + h->length[s] > h->total_size || (h->offset[s] & 3)) return false;
    return true;
}

bool cb_roms_present()
{
    DIR d; FILINFO fi;
    if (f_opendir(&d, CB_ROM_DIR) != FR_OK) return false;
    bool any = false;
    // The folder also holds outrun.dat and the score files: look for something ROM-sized
    while (!any && f_readdir(&d, &fi) == FR_OK && fi.fname[0])
        if (!(fi.fattrib & AM_DIR) && strncmp(fi.fname, "hiscores", 8) != 0)
            for (int i = 0; i < NROMS && !any; i++)
                if (fi.fsize == ROMS[i].length) any = true;
    f_closedir(&d);
    return any;
}

// Finds every ROM (by name, else by CRC) and checks its CRC. Returns the missing/bad file name.
static const char* resolve_all()
{
    for (int i = 0; i < NROMS; i++) {
        const rom_file_t& r = ROMS[i];
        snprintf(g_path[i], sizeof g_path[i], "%s/%s", CB_ROM_DIR, r.name);
        uint32_t crc, size;
        progress(i * 100 / NROMS);
        if (file_crc(g_path[i], &crc, &size) && crc == r.crc && size == r.length) continue;
        // Not there under its name (or a different revision): look for the CRC
        if (g_ndir < 0) scan_dir();
        bool found = false;
        for (int k = 0; k < g_ndir && !found; k++) {
            dir_entry_t& e = g_dir[k];
            if (e.size != r.length) continue;
            char p[96]; snprintf(p, sizeof p, "%s/%s", CB_ROM_DIR, e.name);
            if (!e.crc_done) { uint32_t s; if (!file_crc(p, &e.crc, &s)) continue; e.crc_done = true; }
            if (e.crc == r.crc) { strcpy(g_path[i], p); found = true; }
        }
        if (!found) return r.name;
    }
    return NULL;
}

// Reads `len` bytes of ROM i into dst with the given stride (1 = contiguous)
static bool read_rom(int i, uint8_t* dst, uint32_t stride)
{
    FILE* f = fopen(g_path[i], "rb");
    if (!f) return false;
    uint32_t left = ROMS[i].length;
    while (left) {
        uint32_t n = left > IO_SIZE ? IO_SIZE : left;
        if (fread(g_io, 1, n, f) != n) { fclose(f); return false; }
        if (stride == 1) { memcpy(dst, g_io, n); dst += n; }
        else for (uint32_t k = 0; k < n; k++) { *dst = g_io[k]; dst += stride; }
        left -= n;
        wdog_refresh();
    }
    fclose(f);
    return true;
}

static bool write_all(FILE* out, const uint8_t* p, uint32_t n)
{
    while (n) {
        uint32_t c = n > 0x4000 ? 0x4000 : n;
        if (fwrite(p, 1, c, out) != c) return false;
        p += c; n -= c;
        wdog_refresh();
    }
    return true;
}

// Builds bytes [base, base + len) of one region in `buf`, from the ROMs of that region that
// fall in that window (each ROM must lie entirely inside one window).
static bool assemble(int region, uint32_t base, uint8_t* buf, uint32_t len)
{
    memset(buf, 0, len);
    for (int i = 0; i < NROMS; i++) {
        const rom_file_t& r = ROMS[i];
        if (r.region != region || r.offset < base || r.offset >= base + len) continue;
        if (r.offset + (r.length - 1) * r.mode >= base + len) return false;
        if (!read_rom(i, buf + (r.offset - base), r.mode)) return false;
    }
    return true;
}

const char* cb_convert_roms(uint8_t* scratch, size_t scratch_size, void (*prog)(const char*, int))
{
    static char msg[96];
    g_progress = prog;
    if (scratch_size < SCRATCH_NEEDED) return "Internal error: no scratch RAM";
    g_io   = scratch + WORK;
    g_dir  = (dir_entry_t*)(g_io + IO_SIZE);
    g_path = (char (*)[96])(g_dir + DIR_MAX);
    g_ndir = -1;
    crc_init();

    g_progress_what = "Checking ROMs";
    const char* bad = resolve_all();
    if (bad) { snprintf(msg, sizeof msg, "Missing or bad ROM: %s", bad); return msg; }

    FILE* out = fopen(CB_DATA_FILE, "wb");
    if (!out) return "Cannot write " CB_DATA_FILE;

    cb_dat_header_t h;
    memset(&h, 0, sizeof h);
    uint32_t pos = 0x1000;
    for (int s = 0; s < SEC_COUNT; s++) { h.offset[s] = pos; h.length[s] = REGION_LEN[s]; pos += (REGION_LEN[s] + 0xfff) & ~0xfffu; }
    h.header_size = sizeof h; h.total_size = pos; h.version = CB_DAT_VERSION;
    h.magic = 0;                                   // written last: an interrupted conversion stays invalid
    memset(scratch, 0, 0x1000);
    memcpy(scratch, &h, sizeof h);
    bool ok = write_all(out, scratch, 0x1000);

    g_progress_what = "Converting ROMs";
    // Program/sound/road regions: assembled in 0x20000 windows in scratch
    static const int small[] = { SEC_ROM0, SEC_ROM1, SEC_Z80, SEC_ROAD };
    int step = 0;
    for (int s = 0; s < SEC_COUNT && ok; s++) {
        progress(step++ * 100 / SEC_COUNT);
        bool is_small = false;
        for (int k = 0; k < 4; k++) if (small[k] == s) is_small = true;
        fseek(out, h.offset[s], SEEK_SET);
        if (is_small) {
            for (uint32_t w = 0; w < REGION_LEN[s] && ok; w += 0x20000) {
                uint32_t n = REGION_LEN[s] - w < 0x20000 ? REGION_LEN[s] - w : 0x20000;
                ok = assemble(s, w, scratch, n) && write_all(out, scratch, n);
            }
        }
        else if (s == SEC_PCM) {
            // 6 x 0x8000 at a 0x10000 stride: two halves of 0x30000
            for (int half = 0; half < 2 && ok; half++) {
                memset(scratch, 0, 0x30000);
                for (int i = 0; i < NROMS && ok; i++)
                    if (ROMS[i].region == SEC_PCM && ROMS[i].offset / 0x30000 == (uint32_t)half)
                        ok = read_rom(i, scratch + ROMS[i].offset % 0x30000, 1);
                ok = ok && write_all(out, scratch, 0x30000);
            }
        }
        else if (s == SEC_TILES) {
            // Raw 3-plane tiles -> hwtiles::init() format: one uint32_t of 8 4-bit pixels per byte
            ok = assemble(SEC_TILES, 0, scratch, TILES_RAW_LEN);
            uint32_t* conv = (uint32_t*)(scratch + TILES_RAW_LEN);      // 16 KB chunks after the raw data
            for (uint32_t base = 0; base < 0x10000 && ok; base += 0x1000) {
                for (uint32_t j = 0; j < 0x1000; j++) {
                    uint32_t i = base + j;
                    uint8_t p0 = scratch[i], p1 = scratch[i + 0x10000], p2 = scratch[i + 0x20000];
                    uint32_t val = 0;
                    for (int ii = 0; ii < 8; ii++) {
                        uint8_t bit = 7 - ii;
                        uint8_t pix = ((((p0 >> bit)) & 1) | (((p1 >> bit) << 1) & 2) | (((p2 >> bit) << 2) & 4));
                        val = (val << 4) | pix;
                    }
                    conv[j] = val;
                }
                ok = write_all(out, (uint8_t*)conv, 0x4000);
            }
        }
        else if (s == SEC_SPRITES) {
            // Interleave-4: byte k of each 4-byte group comes from file k. hwsprites::init() then
            // builds (d0<<24|d1<<16|d2<<8|d3) from bytes 3,2,1,0, which on a little-endian CPU is
            // the same bytes in the same order: the interleaved data IS the converted format.
            for (uint32_t base = 0; base < 0x100000 && ok; base += 0x80000) {
                int f[4] = { -1, -1, -1, -1 };
                for (int i = 0; i < NROMS; i++)
                    if (ROMS[i].region == SEC_SPRITES && (ROMS[i].offset & ~3u) == base) f[ROMS[i].offset & 3] = i;
                FILE* in[4] = { 0, 0, 0, 0 };
                for (int k = 0; k < 4; k++) if (f[k] < 0 || !(in[k] = fopen(g_path[f[k]], "rb"))) ok = false;
                for (uint32_t done = 0; done < 0x20000 && ok; done += 0x1000) {
                    uint8_t* o = scratch;
                    for (int k = 0; k < 4 && ok; k++) ok = fread(scratch + 0x10000 + k * 0x1000, 1, 0x1000, in[k]) == 0x1000;
                    for (uint32_t j = 0; j < 0x1000 && ok; j++)
                        for (int k = 0; k < 4; k++) *o++ = scratch[0x10000 + k * 0x1000 + j];
                    ok = ok && write_all(out, scratch, 0x4000);
                    progress((step - 1) * 100 / SEC_COUNT + (int)((base + done * 4) * 100 / 0x100000) / SEC_COUNT);
                }
                for (int k = 0; k < 4; k++) if (in[k]) fclose(in[k]);
            }
        }
    }
    // Pad to the full size, then validate the file by writing the magic
    if (ok) { fseek(out, 0, SEEK_END); long end = ftell(out); if (end < (long)h.total_size) { memset(scratch, 0, 0x1000); ok = write_all(out, scratch, h.total_size - end); } }
    if (ok) { h.magic = CB_DAT_MAGIC; fseek(out, 0, SEEK_SET); ok = fwrite(&h, 1, sizeof h, out) == sizeof h; }
    fclose(out);
    progress(100);
    if (!ok) { remove(CB_DATA_FILE); return "Error writing " CB_DATA_FILE " (SD card full?)"; }
    return NULL;
}
