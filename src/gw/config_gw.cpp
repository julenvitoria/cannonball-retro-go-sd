// Game & Watch port: replaces frontend/config.cpp (XML via boost::property_tree is far too big).
// Settings are fixed G&W defaults, plus the few the port exposes in the Retro-Go pause menu
// (main_cannonball.cpp). Hi-scores are saved as small binary files in CB_DIR.
#include <stdio.h>
#include <string.h>
#include "frontend/config.hpp"
#include "engine/ohiscore.hpp"
#include "engine/outils.hpp"
#include "engine/audio/osoundint.hpp"
#include "main.hpp"
#include "gw_port.hpp"

Config config;

Config::Config(void)
{
    music_t magical, breeze, splash;
    magical.title = "MAGICAL SOUND SHOWER";
    breeze.title  = "PASSING BREEZE";
    splash.title  = "SPLASH WAVE";
    magical.type  = breeze.type = splash.type = music_t::IS_YM_INT;
    magical.cmd   = sound::MUSIC_MAGICAL;
    breeze.cmd    = sound::MUSIC_BREEZE;
    splash.cmd    = sound::MUSIC_SPLASH;
    sound.music.push_back(magical);
    sound.music.push_back(breeze);
    sound.music.push_back(splash);
}
Config::~Config(void) {}
void Config::set_config_file(const std::string&) {}
bool Config::save() { return true; }

void Config::load()
{
    data.crc32 = 0;
    menu.enabled = 0;
    menu.road_scroll_speed = 50;

    video.mode = video.MODE_WINDOW; video.scale = 1; video.scanlines = 0;
    video.fps = 0;            // 30 fps: the original game's rate (measured on the G&W: ~19 ms/frame at 280 MHz)
    video.fps_count = 0; video.widescreen = 0; video.hires = 0; video.filtering = 0; video.vsync = 0;
    video.shadow = 0;         // original shadow intensity

    sound.enabled = 1;
    sound.rate = 22050;       // SegaPCM + YM2151 at 22 kHz: ~5 ms/frame on the G&W
    sound.advertise = 1;      // attract-mode sound, like the arcade
    sound.preview = 1;
    sound.fix_samples = 0;    // needs the optional patched opr-10188.71f
    sound.music_timer = MUSIC_TIMER;

    smartypi.enabled = 0; smartypi.ouputs = 0; smartypi.cabinet = CABINET_UPRIGHT;

    controls.gear = controls.GEAR_SEPARATE;   // D-pad up/down = high/low (pause menu: automatic)
    controls.steer_speed = 3;
    controls.pedal_speed = 4;
    controls.analog = 0;
    controls.pad_id = 0;
    for (int i = 0; i < 4; i++) controls.axis[i] = -1;
    controls.asettings[0] = 75; controls.asettings[1] = 0;
    controls.invert[0] = controls.invert[1] = controls.invert[2] = false;
    controls.rumble = 0; controls.haptic = 0;
    controls.max_force = controls.min_force = controls.force_duration = 0;
    for (int i = 0; i < 15; i++) controls.padconfig[i] = -1;
    for (int i = 0; i < 12; i++) controls.keyconfig[i] = -1;

    engine.dip_time = 0;      // arcade default
    engine.dip_traffic = 1;
    engine.freeze_timer = false;
    engine.disable_traffic = false;
    engine.freeplay = true;   // no coin slot: GAME starts a race (TIME still inserts a coin)
    engine.jap = 0;
    engine.prototype = 0;
    engine.level_objects = 1;
    engine.randomgen = 1;
    engine.fix_bugs_backup = engine.fix_bugs = true;
    engine.fix_timer = false;
    engine.layout_debug = false;
    engine.hiscore_delete = true;
    engine.hiscore_timer = HIGHSCORE_TIMER;
    engine.new_attract = 1;
    engine.grippy_tyres = engine.offroad = engine.bumper = engine.turbo = false;
    engine.car_pal = 0;

    ttrial.laps = 5; ttrial.traffic = 3;
    cont_traffic = 3;
}

// ------------------------------------------------------------------------------------------------
// Hi-scores: raw score_entry arrays with a small header
// ------------------------------------------------------------------------------------------------
#define SCORES_MAGIC 0x53434243u   // "CBCS"
struct scores_hdr_t { uint32_t magic, count, entry_size; };

static const char* scores_path(bool original_mode)
{
    return original_mode ? CB_DIR "/hiscores.bin" : CB_DIR "/hiscores_cont.bin";
}

void Config::load_scores(bool original_mode)
{
    FILE* f = fopen(scores_path(original_mode), "rb");
    if (!f) return;                       // keep the defaults ohiscore set up
    scores_hdr_t h;
    score_entry tmp[OHiScore::NO_SCORES];
    if (fread(&h, 1, sizeof h, f) == sizeof h && h.magic == SCORES_MAGIC &&
        h.count == OHiScore::NO_SCORES && h.entry_size == sizeof(score_entry) &&
        fread(tmp, 1, sizeof tmp, f) == sizeof tmp)
        memcpy(ohiscore.scores, tmp, sizeof tmp);
    fclose(f);
}

void Config::save_scores(bool original_mode)
{
    FILE* f = fopen(scores_path(original_mode), "wb");
    if (!f) return;
    scores_hdr_t h = { SCORES_MAGIC, OHiScore::NO_SCORES, sizeof(score_entry) };
    fwrite(&h, 1, sizeof h, f);
    fwrite(ohiscore.scores, 1, sizeof ohiscore.scores, f);
    fclose(f);
}

void Config::load_tiletrial_scores()
{
    static const uint16_t COUNTER_1M_15 = 0x11D0;   // 1m 15s 0ms, as the original
    for (int i = 0; i < 15; i++) ttrial.best_times[i] = COUNTER_1M_15;
    FILE* f = fopen(CB_DIR "/hiscores_tt.bin", "rb");
    if (!f) return;
    uint16_t t[15];
    if (fread(t, 1, sizeof t, f) == sizeof t) memcpy(ttrial.best_times, t, sizeof t);
    fclose(f);
}

void Config::save_tiletrial_scores()
{
    FILE* f = fopen(CB_DIR "/hiscores_tt.bin", "wb");
    if (!f) return;
    fwrite(ttrial.best_times, 1, sizeof ttrial.best_times, f);
    fclose(f);
}

bool Config::clear_scores()
{
    ohiscore.init_def_scores();
    remove(CB_DIR "/hiscores.bin");
    remove(CB_DIR "/hiscores_cont.bin");
    remove(CB_DIR "/hiscores_tt.bin");
    return true;
}

void Config::set_fps(int fps)
{
    video.fps = fps;
    this->fps = video.fps == 0 ? 30 : 60;
    tick_fps  = video.fps < 2 ? 30 : 60;
    cannonball::frame_ms = 1000.0 / this->fps;
    if (sound.enabled) cannonball::audio.stop_audio();
    osoundint.init();
    if (sound.enabled) cannonball::audio.start_audio();
}

void Config::inc_time()
{
    if (engine.dip_time == 3) { if (!engine.freeze_timer) engine.freeze_timer = 1; else { engine.dip_time = 0; engine.freeze_timer = 0; } }
    else engine.dip_time++;
}

void Config::inc_traffic()
{
    if (engine.dip_traffic == 3) { if (!engine.disable_traffic) engine.disable_traffic = 1; else { engine.dip_traffic = 0; engine.disable_traffic = 0; } }
    else engine.dip_traffic++;
}
