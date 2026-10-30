// Cannonball's front-end menu, rebuilt for the Game & Watch.
//
// Same look as frontend/menu.cpp (OutRun logo, scrolling road, green text in the game's font,
// minicar cursor) but without std::vector/std::string/boost, and with only the options that make
// sense here: PLAY GAME, GAME MODES (continuous mode, time trial), SETTINGS (gears, time, traffic,
// steering) and EXIT. Labels follow the Retro-Go
// language for the Latin-alphabet languages (the OutRun font only has A-Z, 0-9 and a few
// symbols, so no accents); anything else falls back to English.
#include <string.h>
#include "gw_engine.hpp"

#include "main.hpp"
#include "video.hpp"
#include "roms.hpp"
#include "globals.hpp"
#include "engine/outrun.hpp"
#include "engine/ohud.hpp"
#include "engine/oinputs.hpp"
#include "engine/ooutputs.hpp"
#include "engine/oinitengine.hpp"
#include "engine/ologo.hpp"
#include "engine/opalette.hpp"
#include "engine/oroad.hpp"
#include "engine/osprites.hpp"
#include "engine/otiles.hpp"
#include "engine/audio/osoundint.hpp"
#include "frontend/config.hpp"
#include "frontend/ttrial.hpp"
#include "sdl2/input.hpp"

// ------------------------------------------------------------------------------------------------
// Text
// ------------------------------------------------------------------------------------------------
enum {
    S_PLAY, S_SETTINGS, S_EXIT, S_GEARS, S_MANUAL, S_AUTO, S_TIME, S_TRAFFIC,
    S_EASY, S_NORMAL, S_HARD, S_HARDEST, S_INFINITE, S_NONE, S_BACK, S_STEER,
    S_MODES, S_CONT, S_TTRIAL, S_START, S_LAPS, S_COUNT
};

struct lang_t { const char* code; const char* s[S_COUNT]; };

static const lang_t LANGS[] = {
    { "en", { "PLAY GAME", "SETTINGS", "EXIT", "GEARS", "MANUAL", "AUTOMATIC", "TIME", "TRAFFIC",
              "EASY", "NORMAL", "HARD", "HARDEST", "INFINITE", "NONE", "BACK", "STEERING",
              "GAME MODES", "CONTINUOUS MODE", "TIME TRIAL", "START", "LAPS" } },
    { "es", { "JUGAR", "AJUSTES", "SALIR", "MARCHAS", "MANUAL", "AUTOMATICO", "TIEMPO", "TRAFICO",
              "FACIL", "NORMAL", "DIFICIL", "MUY DIFICIL", "INFINITO", "SIN TRAFICO", "VOLVER", "DIRECCION",
              "MODOS DE JUEGO", "MODO CONTINUO", "CONTRARRELOJ", "EMPEZAR", "VUELTAS" } },
    { "pt", { "JOGAR", "DEFINICOES", "SAIR", "MUDANCAS", "MANUAL", "AUTOMATICO", "TEMPO", "TRANSITO",
              "FACIL", "NORMAL", "DIFICIL", "MUITO DIFICIL", "INFINITO", "SEM TRANSITO", "VOLTAR", "DIRECAO",
              "MODOS DE JOGO", "MODO CONTINUO", "CONTRA RELOGIO", "COMECAR", "VOLTAS" } },
    { "fr", { "JOUER", "PARAMETRES", "QUITTER", "VITESSES", "MANUELLES", "AUTOMATIQUES", "TEMPS", "TRAFIC",
              "FACILE", "NORMAL", "DIFFICILE", "TRES DIFFICILE", "INFINI", "AUCUN", "RETOUR", "DIRECTION",
              "MODES DE JEU", "MODE CONTINU", "CONTRE LA MONTRE", "COMMENCER", "TOURS" } },
    { "it", { "GIOCA", "IMPOSTAZIONI", "ESCI", "CAMBIO", "MANUALE", "AUTOMATICO", "TEMPO", "TRAFFICO",
              "FACILE", "NORMALE", "DIFFICILE", "DIFFICILISSIMO", "INFINITO", "NESSUNO", "INDIETRO", "STERZO",
              "MODALITA", "MODALITA CONTINUA", "CONTRO IL TEMPO", "INIZIA", "GIRI" } },
    { "de", { "SPIELEN", "EINSTELLUNGEN", "BEENDEN", "SCHALTUNG", "MANUELL", "AUTOMATIK", "ZEIT", "VERKEHR",
              "LEICHT", "NORMAL", "SCHWER", "SEHR SCHWER", "UNENDLICH", "KEINER", "ZURUECK", "LENKUNG",
              "SPIELMODI", "ENDLOSMODUS", "ZEITFAHREN", "STARTEN", "RUNDEN" } },
    { "no", { "SPILL", "INNSTILLINGER", "AVSLUTT", "GIR", "MANUELL", "AUTOMATISK", "TID", "TRAFIKK",
              "LETT", "NORMAL", "VANSKELIG", "SVAERT VANSKELIG", "UENDELIG", "INGEN", "TILBAKE", "STYRING",
              "SPILLMODUS", "KONTINUERLIG", "TIDSKJORING", "START", "RUNDER" } },
};

