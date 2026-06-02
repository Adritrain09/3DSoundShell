/*
** main.c — 3DSoundShell
** Lecteur de musique pour Nintendo 3DS
*/
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "main.h"
#include "theme.h"
#include "audio.h"
#include "filebrowser.h"
#include "playlist.h"
#include "player_ui.h"
#include "settings.h"
#include "input.h"

/* Forward declarations equalizer */
extern void eq_draw_top(int selected_band);
extern void eq_draw_bottom(int selected_band);
extern void eq_handle_input(int *selected_band, u32 keys_down);

/* ── Global state ─────────────────────────────────────────── */
static AppState    g_state        = STATE_FILE_BROWSER;
static FileBrowser g_browser;
static Playlist    g_playlist;
static PlayerUI    g_player_ui;
static InputState  g_input;
static int         g_settings_sel = 0;
static int         g_eq_band      = 0;
static volatile bool g_init_done  = false;

/* ── Render targets ───────────────────────────────────────── */
static C3D_RenderTarget *g_top = NULL;
static C3D_RenderTarget *g_bot = NULL;

/* ── Debug log ────────────────────────────────────────────── */
static void dbg(const char *msg)
{
    FILE *f = fopen("sdmc:/3DSoundShell/debug.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

/* ── Load and play ────────────────────────────────────────── */
/* Reset timer auto-avance a chaque nouveau chargement */
extern void reset_load_timer(void);
static u64 s_load_time = 0;
void reset_load_timer(void) { s_load_time = 0; }

static void load_and_play(const char *path)
{
    s_load_time = osGetTime(); /* marquer debut chargement */
    Result r = audio_load(path);
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "audio_load=%ld path=%s", r, path);
        dbg(buf);
    }
    if (R_FAILED(r)) return;

    audio_set_volume(g_settings.volume);
    audio_play();
    player_ui_load_cover(&g_player_ui, audio_get_metadata());

    /* Sauvegarder immediatement la piste courante */
    strncpy(g_settings.last_path, path, 511);
    g_settings.last_track    = g_playlist.current;
    g_settings.last_position = 0;
    settings_save(); /* Async: pas de freeze */
    
    g_state = STATE_PLAYER;
}

static void play_current_track(void)
{
    if (g_playlist.current < 0 ||
        g_playlist.current >= g_playlist.count) return;
    load_and_play(g_playlist.items[g_playlist.current].path);
}

/* ── Touch seek ───────────────────────────────────────────── */
static void handle_touch_seek(void)
{
    if (!g_input.touch_held) return;
    int dur = audio_get_duration();
    if (dur <= 0) return;
    int tx = g_input.touch.px, ty = g_input.touch.py;
    if (ty >= 8 && ty <= 20 && tx >= 10 && tx <= 310) {
        float pct = (float)(tx - 10) / 300.f;
        if (pct < 0.f) pct = 0.f;
        if (pct > 1.f) pct = 1.f;
        audio_seek((int)(pct * dur));
    }
}

