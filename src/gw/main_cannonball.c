/*
 * Cannonball (OutRun engine) — Retro-Go SD GWHB homebrew for the Game & Watch.
 *
 * Firmware side: start-up, ROM conversion, input, frame pacing, audio, pause menu.
 * The game itself is Cannonball (external/cannonball), driven through gw_engine.hpp.
 *
 * SD card:
 *   /homebrews/Cannonball.bin       this program
 *   /homebrews/cannonball/          the OutRun rev. B ROM files (unzipped outrun.zip), and:
 *   /homebrews/cannonball/outrun.dat     built from them on the first launch (~2.3 MB)
 *   /homebrews/cannonball/hiscores*.bin  high scores
 *
 * Controls: D-pad left/right steer, A accelerate, B brake, up/down gear high/low,
 * GAME start, TIME coin. Starts on Cannonball's menu (play, settings, exit).
 * PAUSE/SET opens the Retro-Go menu ("Cannonball menu" goes back to the game's menu).
 *
 * Entry (run_gwhb_homebrew):
 *   void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
 */
#include <odroid_system.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "gw_lcd.h"
#include "gw_malloc.h"
#include "gw_audio.h"
#include "common.h"
#include "appid.h"
#include "odroid_overlay.h"
#include "rg_storage.h"

#include "gw_engine.hpp"
#include "gw_port.hpp"

#include "gw_core_bridge.h"
#include "gw_core_i18n.h"
#include "gw_firmware_abi.h"

static odroid_gamepad_state_t joystick;
static int16_t g_mix[CB_SAMPLES];

// ------------------------------------------------------------------------------------------------
// Messages before the game runs
// ------------------------------------------------------------------------------------------------
static void show_error(const char *line1, const char *line2, const char *line3)
{
    odroid_dialog_choice_t choices[] = {
        {0, line1, "", -1, NULL},
        {0, line2 ? line2 : "", "", -1, NULL},
        {0, line3 ? line3 : "", "", -1, NULL},
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {1, "OK", "", 1, NULL},
        ODROID_DIALOG_CHOICE_LAST,
    };
    lcd_clear_buffers();
    odroid_overlay_dialog("Cannonball", choices, 4, NULL, 0);
}

static void convert_progress(const char *what, int percent)
{
    wdog_refresh();
    if (lcd_is_swap_pending())
        return;
    odroid_overlay_draw_progress_bar(what, (uint8_t)(percent < 0 ? 0 : percent > 100 ? 100 : percent));
    lcd_swap();
}

// Size of outrun.dat on the SD card, and whether its header is valid
static bool dat_file_ok(void)
{
    FILE *f = fopen(CB_DATA_FILE, "rb");
    if (!f)
        return false;
    cb_dat_header_t h;
    bool ok = fread(&h, 1, sizeof h, f) == sizeof h;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return ok && size > 0 && cb_dat_header_ok(&h, (uint32_t)size);
}

