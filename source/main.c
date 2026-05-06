/*
 * main.c — 3DSoundShell
 * Lecteur de musique avec interface 3DShell pour Nintendo 3DS / 2DS
 * Requiert Luma3DS + FBI pour installer le .cia
 *
 * Raccourcis clavier:
 *   L + R        = Pause / Lecture
 *   ◀            = Piste précédente
 *   ▶            = Piste suivante
 *   ▲ / ▼        = Volume +/-
 *   ZL / ZR      = Reculer / Avancer 10s (New3DS)
 *   R            = Changer visualiseur
 *   Y            = Activer/désactiver aléatoire
 *   Select       = Explorateur de fichiers
 *   Start        = Lecteur
 *   A            = Confirmer / Entrer dossier
 *   B            = Retour / Remonter dossier
 *   X            = Ajouter à la playlist
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

// Forward declarations for equalizer (defined in equalizer.c)
extern void eq_draw_top(int selected_band);
extern void eq_draw_bottom(int selected_band);
extern void eq_handle_input(int *selected_band, u32 keys_down);

// ─── Global app state ─────────────────────────────────────────
static AppState    g_state       = STATE_FILE_BROWSER;
static FileBrowser g_browser;
static Playlist    g_playlist;
static PlayerUI    g_player_ui;
static InputState  g_input;
static int         g_settings_sel = 0;
static int         g_eq_band      = 0;

// ─── Render targets ───────────────────────────────────────────
static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bot;

// ─── Helpers ──────────────────────────────────────────────────
static void load_and_play(const char *path, const char *title)
{
    Result r = audio_load(path);
    FILE *dbg=fopen("sdmc:/3DSoundShell/debug.log","a");
    if(dbg){fprintf(dbg,"load: %ld path: %s\n",r,path);fclose(dbg);}
    audio_set_volume(g_settings.volume);
    audio_play();
    player_ui_load_cover(&g_player_ui, audio_get_metadata());

    // Update settings for resume
    strncpy(g_settings.last_path, path, 511);
    g_settings.last_track = g_playlist.current;

    g_state = STATE_PLAYER;
}

static void play_current_track(void)
{
    if(g_playlist.current < 0 || g_playlist.current >= g_playlist.count) return;
    PlaylistEntry *e = &g_playlist.items[g_playlist.current];
    load_and_play(e->path, e->title);
}

// ─── Progress bar touch seek ──────────────────────────────────
static void handle_touch_seek(void)
{
    // Progress bar on top screen is at y=122, x=10, w=380
    // Bottom screen touch only (touchscreen)
    // We detect drag on bottom if in player state
    if(!g_input.touch_held) return;
    // Map touch x on bottom screen to seek
    // (Rudimentary: touch anywhere on bottom progress zone)
    int dur = audio_get_duration();
    if(dur <= 0) return;
    // If touch is in progress bar area on bottom (y: 8-20, x: 10-310)
    int tx = g_input.touch.px, ty = g_input.touch.py;
    if(ty >= 8 && ty <= 20 && tx >= 10 && tx <= 310) {
        float pct = (float)(tx - 10) / 300.f;
        if(pct < 0) pct = 0;
        if(pct > 1) pct = 1;
        audio_seek((int)(pct * dur));
    }
}

// ─── Input: File browser ──────────────────────────────────────
static void input_file_browser(void)
{
    u32 kd = g_input.down;

    if(kd & KEY_DOWN)   fb_select_next(&g_browser);
    if(kd & KEY_UP)     fb_select_prev(&g_browser);

    if(kd & KEY_A) {
        if(g_browser.count == 0) return;
        FileEntry *fe = &g_browser.entries[g_browser.selected];
        if(fe->is_dir) {
            if(!strcmp(fe->name,"..")) fb_go_up(&g_browser);
            else fb_enter_dir(&g_browser, fe->full_path);
        } else if(fe->is_audio) {
            // Clear playlist, add file, play
            pl_clear(&g_playlist);
            // Add all audio from same dir, jump to this track
            pl_add_dir(&g_playlist, g_browser.cwd);
            // Find this track in playlist
            for(int i=0;i<g_playlist.count;i++) {
                if(!strcmp(g_playlist.items[i].path, fe->full_path)) {
                    pl_jump(&g_playlist, i);
                    break;
                }
            }
            play_current_track();
        }
    }

    if(kd & KEY_B)      fb_go_up(&g_browser);

    if(kd & KEY_X) {
        // Add selected file to playlist without switching
        if(g_browser.count == 0) return;
        FileEntry *fe = &g_browser.entries[g_browser.selected];
        if(fe->is_audio) pl_add(&g_playlist, fe->full_path, fe->name);
    }

    if(kd & KEY_Y) {
        // Add entire directory to playlist
        pl_add_dir(&g_playlist, g_browser.cwd);
    }

    if(kd & KEY_START)  g_state = STATE_PLAYER;
    if(kd & KEY_SELECT) g_state = STATE_SETTINGS;
}

// ─── Input: Player ────────────────────────────────────────────
static void input_player(void)
{
    u32 kd   = g_input.down;
    u32 held = g_input.held;

    // L + R = toggle pause
    if((kd & KEY_L) && (held & KEY_R)) { audio_toggle_pause(); return; }
    if((kd & KEY_R) && (held & KEY_L)) { audio_toggle_pause(); return; }

    // Arrow keys
    if(kd & KEY_LEFT) {
        int nxt = pl_prev(&g_playlist);
        if(nxt >= 0) play_current_track();
    }
    if(kd & KEY_RIGHT) {
        int nxt = pl_next(&g_playlist);
        if(nxt >= 0) play_current_track();
        else audio_stop();
    }

    // Volume
    if(held & KEY_UP) {
        float v = audio_get_volume() + 0.02f;
        if(v > 1.f) v = 1.f;
        audio_set_volume(v);
        g_settings.volume = v;
    }
    if(held & KEY_DOWN) {
        float v = audio_get_volume() - 0.02f;
        if(v < 0.f) v = 0.f;
        audio_set_volume(v);
        g_settings.volume = v;
    }

    // Seek ±10s (New3DS ZL/ZR, regular 3DS will just ignore if not present)
    if(kd & KEY_ZL)  audio_seek(audio_get_position() - 10);
    if(kd & KEY_ZR)  audio_seek(audio_get_position() + 10);

    // R = cycle visualizer
    if(kd & KEY_R)   player_ui_cycle_viz(&g_player_ui);

    // Y = toggle shuffle
    if(kd & KEY_Y)   pl_toggle_shuffle(&g_playlist);

    // A = pause/play toggle (alternative)
    if(kd & KEY_A)   audio_toggle_pause();

    // B = go to file browser
    if(kd & KEY_B)   g_state = STATE_FILE_BROWSER;

    // Select = file browser
    if(kd & KEY_SELECT) g_state = STATE_FILE_BROWSER;

    // X = playlist view
    if(kd & KEY_X)   g_state = STATE_PLAYLIST;

    // Touch seek on bottom
    handle_touch_seek();

    // Auto-advance when track ends
    if(audio_is_finished()) {
        int nxt = pl_next(&g_playlist);
        if(nxt >= 0) play_current_track();
        else audio_stop();
    }
}

// ─── Input: Playlist ─────────────────────────────────────────
static void input_playlist(void)
{
    u32 kd = g_input.down;

    if(kd & KEY_DOWN) {
        if(g_playlist.selected < g_playlist.count-1) {
            g_playlist.selected++;
            // Scroll
            int vis = (BOT_HEIGHT-30)/22;
            if(g_playlist.selected >= g_playlist.scroll_offset+vis)
                g_playlist.scroll_offset++;
        }
    }
    if(kd & KEY_UP) {
        if(g_playlist.selected > 0) {
            g_playlist.selected--;
            if(g_playlist.selected < g_playlist.scroll_offset)
                g_playlist.scroll_offset--;
        }
    }

    // A = play selected
    if(kd & KEY_A) {
        pl_jump(&g_playlist, g_playlist.selected);
        play_current_track();
    }

    // X = remove from playlist
    if(kd & KEY_X) pl_remove(&g_playlist, g_playlist.selected);

    // Y = shuffle toggle
    if(kd & KEY_Y) pl_toggle_shuffle(&g_playlist);

    // B = back to player
    if(kd & KEY_B)  g_state = STATE_PLAYER;

    // Select = file browser
    if(kd & KEY_SELECT) g_state = STATE_FILE_BROWSER;

    // Start = player
    if(kd & KEY_START)  g_state = STATE_PLAYER;

    // L+R = pause
    u32 held = g_input.held;
    if((kd & KEY_L) && (held & KEY_R)) audio_toggle_pause();
    if((kd & KEY_R) && (held & KEY_L)) audio_toggle_pause();

    // Auto-advance
    if(audio_is_finished()) {
        int nxt = pl_next(&g_playlist);
        if(nxt >= 0) play_current_track();
    }
}

// ─── Input: Settings ─────────────────────────────────────────
static void input_settings(void)
{
    settings_handle_input(&g_settings_sel, g_input.down);
    if(g_input.down & KEY_B)      g_state = STATE_PLAYER;
    if(g_input.down & KEY_START)  g_state = STATE_PLAYER;
    if(g_input.down & KEY_SELECT) g_state = STATE_FILE_BROWSER;
    // L = go to EQ
    if(g_input.down & KEY_L)      g_state = STATE_EQUALIZER;
}

// ─── Input: Equalizer ────────────────────────────────────────
static void input_equalizer(void)
{
    eq_handle_input(&g_eq_band, g_input.down);
    if(g_input.down & KEY_B)     g_state = STATE_SETTINGS;
    if(g_input.down & KEY_START) g_state = STATE_PLAYER;
    // Save EQ to settings
    for(int i=0;i<EQ_BANDS;i++)
        g_settings.eq_gains[i] = audio_eq_get_gain(i);
}

// ─── Draw ─────────────────────────────────────────────────────
static void draw_frame(float dt)
{
    const AudioMetadata *meta = audio_get_metadata();

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    // ── Top screen ─────────────────────────────────────────────
    C2D_TargetClear(g_top, current_theme->bg_primary);
    C2D_SceneBegin(g_top);

    switch(g_state) {
        case STATE_FILE_BROWSER: fb_draw_top(&g_browser);              break;
        case STATE_PLAYER:       player_ui_draw_top(&g_player_ui,meta,&g_playlist); break;
        case STATE_PLAYLIST:     pl_draw_top(&g_playlist);             break;
        case STATE_SETTINGS:     settings_draw_top();                  break;
        case STATE_EQUALIZER:    eq_draw_top(g_eq_band);               break;
    }

    // ── Bottom screen ──────────────────────────────────────────
    C2D_TargetClear(g_bot, current_theme->bg_primary);
    C2D_SceneBegin(g_bot);

    switch(g_state) {
        case STATE_FILE_BROWSER: fb_draw_bottom(&g_browser);            break;
        case STATE_PLAYER:       player_ui_draw_bottom(&g_player_ui,meta,&g_playlist); break;
        case STATE_PLAYLIST:     pl_draw_bottom(&g_playlist);           break;
        case STATE_SETTINGS:     settings_draw_bottom(&g_settings_sel); break;
        case STATE_EQUALIZER:    eq_draw_bottom(g_eq_band);             break;
    }

    C3D_FrameEnd(0);
    (void)dt;
}

// ─── Main ─────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    srand(time(NULL));

    // Init services
    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    cfguInit();
    srand(osGetTime());

    // Create render targets
    g_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Create app directory
    mkdir("sdmc:/3DSoundShell", 0777);
    mkdir("sdmc:/Music",        0777);
    FILE*fm=fopen("sdmc:/debug_root.log","w");if(fm){fprintf(fm,"main start\n");fclose(fm);}

    // Init subsystems
    theme_init();
    settings_load();
    theme_set(theme_get(g_settings.theme_index));
    audio_init();
    {FILE*fm=fopen("sdmc:/3DSoundShell/debug.log","w");if(fm){fprintf(fm,"after audio_init\n");fclose(fm);}}
    audio_set_volume(g_settings.volume);

    // Apply saved EQ
    for(int i=0;i<EQ_BANDS;i++) audio_eq_set_gain(i, g_settings.eq_gains[i]);

    // Init UI
    fb_init(&g_browser, g_settings.start_dir[0] ? g_settings.start_dir : "sdmc:/Music");
    pl_init(&g_playlist);
    g_playlist.shuffle = g_settings.shuffle;
    g_playlist.repeat  = (RepeatMode)g_settings.repeat;
    player_ui_init(&g_player_ui);
    g_player_ui.viz_style = g_settings.viz_style;

    // Resume last session
    if(g_settings.resume_on_start && g_settings.last_path[0]) {
        pl_add(&g_playlist, g_settings.last_path, g_settings.last_path);
        pl_jump(&g_playlist, 0);
        play_current_track();
        if(g_settings.last_position > 0)
            audio_seek(g_settings.last_position);
        g_state = STATE_PLAYER;
    }

    u64 last_time = osGetTime();

    // ─── Main loop ────────────────────────────────────────────
    while(aptMainLoop()) {

        // Delta time
        u64 now = osGetTime();
        float dt = (now - last_time) / 1000.f;
        last_time = now;
        if(dt > 0.1f) dt = 0.1f;

        // Input
        input_update(&g_input);

        // Global: HOME button handled by aptMainLoop
        // Global: power button / sleep handled automatically

        // State input
        switch(g_state) {
            case STATE_FILE_BROWSER: input_file_browser(); break;
            case STATE_PLAYER:       input_player();       break;
            case STATE_PLAYLIST:     input_playlist();     break;
            case STATE_SETTINGS:     input_settings();     break;
            case STATE_EQUALIZER:    input_equalizer();    break;
        }

        // Update player UI
        player_ui_update(&g_player_ui, audio_get_metadata(), dt);

        // Save position periodically (every ~5s)
        static int save_timer = 0;
        if(++save_timer > 300) {
            save_timer = 0;
            g_settings.last_position = audio_get_position();
            g_settings.last_track    = g_playlist.current;
            settings_save();
        }

        // Draw
        draw_frame(dt);
    }

    // ─── Cleanup ──────────────────────────────────────────────
    settings_save();
    audio_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    cfguExit();
    romfsExit();

    return 0;
}