/* ── Input: File browser ──────────────────────────────────── */
static void input_file_browser(void)
{
    u32 kd   = g_input.down;
    u32 held = g_input.held;

    /* Navigation verticale */
    if (kd & KEY_DOWN) fb_select_next(&g_browser);
    if (kd & KEY_UP)   fb_select_prev(&g_browser);

    /* Répétition maintenue */
    static u64  ud_held = 0;
    static bool ud_rep  = false;
    if (held & (KEY_DOWN | KEY_UP)) {
        if (!ud_rep) {
            if (ud_held == 0) ud_held = osGetTime();
            if (osGetTime() - ud_held > 400) ud_rep = true;
        }
        if (ud_rep) {
            if (held & KEY_DOWN) fb_select_next(&g_browser);
            else                 fb_select_prev(&g_browser);
        }
    } else { ud_held = 0; ud_rep = false; }

    /* Sauter 5 fichiers */
    if (kd & KEY_RIGHT)
        for (int i = 0; i < 5; i++) fb_select_next(&g_browser);
    if (kd & KEY_LEFT)
        for (int i = 0; i < 5; i++) fb_select_prev(&g_browser);

    /* Entrée / lecture */
    if (kd & KEY_A) {
        if (g_browser.count == 0) return;
        FileEntry *fe = &g_browser.entries[g_browser.selected];
        if (fe->is_dir) {
            if (!strcmp(fe->name, "..")) fb_go_up(&g_browser);
            else                         fb_enter_dir(&g_browser, fe->full_path);
        } else if (fe->is_audio) {
            pl_clear(&g_playlist);
            pl_add_dir(&g_playlist, g_browser.cwd);
            for (int i = 0; i < g_playlist.count; i++) {
                if (!strcmp(g_playlist.items[i].path, fe->full_path)) {
                    pl_jump(&g_playlist, i);
                    break;
                }
            }
            play_current_track();
        }
    }

    if (kd & KEY_B) fb_go_up(&g_browser);

    if (kd & KEY_X) {
        if (g_browser.count == 0) return;
        FileEntry *fe = &g_browser.entries[g_browser.selected];
        if (fe->is_audio) pl_add(&g_playlist, fe->full_path, fe->name);
    }

    if (kd & KEY_Y)     pl_add_dir(&g_playlist, g_browser.cwd);
    if (kd & KEY_START) g_state = STATE_PLAYER;
    if (kd & KEY_SELECT)g_state = STATE_SETTINGS;

    (void)held;
}

/* ── Input: Player ────────────────────────────────────────── */
static void input_player(void)
{
    u32 kd   = g_input.down;
    u32 held = g_input.held;

    /* L+R = pause */
    if ((kd & KEY_L) && (held & KEY_R)) { audio_toggle_pause(); return; }
    if ((kd & KEY_R) && (held & KEY_L)) { audio_toggle_pause(); return; }

    if (kd & KEY_LEFT) {
        if (pl_prev(&g_playlist) >= 0) play_current_track();
    }
    if (kd & KEY_RIGHT) {
        int n = pl_next(&g_playlist);
        if (n >= 0) play_current_track();
        else        audio_stop();
    }

    /* Volume */
    if (held & KEY_UP) {
        float v = audio_get_volume() + 0.02f;
        if (v > 1.f) v = 1.f;
        audio_set_volume(v);
        g_settings.volume = v;
    }
    if (held & KEY_DOWN) {
        float v = audio_get_volume() - 0.02f;
        if (v < 0.f) v = 0.f;
        audio_set_volume(v);
        g_settings.volume = v;
    }
    /* Save volume quand on relache */
    if ((g_input.up & KEY_UP) || (g_input.up & KEY_DOWN))
        settings_save();

    if (kd & KEY_ZL) audio_seek(audio_get_position() - 10);
    if (kd & KEY_ZR) audio_seek(audio_get_position() + 10);
    if (kd & KEY_R)  player_ui_cycle_viz(&g_player_ui);
    if (kd & KEY_Y) { pl_toggle_shuffle(&g_playlist); g_settings.shuffle = g_playlist.shuffle; settings_save(); }
    if (kd & KEY_A)  audio_toggle_pause();
    if (kd & KEY_B)  g_state = STATE_FILE_BROWSER;
    if (kd & KEY_SELECT) g_state = STATE_FILE_BROWSER;
    if (kd & KEY_X)  g_state = STATE_PLAYLIST;

    handle_touch_seek();

    /* Auto-avance */
    if (audio_is_finished()) {
        int n = pl_next(&g_playlist);
        if (n >= 0) play_current_track();
        else        audio_stop();
    }
}