// outrun.dat mapped in external flash, building it first if needed. NULL if the game cannot run.
static const uint8_t *prepare_data(void)
{
    rg_storage_mkdir(CB_DIR);

    if (!dat_file_ok())
    {
        if (!cb_roms_present())
        {
            show_error("OutRun ROMs not found.", "Unzip outrun.zip (rev. B)", "into /homebrews/cannonball/");
            return NULL;
        }
        size_t scratch_size;
        uint8_t *scratch = cb_engine_scratch(&scratch_size);
        lcd_clear_buffers();
        const char *err = cb_convert_roms(scratch, scratch_size, convert_progress);
        if (err)
        {
            show_error(err, "Needs the OutRun rev. B set", "in /homebrews/cannonball/");
            return NULL;
        }
    }

    const uint8_t *dat = NULL;
    for (int attempt = 0; attempt < 2 && !dat; attempt++)
    {
        uint32_t size = 0;   // in/out: 0 = take the file's size
        dat = odroid_overlay_cache_file_in_flash(CB_DATA_FILE, &size, false);
        if (dat && !cb_dat_header_ok((const cb_dat_header_t *)dat, size))
        {
            // The firmware keeps its flash-cache index on the SD card, keyed by path, date and
            // size. If the flash no longer holds what the index says (external flash erased or
            // rewritten), it serves stale data. Rewriting the header unchanged gives the file a
            // new date, hence a new key, so the next call copies it to flash again.
            printf("Cannonball: stale flash copy of outrun.dat, re-caching\n");
            dat = NULL;
            FILE *f = fopen(CB_DATA_FILE, "r+b");
            cb_dat_header_t h;
            if (f && fread(&h, 1, sizeof h, f) == sizeof h)
            {
                fseek(f, 0, SEEK_SET);
                fwrite(&h, 1, sizeof h, f);
            }
            if (f)
                fclose(f);
        }
    }
    if (!dat)
    {
        show_error("Cannot load outrun.dat", "Delete it to rebuild it", "from the ROMs.");
        return NULL;
    }
    return dat;
}

// ------------------------------------------------------------------------------------------------
// Pause menu
// ------------------------------------------------------------------------------------------------
static const gw_i18n_entry_t i18n_game_menu[] = {
    { "en", "Cannonball menu" }, { "es", "Menú de Cannonball" }, { "pt", "Menu do Cannonball" },
    { "fr", "Menu de Cannonball" }, { "de", "Cannonball-Menü" }, { "it", "Menu di Cannonball" },
    { "no", "Cannonball-meny" },
    GW_I18N_END
};