static const lang_t* g_lang = &LANGS[0];

void cb_engine_set_lang(const char* code)
{
    g_lang = &LANGS[0];
    if (!code) return;
    for (size_t i = 0; i < sizeof LANGS / sizeof LANGS[0]; i++)
        if (strncmp(code, LANGS[i].code, 2) == 0) { g_lang = &LANGS[i]; return; }
}

static const char* T(int id) { return g_lang->s[id]; }

// ------------------------------------------------------------------------------------------------
// Settings (stored by the firmware side; see main_cannonball.c)
// ------------------------------------------------------------------------------------------------
static bool g_settings_dirty;

static int  time_level(void)    { return config.engine.freeze_timer ? 4 : config.engine.dip_time; }
static int  traffic_level(void) { return config.engine.disable_traffic ? 4 : config.engine.dip_traffic; }

void cb_engine_set_settings(int auto_gears, int time, int traffic, int steer)
{
    // Digital steering: how far the wheel turns per game tick (30 Hz) while left/right is held,
    // out of 0x38 to full lock. Cannonball's default is 3 (about 0.6 s to full lock); 8 here
    // (about 0.25 s), which suits the G&W D-pad better.
    if (steer < 1 || steer > 9) steer = 8;
    config.controls.steer_speed = steer;
    cb_engine_set_auto_gears(auto_gears != 0);
    if (time < 0 || time > 4) time = 0;
    if (traffic < 0 || traffic > 4) traffic = 1;
    config.engine.freeze_timer    = time == 4;
    config.engine.dip_time        = time == 4 ? 0 : time;
    config.engine.disable_traffic = traffic == 4;
    config.engine.dip_traffic     = traffic == 4 ? 1 : traffic;
}

bool cb_engine_take_settings(int* auto_gears, int* time, int* traffic, int* steer)
{
    if (!g_settings_dirty) return false;
    g_settings_dirty = false;
    *auto_gears = cb_engine_get_auto_gears();
    *time = time_level();
    *traffic = traffic_level();
    *steer = config.controls.steer_speed;
    return true;
}

// Continuous mode traffic (0-8) and time trial laps (1-5) / traffic (0-8), as Cannonball's menu
static bool g_modes_dirty;

void cb_engine_set_mode_settings(int cont_traffic, int laps, int tt_traffic)
{
    config.cont_traffic   = cont_traffic >= 0 && cont_traffic <= TTrial::MAX_TRAFFIC ? cont_traffic : 3;
    config.ttrial.laps    = laps >= 1 && laps <= TTrial::MAX_LAPS ? laps : 5;
    config.ttrial.traffic = tt_traffic >= 0 && tt_traffic <= TTrial::MAX_TRAFFIC ? tt_traffic : 3;
}

bool cb_engine_take_mode_settings(int* cont_traffic, int* laps, int* tt_traffic)
{
    if (!g_modes_dirty) return false;
    g_modes_dirty = false;
    *cont_traffic = config.cont_traffic;
    *laps = config.ttrial.laps;
    *tt_traffic = config.ttrial.traffic;
    return true;
}

// ------------------------------------------------------------------------------------------------
// Menu
// ------------------------------------------------------------------------------------------------
static const int16_t  LOGO_Y       = -60;
static const uint16_t ROWS         = 28;
static const uint16_t HORIZON_DEST = 0x3A0;

enum { PAGE_MAIN, PAGE_SETTINGS, PAGE_MODES, PAGE_CONT, PAGE_TTRIAL, PAGE_COUNT };
enum { ITEM_PLAY, ITEM_SETTINGS, ITEM_EXIT, ITEM_GEARS, ITEM_TIME, ITEM_TRAFFIC, ITEM_STEER, ITEM_BACK,
       ITEM_MODES, ITEM_CONT, ITEM_TTRIAL, ITEM_CONT_START, ITEM_CONT_TRAFFIC, ITEM_TT_START, ITEM_TT_LAPS,
       ITEM_TT_TRAFFIC };