/* ── Input: Playlist ──────────────────────────────────────── */
static void input_playlist(void)
{
    u32 kd   = g_input.down;
    u32 held = g_input.held;
    int vis  = (BOT_HEIGHT - 30) / 22;

    if (kd & KEY_DOWN && g_playlist.selected < g_playlist.count - 1) {
        g_playlist.selected++;
        if (g_playlist.selected >= g_playlist.scroll_offset + vis)
            g_playlist.scroll_offset++;
    }
    if (kd & KEY_UP && g_playlist.selected > 0) {
        g_playlist.selected--;
        if (g_playlist.selected < g_playlist.scroll_offset)
            g_playlist.scroll_offset--;
    }

    if (kd & KEY_A) {
        pl_jump(&g_playlist, g_playlist.selected);
        play_current_track();
    }
    if (kd & KEY_X)  pl_remove(&g_playlist, g_playlist.selected);
    if (kd & KEY_Y) { pl_toggle_shuffle(&g_playlist); g_settings.shuffle = g_playlist.shuffle; settings_save(); }
    if (kd & KEY_B)  g_state = STATE_PLAYER;
    if (kd & KEY_SELECT) g_state = STATE_FILE_BROWSER;
    if (kd & KEY_START)  g_state = STATE_PLAYER;

    if ((kd & KEY_L) && (held & KEY_R)) audio_toggle_pause();
    if ((kd & KEY_R) && (held & KEY_L)) audio_toggle_pause();

    if (audio_is_finished()) {
        if (pl_next(&g_playlist) >= 0) play_current_track();
    }
}

/* ── Input: Settings ──────────────────────────────────────── */
static void input_settings(void)
{
    settings_handle_input(&g_settings_sel, g_input.down);
    if (g_input.down & KEY_B)      g_state = STATE_PLAYER;
    if (g_input.down & KEY_START)  g_state = STATE_PLAYER;
    if (g_input.down & KEY_SELECT) g_state = STATE_FILE_BROWSER;
    if (g_input.down & KEY_L)      g_state = STATE_EQUALIZER;
}

/* ── Input: Equalizer ─────────────────────────────────────── */
static void input_equalizer(void)
{
    eq_handle_input(&g_eq_band, g_input.down);
    if (g_input.down & KEY_B)     g_state = STATE_SETTINGS;
    if (g_input.down & KEY_START) g_state = STATE_PLAYER;
    for (int i = 0; i < EQ_BANDS; i++)
        g_settings.eq_gains[i] = audio_eq_get_gain(i);
}

/* ── Draw ─────────────────────────────────────────────────── */
static void draw_frame(float dt)
{
    const AudioMetadata *meta = audio_get_metadata();

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    /* Ecran de chargement si init pas terminee */
    if (!g_init_done) {
        C2D_TargetClear(g_top, current_theme->bg_primary);
        C2D_SceneBegin(g_top);
        /* Texte de chargement */
        static C2D_TextBuf load_tb = NULL;
        if (!load_tb) load_tb = C2D_TextBufNew(64);
        C2D_TextBufClear(load_tb);
        C2D_Text tx;
        C2D_TextParse(&tx, load_tb, "Chargement...");
        C2D_TextOptimize(&tx);
        C2D_DrawText(&tx, C2D_AlignLeft|C2D_WithColor,
            140, 110, 0, 0.6f, 0.6f, current_theme->text_accent);

        C2D_TargetClear(g_bot, current_theme->bg_primary);
        C2D_SceneBegin(g_bot);
        C3D_FrameEnd(0);
        return;
    }

    /* Top screen */
    C2D_TargetClear(g_top, current_theme->bg_primary);
    C2D_SceneBegin(g_top);
    switch (g_state) {
        case STATE_FILE_BROWSER: fb_draw_top(&g_browser);                          break;
        case STATE_PLAYER:       player_ui_draw_top(&g_player_ui, meta, &g_playlist); break;
        case STATE_PLAYLIST:     pl_draw_top(&g_playlist);                         break;
        case STATE_SETTINGS:     settings_draw_top();                              break;
        case STATE_EQUALIZER:    eq_draw_top(g_eq_band);                           break;
    }

    /* Bottom screen */
    C2D_TargetClear(g_bot, current_theme->bg_primary);
    C2D_SceneBegin(g_bot);
    switch (g_state) {
        case STATE_FILE_BROWSER: fb_draw_bottom(&g_browser);                           break;
        case STATE_PLAYER:       player_ui_draw_bottom(&g_player_ui, meta, &g_playlist); break;
        case STATE_PLAYLIST:     pl_draw_bottom(&g_playlist);                          break;
        case STATE_SETTINGS:     settings_draw_bottom(&g_settings_sel);               break;
        case STATE_EQUALIZER:    eq_draw_bottom(g_eq_band);                           break;
    }

    C3D_FrameEnd(0);
    (void)dt;
}

