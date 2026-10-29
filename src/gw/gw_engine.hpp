// Interface between the firmware side (main_cannonball.c) and the Cannonball side
// (gw_engine.cpp). Kept free of both projects' headers: they define clashing names.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum {
    CB_BTN_LEFT  = 1 << 0, CB_BTN_RIGHT = 1 << 1, CB_BTN_UP    = 1 << 2, CB_BTN_DOWN = 1 << 3,
    CB_BTN_ACCEL = 1 << 4, CB_BTN_BRAKE = 1 << 5, CB_BTN_GEAR_LOW = 1 << 6, CB_BTN_GEAR_HIGH = 1 << 7,
    CB_BTN_START = 1 << 8, CB_BTN_COIN  = 1 << 9,
};

#define CB_FPS          30
#define CB_SAMPLE_RATE  22050
#define CB_SAMPLES      (CB_SAMPLE_RATE / CB_FPS)   // 735 per frame
#define CB_LCD_YOFF     8                           // 224 game lines centred on the 240-line LCD

#ifdef __cplusplus
extern "C" {
#endif

// RAM the engine owns but does not need until cb_engine_init(): lent to the ROM converter.
uint8_t* cb_engine_scratch(size_t* size);

// dat = outrun.dat mapped in external flash (checked by the caller). False if it cannot start.
bool cb_engine_init(const uint8_t* dat);
void cb_engine_set_buttons(uint32_t buttons);
void cb_engine_set_auto_gears(bool on);
bool cb_engine_get_auto_gears(void);
void cb_engine_tick(void);                     // game logic + sound chips, one 30 fps frame
void cb_engine_render(uint16_t* lcd);          // draws the frame into a 320x240 RGB565 LCD buffer
void cb_engine_redraw(uint16_t* lcd);          // last frame again (pause menu background)
void cb_engine_audio(int16_t* out, int n, int volume);   // mono, volume 0..255

// Front-end menu (gw_menu.cpp)
void cb_engine_set_lang(const char* code);     // Retro-Go language code ("es_es"...)
void cb_engine_go_menu(void);                  // back to the menu (from the pause menu)
bool cb_engine_exit_requested(void);           // EXIT chosen in the menu (cleared on read)
// time / traffic: 0 easy, 1 normal, 2 hard, 3 hardest, 4 infinite time / no traffic
void cb_engine_set_settings(int auto_gears, int time, int traffic, int steer);
bool cb_engine_take_settings(int* auto_gears, int* time, int* traffic, int* steer);  // true if changed
void cb_engine_set_mode_settings(int cont_traffic, int laps, int tt_traffic);
bool cb_engine_take_mode_settings(int* cont_traffic, int* laps, int* tt_traffic);  // true if changed

#ifdef __cplusplus
}
#endif