static const int MAIN_ITEMS[]     = { ITEM_PLAY, ITEM_MODES, ITEM_SETTINGS, ITEM_EXIT };
static const int SETTINGS_ITEMS[] = { ITEM_GEARS, ITEM_TIME, ITEM_TRAFFIC, ITEM_STEER, ITEM_BACK };
static const int MODES_ITEMS[]    = { ITEM_CONT, ITEM_TTRIAL, ITEM_BACK };
static const int CONT_ITEMS[]     = { ITEM_CONT_START, ITEM_CONT_TRAFFIC, ITEM_BACK };
static const int TTRIAL_ITEMS[]   = { ITEM_TT_START, ITEM_TT_LAPS, ITEM_TT_TRAFFIC, ITEM_BACK };
static const int PARENT[PAGE_COUNT] = { PAGE_MAIN, PAGE_MAIN, PAGE_MAIN, PAGE_MODES, PAGE_MODES };

static int  g_page, g_cursor, g_cursors[PAGE_COUNT];     // cursor kept per page for BACK
static int  g_frame;
static bool g_exit_requested;
static bool g_ttrial;                                    // time trial track selection on screen
static TTrial g_ttrial_fe(config.ttrial.best_times);

bool cb_engine_exit_requested(void) { bool r = g_exit_requested; g_exit_requested = false; return r; }

#define N(a) (int)(sizeof a / sizeof a[0])
static const int* items(int* n)
{
    switch (g_page) {
        case PAGE_SETTINGS: *n = N(SETTINGS_ITEMS); return SETTINGS_ITEMS;
        case PAGE_MODES:    *n = N(MODES_ITEMS);    return MODES_ITEMS;
        case PAGE_CONT:     *n = N(CONT_ITEMS);     return CONT_ITEMS;
        case PAGE_TTRIAL:   *n = N(TTRIAL_ITEMS);   return TTRIAL_ITEMS;
        default:            *n = N(MAIN_ITEMS);     return MAIN_ITEMS;
    }
}

static void go_page(int page)
{
    g_cursors[g_page] = g_cursor;
    g_page = page;
    g_cursor = page == PARENT[page] ? g_cursors[page] : 0;
}

static void go_back(void)
{
    int parent = PARENT[g_page];
    g_page = parent;
    g_cursor = g_cursors[parent];
}

static const char* level_name(int level, bool traffic)
{
    static const int L[] = { S_EASY, S_NORMAL, S_HARD, S_HARDEST };
    if (level == 4) return T(traffic ? S_NONE : S_INFINITE);
    return T(L[level & 3]);
}

static void item_text(int item, char* out, size_t size)
{
    const char* a = "";
    const char* b = NULL;
    switch (item) {
        case ITEM_PLAY:     a = T(S_PLAY); break;
        case ITEM_SETTINGS: a = T(S_SETTINGS); break;
        case ITEM_EXIT:     a = T(S_EXIT); break;
        case ITEM_GEARS:    a = T(S_GEARS);   b = T(cb_engine_get_auto_gears() ? S_AUTO : S_MANUAL); break;
        case ITEM_TIME:     a = T(S_TIME);    b = level_name(time_level(), false); break;
        case ITEM_TRAFFIC:  a = T(S_TRAFFIC); b = level_name(traffic_level(), true); break;
        case ITEM_STEER: {
            static char num[2];
            num[0] = (char)('0' + config.controls.steer_speed); num[1] = 0;
            a = T(S_STEER); b = num; break;
        }
        case ITEM_BACK:     a = T(S_BACK); break;
        case ITEM_MODES:    a = T(S_MODES); break;
        case ITEM_CONT:     a = T(S_CONT); break;
        case ITEM_TTRIAL:   a = T(S_TTRIAL); break;
        case ITEM_CONT_START:
        case ITEM_TT_START: a = T(S_START); break;
        case ITEM_CONT_TRAFFIC:
        case ITEM_TT_TRAFFIC:
        case ITEM_TT_LAPS: {
            static char num[2];
            int v = item == ITEM_CONT_TRAFFIC ? config.cont_traffic : item == ITEM_TT_LAPS ? config.ttrial.laps
                                                                                         : config.ttrial.traffic;
            num[0] = (char)('0' + v); num[1] = 0;
            a = T(item == ITEM_TT_LAPS ? S_LAPS : S_TRAFFIC); b = num; break;
        }
    }
    size_t n = 0;
    for (const char* p = a; *p && n + 1 < size; p++) out[n++] = *p;
    if (b) {
        if (n + 1 < size) out[n++] = ' ';
        for (const char* p = b; *p && n + 1 < size; p++) out[n++] = *p;
    }
    out[n] = 0;
}

