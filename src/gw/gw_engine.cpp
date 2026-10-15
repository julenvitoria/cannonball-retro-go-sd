// Cannonball engine glue for the Game & Watch: everything main.cpp and the SDL layer did,
// minus SDL. The engine itself (external/cannonball) is used as is.
#include <string.h>
#include <iostream>
#include "gw_engine.hpp"
#include "gw_port.hpp"

#include "main.hpp"
#include "video.hpp"
#include "roms.hpp"
#include "trackloader.hpp"
#include "engine/outrun.hpp"
#include "engine/oinputs.hpp"
#include "engine/ooutputs.hpp"
#include "engine/oroad.hpp"
#include "engine/audio/osoundint.hpp"
#include "frontend/config.hpp"
#include "sdl2/input.hpp"
#include "sdl2/rendersurface.hpp"

// <iostream> stand-in (see shim/iostream)
namespace std { gw_null_ostream cout, cerr; }

// ------------------------------------------------------------------------------------------------
// Globals normally defined in main.cpp
// ------------------------------------------------------------------------------------------------
namespace cannonball {
    int    state       = STATE_BOOT;
    double frame_ms    = 0;
    int    frame       = 0;
    bool   tick_frame  = true;
    int    fps_counter = 0;
    Audio  audio;
}
using namespace cannonball;

// ------------------------------------------------------------------------------------------------
// Memory: the decoded road (257 KB) is the only large buffer that has to be in RAM (tiles and
// sprites stay in flash). Before the engine starts, the ROM converter borrows it.
// ------------------------------------------------------------------------------------------------
static uint8_t g_road[0x40200] __attribute__((aligned(4)));

uint8_t* cb_engine_scratch(size_t* size) { *size = sizeof g_road; return g_road; }

// outrun.dat as mapped by the firmware (roms_gw.cpp points the RomLoaders into it)
const uint8_t* g_cb_dat;

// Friend of hwtiles/hwsprites/HWRoad (added by tools/prepare_cannonball.py)
struct GwPort
{
    static void set(const uint8_t* dat)
    {
        const cb_dat_header_t* h = (const cb_dat_header_t*)dat;
        video.tile_layer->tiles     = (uint32_t*)(dat + h->offset[SEC_TILES]);
        video.sprite_layer->sprites = (uint32_t*)(dat + h->offset[SEC_SPRITES]);
        hwroad.roads = g_road;
        // Decode the road here: Video::init() would decode it too, but then delete[] the source,
        // which is flash. With road.rom = NULL it only sets up the road renderer.
        hwroad.decode_road(dat + h->offset[SEC_ROAD]);
        roms.road.rom = NULL;
    }
};

// ------------------------------------------------------------------------------------------------
// Fast memory for the hot engine objects (see gw_alloc.hpp, used by video.cpp for hwtiles)
// ------------------------------------------------------------------------------------------------
#include "gw_alloc.hpp"
extern "C" void* dtc_calloc(size_t count, size_t size);
extern "C" void* heap_alloc_mem(size_t size);
void* gw_fast_alloc(size_t size)
{
    void* p = dtc_calloc(1, size);
    return p ? p : heap_alloc_mem(size);
}

// ------------------------------------------------------------------------------------------------
// Input (sdl2/input.cpp semantics, keys set from the G&W buttons once per frame)
// ------------------------------------------------------------------------------------------------
Input input;
Input::Input(void)
{
    memset(keys, 0, sizeof keys); memset(keys_old, 0, sizeof keys_old);
    gamepad = false; rumble_supported = false; analog = 0; key_press = -1; joy_button = -1;
    wheel = a_wheel = CENTRE; a_accel = a_brake = 0; a_motor = 0;
}
Input::~Input(void) {}
void Input::init(int, int*, int*, const int, int*, bool*, int*) {}
void Input::open_joy() {}
void Input::close_joy() {}
bool Input::has_pressed(presses p)      { return keys[p] && !keys_old[p]; }
bool Input::is_pressed(presses p)       { return keys[p]; }
bool Input::is_pressed_clear(presses p) { bool b = keys[p]; keys[p] = false; return b; }
void Input::frame_done()                { memcpy(&keys_old, &keys, sizeof(keys)); }
void Input::reset_axis_config()         {}
int  Input::get_axis_config()           { return -1; }
void Input::set_rumble(bool, float)     {}

void cb_engine_set_buttons(uint32_t b)
{
    input.keys[Input::LEFT]  = (b & CB_BTN_LEFT)  != 0;
    input.keys[Input::RIGHT] = (b & CB_BTN_RIGHT) != 0;
    input.keys[Input::UP]    = (b & CB_BTN_UP)    != 0;
    input.keys[Input::DOWN]  = (b & CB_BTN_DOWN)  != 0;
    input.keys[Input::ACCEL] = (b & CB_BTN_ACCEL) != 0;
    input.keys[Input::BRAKE] = (b & CB_BTN_BRAKE) != 0;
    input.keys[Input::GEAR1] = (b & CB_BTN_GEAR_LOW)  != 0;   // GEAR_SEPARATE: GEAR1 = low
    input.keys[Input::GEAR2] = (b & CB_BTN_GEAR_HIGH) != 0;   //                GEAR2 = high
    input.keys[Input::START] = (b & CB_BTN_START) != 0;
    input.keys[Input::COIN]  = (b & CB_BTN_COIN)  != 0;
}