/* ── Thread d init SD ────────────────────────────────────── */
static void init_thread_func(void *arg)
{
    const char *path = (const char*)arg;

    /* Scan du dossier musique */
    fb_enter_dir(&g_browser, path);

    /* Resume session */
    if (g_settings.resume_on_start && g_settings.last_path[0]) {
        char resume_dir[512];
        strncpy(resume_dir, g_settings.last_path, 511);
        char *sl = strrchr(resume_dir, '/');
        if (sl) *sl = '\0';

        /* Desactiver shuffle pendant le chargement */
        bool saved_shuffle = g_playlist.shuffle;
        g_playlist.shuffle = false;

        pl_add_dir(&g_playlist, resume_dir);

        dbg("init: pl_add_dir done");
        {
            FILE *f = fopen("sdmc:/3DSoundShell/debug.log","a");
            if(f) {
                fprintf(f, "init: count=%d looking for: %s\n",
                    g_playlist.count, g_settings.last_path);
                for(int i=0;i<g_playlist.count && i<5;i++)
                    fprintf(f, "  [%d] %s\n", i, g_playlist.items[i].path);
                fclose(f);
            }
        }

        int found = 0;
        for (int i = 0; i < g_playlist.count; i++) {
            if (!strcmp(g_playlist.items[i].path, g_settings.last_path)) {
                pl_jump(&g_playlist, i);
                found = 1;
                dbg("init: track found!");
                break;
            }
        }
        if (!found) {
            dbg("init: track NOT found, jumping to 0");
            if (g_playlist.count > 0) pl_jump(&g_playlist, 0);
        }

        /* Restaurer shuffle */
        g_playlist.shuffle = saved_shuffle;
        if (saved_shuffle) pl_rebuild_shuffle(&g_playlist);
    }

    g_init_done = true;
    dbg("init_thread: done");
}