// "Cannonball menu" in the pause menu: back to the game's front-end menu
static bool game_menu_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    (void)option; (void)repeat;
    if (event == ODROID_DIALOG_ENTER) {
        cb_engine_go_menu();
        return true;                       // close the pause menu
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// Settings: kept by the firmware with the homebrew's other settings (app_int32 keys)
// ------------------------------------------------------------------------------------------------
int32_t odroid_settings_app_int32_get(const char *key, int32_t default_value);
void    odroid_settings_app_int32_set(const char *key, int32_t value);

static void settings_load(void)
{
    cb_engine_set_settings(odroid_settings_app_int32_get("cb_autogear", 0),
                           odroid_settings_app_int32_get("cb_time", 0),
                           odroid_settings_app_int32_get("cb_traffic", 1));
}

static void settings_store_if_changed(void)
{
    int gears, time, traffic;
    if (!cb_engine_take_settings(&gears, &time, &traffic))
        return;
    odroid_settings_app_int32_set("cb_autogear", gears);
    odroid_settings_app_int32_set("cb_time", time);
    odroid_settings_app_int32_set("cb_traffic", traffic);
}

static void repaint(void)
{
    cb_engine_redraw(lcd_get_active_buffer());
}

static void *screenshot(void)
{
    lcd_wait_for_vblank();
    cb_engine_redraw(lcd_get_active_buffer());
    return lcd_get_active_buffer();
}

// ------------------------------------------------------------------------------------------------
// Frame
// ------------------------------------------------------------------------------------------------
static uint32_t read_buttons(const odroid_gamepad_state_t *j)
{
    // While PAUSE/SET is held the firmware uses the other buttons for its shortcuts
    if (j->values[ODROID_INPUT_VOLUME])
        return 0;
    uint32_t b = 0;
    if (j->values[ODROID_INPUT_LEFT])   b |= CB_BTN_LEFT;
    if (j->values[ODROID_INPUT_RIGHT])  b |= CB_BTN_RIGHT;
    if (j->values[ODROID_INPUT_UP])     b |= CB_BTN_UP | CB_BTN_GEAR_HIGH;
    if (j->values[ODROID_INPUT_DOWN])   b |= CB_BTN_DOWN | CB_BTN_GEAR_LOW;
    if (j->values[ODROID_INPUT_A])      b |= CB_BTN_ACCEL;
    if (j->values[ODROID_INPUT_B])      b |= CB_BTN_BRAKE;
    if (j->values[ODROID_INPUT_START] || j->values[ODROID_INPUT_X]) b |= CB_BTN_START;   // GAME
    if (j->values[ODROID_INPUT_SELECT] || j->values[ODROID_INPUT_Y]) b |= CB_BTN_COIN;   // TIME
    return b;
}

static void clear_borders(uint16_t *lcd)
{
    // Rows above and below the 224-line picture (the in-game overlay may have drawn there)
    memset(lcd, 0, CB_LCD_YOFF * GW_LCD_WIDTH * sizeof(uint16_t));
    memset(lcd + (CB_LCD_YOFF + 224) * GW_LCD_WIDTH, 0,
           (GW_LCD_HEIGHT - CB_LCD_YOFF - 224) * GW_LCD_WIDTH * sizeof(uint16_t));
}

static void submit_audio(void)
{
    if (common_emu_sound_loop_is_muted())
        return;
    int16_t *buf = audio_get_active_buffer();
    int n = audio_get_buffer_length();
    if (n > CB_SAMPLES)
        n = CB_SAMPLES;
    memcpy(buf, g_mix, n * sizeof(int16_t));
}

void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    (void)load_state;
    (void)save_slot;

    // The launcher seeds ram_start past code+bss; keep the bump allocator past our BSS.
    ram_start = (uint32_t)(uintptr_t)&__CORE_BSS_END__;

    odroid_system_init(APPID_HOMEBREW, CB_SAMPLE_RATE);
    odroid_system_emu_init(NULL, NULL, &screenshot, NULL, NULL, NULL, NULL);
    lcd_clear_buffers();

    const uint8_t *dat = prepare_data();
    if (!dat || !cb_engine_init(dat))
    {
        if (dat)
            show_error("Cannonball could not start", "(not enough memory?)", NULL);
        odroid_system_switch_app(0);
        return;
    }

    settings_load();
#ifdef CB_TEST_LANG     /* emulator tests: force a menu language, e.g. -DCB_TEST_LANG=\"es_es\" */
    cb_engine_set_lang(CB_TEST_LANG);
#else
    cb_engine_set_lang(gw_firmware_abi()->i18n_lang_code());
#endif

    printf("Cannonball: running, RAM_EMU free %u, DTCM free %u\n",
           (unsigned)ram_get_free_size(), (unsigned)dtc_get_free_size());

    if (start_paused)
    {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    }
    else
        common_emu_state.pause_after_frames = 0;
    common_emu_state.frame_time_10us = (uint16_t)(100000 / CB_FPS);

    lcd_clear_buffers();
    memset(g_mix, 0, sizeof g_mix);
    lcd_wait_for_vblank();
    audio_start_playing(CB_SAMPLES);

    while (true)
    {
        wdog_refresh();
        odroid_input_read_gamepad(&joystick);

        odroid_dialog_choice_t options[] = {
            {300, gw_i18n(i18n_game_menu), "", 1, &game_menu_cb},
            ODROID_DIALOG_CHOICE_LAST
        };
        common_emu_input_loop(&joystick, options, &repaint);

        cb_engine_set_buttons(read_buttons(&joystick));

        bool draw = common_emu_frame_loop();

        cb_engine_tick();
        settings_store_if_changed();
        if (cb_engine_exit_requested())
            odroid_system_switch_app(0);    // EXIT in the game's menu: back to Retro-Go
        cb_engine_audio(g_mix, CB_SAMPLES, common_emu_sound_get_volume());

        if (draw)
        {
            uint16_t *lcd = lcd_get_active_buffer();
            cb_engine_render(lcd);
            clear_borders(lcd);
            common_ingame_overlay();
            lcd_swap();
        }

        submit_audio();
        common_emu_sound_sync(false);
    }
}