void cb_engine_set_auto_gears(bool on)
{
    config.controls.gear = on ? config.controls.GEAR_AUTO : config.controls.GEAR_SEPARATE;
}
bool cb_engine_get_auto_gears(void) { return config.controls.gear == config.controls.GEAR_AUTO; }

// ------------------------------------------------------------------------------------------------
// Audio: the real PCM + YM2151 chips, mixed to mono for the G&W speaker
// ------------------------------------------------------------------------------------------------
Audio::Audio()  { sound_enabled = false; mix_buffer = NULL; }
Audio::~Audio() {}
void   Audio::init()         { if (config.sound.enabled) start_audio(); }
void   Audio::tick()         {}
void   Audio::start_audio()  { sound_enabled = true; }
void   Audio::stop_audio()   { sound_enabled = false; }
double Audio::adjust_speed() { return 1.0; }
void   Audio::load_wav(const char*) {}
void   Audio::clear_wav()    {}

void cb_engine_audio(int16_t* out, int n, int volume)
{
    osoundint.pcm->stream_update();
    osoundint.ym->stream_update();
    const int16_t* a = osoundint.pcm->get_buffer();
    const int16_t* b = osoundint.ym->get_buffer();
    int frames = (int)(osoundint.pcm->buffer_size / 2);       // stereo interleaved
    if (frames > n) frames = n;
    for (int i = 0; i < frames; i++)
    {
        // (L + R) / 2 of the sum of both chips, as sdl2/audio.cpp mixes them, then volume
        int32_t m = (a[2 * i] + a[2 * i + 1] + b[2 * i] + b[2 * i + 1]) / 2;
        m = (m * volume) >> 8;
        if (m > 32767) m = 32767; else if (m < -32768) m = -32768;
        out[i] = (int16_t)m;
    }
    for (int i = frames; i < n; i++) out[i] = frames ? out[frames - 1] : 0;
}

// ------------------------------------------------------------------------------------------------
// Renderer: palette -> RGB565 table (renderbase.cpp's maths), frame -> LCD buffer
// ------------------------------------------------------------------------------------------------
// The colour table is RenderBase::rgb[] (uint32_t, 2 x 4096 entries incl. shadows), holding RGB565.
static uint16_t* g_target;

RenderBase::RenderBase() { surface = NULL; screen_pixels = NULL; orig_width = orig_height = 0; shadow_multi = 160; }
bool RenderBase::sdl_screen_size() { return true; }
void RenderBase::set_shadow_intensity(float f) { shadow_multi = (int)(255.0f * f + 0.5f); }
void RenderBase::convert_palette(uint32_t adr, uint32_t r1, uint32_t g1, uint32_t b1)
{
    adr >>= 1;
    // Normal: 5-bit components (r1 * 8 as 8-bit) -> RGB565
    rgb[adr] = (uint16_t)((r1 << 11) | ((g1 << 1) << 5) | b1);
    // Shadow: r1 * shadow_multi / 31 as 8-bit -> RGB565
    uint32_t r = r1 * shadow_multi / 31, g = g1 * shadow_multi / 31, b = b1 * shadow_multi / 31;
    rgb[adr + S16_PALETTE_ENTRIES] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
Render::Render() {}
Render::~Render() {}
bool Render::init(int sw, int sh, int, int, int) { src_width = sw; src_height = sh; return true; }
void Render::disable() {}
bool Render::start_frame() { return true; }
bool Render::finalize_frame() { return true; }
void Render::draw_frame(uint16_t* px)
{
    if (!g_target) return;
    uint16_t* d = g_target + CB_LCD_YOFF * S16_WIDTH;
    for (int i = 0; i < S16_WIDTH * S16_HEIGHT; i++)
        d[i] = (uint16_t)rgb[px[i] & (S16_PALETTE_ENTRIES * 2 - 1)];
}

// ------------------------------------------------------------------------------------------------
// main.cpp's tick(), minus SDL events and rumble (the menu is gw_menu.cpp)
// ------------------------------------------------------------------------------------------------
static bool pause_engine = false;
void gw_menu_state_tick(void);

void cb_engine_tick(void)
{
    frame++;
    if (config.fps == 60)       tick_frame = frame & 1;
    else if (config.fps == 120) tick_frame = (frame & 3) == 1;

    if (tick_frame) { oinputs.tick(); oinputs.do_gear(); }

    switch (state)
    {
        case STATE_GAME:
            if (!pause_engine)
            {
                outrun.tick(tick_frame);
                if (tick_frame) input.frame_done();
                osoundint.tick();
            }
            else if (tick_frame) input.frame_done();
            break;
        case STATE_INIT_GAME:
            tick_frame = true; pause_engine = false;
            outrun.init();
            state = STATE_GAME;
            break;
        case STATE_INIT_MENU:
        case STATE_MENU:
            gw_menu_state_tick();       // gw_menu.cpp
            break;
        default:
            break;
    }
    outrun.outputs->writeDigitalToConsole();
}

void cb_engine_render(uint16_t* lcd)
{
    g_target = lcd;
    video.prepare_frame();
    video.render_frame();
}

void cb_engine_redraw(uint16_t* lcd)
{
    g_target = lcd;
    video.render_frame();
}

bool cb_engine_init(const uint8_t* dat)
{
    g_cb_dat = dat;
    config.load();
    if (!roms.load_revb_roms(false)) return false;
    GwPort::set(dat);
    config.set_fps(config.video.fps);                // also initialises the sound chips
    if (!video.init(&roms, &config.video)) return false;   // decodes the road into g_road
    audio.init();
    state = STATE_INIT_MENU;           // start on the front-end menu, as Cannonball does
    return true;
}