// Background: as Menu::init() (logo, sky, road scrolling towards the horizon)
static void menu_init(void)
{
    // A new best lap in the last time trial: keep it (as Menu::init())
    if (outrun.ttrial.new_high_score) {
        outrun.ttrial.new_high_score = false;
        g_ttrial_fe.update_best_time();
    }
    g_ttrial = false;

    outrun.select_course(false, config.engine.prototype != 0);
    video.enabled = true;
    video.sprite_layer->set_x_clip(false);
    video.sprite_layer->reset();
    video.clear_text_ram();
    // (tiles are never patched on the G&W, so no restore_tiles())
    ologo.enable(LOGO_Y);

    oroad.stage_lookup_off = 9;
    oinitengine.init_road_seg_master();
    opalette.setup_sky_palette();
    opalette.setup_ground_color();
    opalette.setup_road_centre();
    opalette.setup_road_stripes();
    opalette.setup_road_side();
    opalette.setup_road_colour();
    otiles.setup_palette_hud();

    oroad.init();
    oroad.road_ctrl = ORoad::ROAD_R0;
    oroad.horizon_set = 1;
    oroad.horizon_base = HORIZON_DEST + 0x100;
    oinitengine.rd_split_state = OInitEngine::SPLIT_NONE;
    oinitengine.car_increment = 0;
    oinitengine.change_width = 0;

    outrun.game_state = GS_INIT;

    osoundint.has_booted = true;
    osoundint.init();

    g_page = PAGE_MAIN;
    g_cursor = 0;
    for (int i = 0; i < PAGE_COUNT; i++) g_cursors[i] = 0;
    g_frame = 0;
}

static void draw_options(void)
{
    int n;
    const int* it = items(&n);
    int y = 13 + ((ROWS - 13) >> 1) - ((n * 2) >> 1);
    for (int i = 0; i < n; i++) {
        char s[48];
        item_text(it[i], s, sizeof s);
        int x = 20 - ((int)strlen(s) >> 1);
        if (x < 3) x = 3;
        ohud.blit_text_new(x, y, s, OHud::GREEN);
        if (i == g_cursor)
            video.write_text32(ohud.translate(x - 3, y), roms.rom0.read32(TILES_MINICARS1));
        y += 2;
    }
}

static void select_item(int item, int dir)
{
    switch (item) {
        case ITEM_PLAY:
            if (dir) return;
            outrun.cannonball_mode = Outrun::MODE_ORIGINAL;
            config.engine.fix_bugs = config.engine.fix_bugs_backup;
            cannonball::state = cannonball::STATE_INIT_GAME;
            osoundint.queue_clear();
            return;
        case ITEM_SETTINGS:
            if (!dir) go_page(PAGE_SETTINGS);
            return;
        case ITEM_MODES:
            if (!dir) go_page(PAGE_MODES);
            return;
        case ITEM_CONT:
            if (!dir) go_page(PAGE_CONT);
            return;
        case ITEM_TTRIAL:
            if (!dir) go_page(PAGE_TTRIAL);
            return;
        case ITEM_CONT_START:
            if (dir) return;
            outrun.custom_traffic = config.cont_traffic;
            outrun.cannonball_mode = Outrun::MODE_CONT;
            config.engine.fix_bugs = config.engine.fix_bugs_backup;
            cannonball::state = cannonball::STATE_INIT_GAME;
            osoundint.queue_clear();
            return;
        case ITEM_TT_START:
            if (dir) return;
            config.engine.fix_bugs = config.engine.fix_bugs_backup;
            g_ttrial = true;                     // track selection (frontend/ttrial.cpp)
            g_ttrial_fe.init();
            return;
        case ITEM_CONT_TRAFFIC:
            config.cont_traffic = (config.cont_traffic + (dir < 0 ? TTrial::MAX_TRAFFIC : 1)) % (TTrial::MAX_TRAFFIC + 1);
            g_modes_dirty = true;
            return;
        case ITEM_TT_TRAFFIC:
            config.ttrial.traffic = (config.ttrial.traffic + (dir < 0 ? TTrial::MAX_TRAFFIC : 1)) % (TTrial::MAX_TRAFFIC + 1);
            g_modes_dirty = true;
            return;
        case ITEM_TT_LAPS:
            config.ttrial.laps = (config.ttrial.laps + (dir < 0 ? TTrial::MAX_LAPS - 2 : 0)) % TTrial::MAX_LAPS + 1;
            g_modes_dirty = true;
            return;
        case ITEM_EXIT:
            if (!dir) g_exit_requested = true;
            return;
        case ITEM_GEARS:
            cb_engine_set_auto_gears(!cb_engine_get_auto_gears());
            break;
        case ITEM_TIME:
            cb_engine_set_settings(cb_engine_get_auto_gears(), (time_level() + (dir < 0 ? 4 : 1)) % 5, traffic_level(),
                                   config.controls.steer_speed);
            break;
        case ITEM_TRAFFIC:
            cb_engine_set_settings(cb_engine_get_auto_gears(), time_level(), (traffic_level() + (dir < 0 ? 4 : 1)) % 5,
                                   config.controls.steer_speed);
            break;
        case ITEM_STEER:
            cb_engine_set_settings(cb_engine_get_auto_gears(), time_level(), traffic_level(),
                                   (config.controls.steer_speed + (dir < 0 ? 7 : 0)) % 9 + 1);
            break;
        case ITEM_BACK:
            if (!dir) go_back();
            return;
    }
    g_settings_dirty = true;
}