/* ── Main ─────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* ── 1. Services système de base ─────────────────────── */
    romfsInit();
    gfxInitDefault();

    /* Créer le dossier debug AVANT tout */
    mkdir("sdmc:/3DSoundShell", 0777);
    mkdir("sdmc:/Music",        0777);

    /* Vider / créer le log */
    {
        FILE *f = fopen("sdmc:/3DSoundShell/debug.log", "w");
        if (f) { fprintf(f, "=== 3DSoundShell start ===\n"); fclose(f); }
    }
    dbg("step 1: gfx ok");

    /* ── 2. Citro ─────────────────────────────────────────── */
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        dbg("ERREUR: C3D_Init failed");
        gfxExit(); romfsExit(); return 1;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        dbg("ERREUR: C2D_Init failed");
        C3D_Fini(); gfxExit(); romfsExit(); return 1;
    }
    C2D_Prepare();
    dbg("step 2: citro ok");

    /* ── 3. Render targets ────────────────────────────────── */
    g_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!g_top || !g_bot) {
        dbg("ERREUR: render targets failed");
        C2D_Fini(); C3D_Fini(); gfxExit(); romfsExit(); return 1;
    }
    dbg("step 3: render targets ok");

    /* ── 4. Services optionnels ───────────────────────────── */
    cfguInit();
    ptmuInit();
    mcuHwcInit();
    srand((unsigned)osGetTime());
    dbg("step 4: services ok");

    /* ── 5. Thème & settings ──────────────────────────────── */
    theme_init();
    settings_load();
    theme_set(theme_get(g_settings.theme_index));
    dbg("step 5: theme+settings ok");

    /* ── 6. Audio (NDSP) ──────────────────────────────────── */
    Result audio_rc = audio_init();
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "step 6: audio_init=%ld", audio_rc);
        dbg(buf);
    }
    if (R_FAILED(audio_rc)) {
        dbg("ERREUR: audio_init failed — on continue sans audio");
        /* On ne quitte pas, on continue en mode silencieux */
    }
    audio_set_volume(g_settings.volume);
    dbg("step 6b: volume set");

    /* EQ */
    for (int i = 0; i < EQ_BANDS; i++)
        audio_eq_set_gain(i, g_settings.eq_gains[i]);

    /* ── 7. UI (init rapide, pas de scan SD ici) ───────────── */
    pl_init(&g_playlist);
    g_playlist.shuffle = g_settings.shuffle;
    g_playlist.repeat  = (RepeatMode)g_settings.repeat;
    player_ui_init(&g_player_ui);
    g_player_ui.viz_style = g_settings.viz_style;

    /* Init browser vide pour l instant */
    memset(&g_browser, 0, sizeof(g_browser));
    g_browser.visible_rows = 9;
    strncpy(g_browser.cwd,
        g_settings.start_dir[0] ? g_settings.start_dir : "sdmc:/Music",
        MAX_PATH-1);

    dbg("step 7: UI ok");

    /* ── 8. Thread d init SD (scan en arriere-plan) ─────── */
    static char s_init_path[512];
    strncpy(s_init_path,
        g_settings.start_dir[0] ? g_settings.start_dir : "sdmc:/Music",
        511);

    threadCreate(init_thread_func, s_init_path, 32*1024, 0x30, -1, true);
    dbg("step 8: init thread lance");

    dbg("step 8: init thread started");

    /* ── Boucle principale ────────────────────────────────── */
    u64 last_time    = osGetTime();
    int save_timer   = 0;
    bool resumed     = false;

    while (aptMainLoop()) {
        u64   now = osGetTime();
        float dt  = (now - last_time) / 1000.f;
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        input_update(&g_input);

        /* Resume session une fois init terminee */
        if (g_init_done && !resumed) {
            resumed = true;
            dbg("resume: starting");
            if (g_settings.resume_on_start &&
                g_settings.last_path[0] &&
                g_playlist.count > 0) {
                dbg("resume: avant audio_load");
                play_current_track();
                dbg("resume: apres audio_load");
                if (g_settings.last_position > 0)
                    audio_seek(g_settings.last_position);
                g_state = STATE_PLAYER;
                dbg("resume: done");
            }
        }

        switch (g_state) {
            case STATE_FILE_BROWSER: input_file_browser(); break;
            case STATE_PLAYER:       input_player();       break;
            case STATE_PLAYLIST:     input_playlist();     break;
            case STATE_SETTINGS:     input_settings();     break;
            case STATE_EQUALIZER:    input_equalizer();    break;
        }

        player_ui_update(&g_player_ui, audio_get_metadata(), dt);

        /* Sauvegarde périodique (toutes ~5s à 60fps) */
        if (++save_timer > 600) {
            save_timer = 0;
            if (audio_get_state() == AUDIO_PLAYING) {
                g_settings.last_position = audio_get_position();
                g_settings.last_track    = g_playlist.current;
                settings_save();
            }
        }

        draw_frame(dt);
    }

    /* ── Nettoyage (UNE SEULE FOIS chaque service) ────────── */
    g_settings.last_position = audio_get_position();
    g_settings.last_track    = g_playlist.current;
    dbg("cleanup start");
    settings_save_sync();
    dbg("cleanup: save done");

    player_ui_exit(&g_player_ui);
    audio_exit();

    C2D_Fini();
    C3D_Fini();

    ptmuExit();
    mcuHwcExit();
    cfguExit();
    gfxExit();
    romfsExit();

    dbg("cleanup done");
    return 0;
}