static void menu_tick(void)
{
    g_frame++;
    video.clear_text_ram();

    int n;
    const int* it = items(&n);
    if (input.has_pressed(Input::DOWN)) {
        osoundint.queue_sound(sound::BEEP1);
        if (++g_cursor >= n) g_cursor = 0;
    } else if (input.has_pressed(Input::UP)) {
        osoundint.queue_sound(sound::BEEP1);
        if (--g_cursor < 0) g_cursor = n - 1;
    } else if (input.has_pressed(Input::START) || input.has_pressed(Input::ACCEL)) {
        osoundint.queue_sound(sound::BEEP1);
        select_item(it[g_cursor], 0);
    } else if (g_page != PAGE_MAIN && (input.has_pressed(Input::LEFT) || input.has_pressed(Input::RIGHT))) {
        osoundint.queue_sound(sound::BEEP1);
        select_item(it[g_cursor], input.has_pressed(Input::LEFT) ? -1 : 1);
    } else if (g_page != PAGE_MAIN && input.has_pressed(Input::BRAKE)) {
        osoundint.queue_sound(sound::BEEP1);
        go_back();
    }
    if (cannonball::state != cannonball::STATE_MENU || g_ttrial)
        return;
    draw_options();

    // Horizon slides down, then the road scrolls (Menu::tick_ui at 30 fps)
    if (oroad.horizon_base > HORIZON_DEST) {
        oroad.horizon_base -= 60 / config.fps;
        if (oroad.horizon_base < HORIZON_DEST)
            oroad.horizon_base = HORIZON_DEST;
    } else {
        uint32_t scroll_speed = (config.fps == 60) ? config.menu.road_scroll_speed : config.menu.road_scroll_speed << 1;
        if (oinitengine.car_increment < scroll_speed << 16)
            oinitengine.car_increment += (1 << 14);
        if (oinitengine.car_increment > scroll_speed << 16)
            oinitengine.car_increment = scroll_speed << 16;
        uint32_t result = 0x12F * (oinitengine.car_increment >> 16);
        oroad.road_pos_change = result;
        oroad.road_pos += result;
        if (oroad.road_pos >> 16 > ROAD_END)
            oroad.road_pos = 0;
        oinitengine.update_road();
        oinitengine.set_granular_position();
        oroad.road_width_bak = oroad.road_width >> 16;
        oroad.car_x_bak = -oroad.road_width_bak;
        oinitengine.car_x_pos = oroad.car_x_bak;
    }

    ologo.tick();
    osprites.sprite_copy();
    osprites.update_sprites();
    oroad.tick();
}

// Called from cb_engine_tick() (gw_engine.cpp)
void gw_menu_state_tick(void)
{
    switch (cannonball::state) {
        case cannonball::STATE_INIT_MENU:
            oinputs.init();
            outrun.outputs->init();
            menu_init();
            cannonball::state = cannonball::STATE_MENU;
            break;
        case cannonball::STATE_MENU:
            if (g_ttrial) {
                // Time trial track selection: steer to choose, GAME/A to start, B back to the menu
                if (input.has_pressed(Input::BRAKE)) {
                    menu_init();
                    g_page = PAGE_TTRIAL;
                } else if (g_ttrial_fe.tick() == TTrial::INIT_GAME) {
                    g_ttrial = false;
                    cannonball::state = cannonball::STATE_INIT_GAME;
                    osoundint.queue_clear();
                }
            } else
                menu_tick();
            input.frame_done();
            osoundint.tick();
            break;
    }
}

void cb_engine_go_menu(void)
{
    cannonball::state = cannonball::STATE_INIT_MENU;
}
