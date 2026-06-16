/*
** main.c — 3DSoundShell
** 3DSoundShell - Music Player by Adri
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
#include "favorites.h"

/* Variables globales 3D stéréoscopique */
float g_3d_iod = 0.f;
int   g_3d_eye = 0;

/* ── TextBuf global partagé (filebrowser, equalizer, etc.) ── */
static u64   s_touch_arrow_held_time = 0;
static bool  s_touch_arrow_up        = false;
static bool  s_touch_arrow_down      = false;
static bool  s_touch_arrow_rep       = false;
#define ARROW_DELAY_MS  400
#define ARROW_REPEAT_MS 100

/* Forward declarations equalizer */
extern void eq_draw_top(int selected_band);
extern void eq_draw_bottom(int selected_band);
extern void eq_handle_input(int *selected_band, u32 keys_down);

/* ── Forward declarations navigation ─────────────────────── */
static void nav_enter(const char *path);

/* Retourne true si la flèche tactile doit déclencher un scroll ce frame
   dir = true → flèche haut, false → flèche bas */
static bool touch_arrow_tick(bool up)
{
    u64 now = osGetTime();

    /* Nouveau appui sur cette flèche */
    if ((up && !s_touch_arrow_up) || (!up && !s_touch_arrow_down)) {
        s_touch_arrow_up        = up;
        s_touch_arrow_down      = !up;
        s_touch_arrow_held_time = now;
        s_touch_arrow_rep       = false;
        return true; /* premier déclenchement immédiat */
    }

    /* Flèche maintenue → répétition après délai */
    u64 held = now - s_touch_arrow_held_time;
    if (!s_touch_arrow_rep && held >= ARROW_DELAY_MS) {
        s_touch_arrow_rep       = true;
        s_touch_arrow_held_time = now; /* reset timer pour intervalle */
        return true;
    }
    if (s_touch_arrow_rep && held >= ARROW_REPEAT_MS) {
        s_touch_arrow_held_time = now;
        return true;
    }
    return false;
}

/* Réinitialiser l état des flèches quand on relâche */
static void touch_arrow_reset(void)
{
    s_touch_arrow_up   = false;
    s_touch_arrow_down = false;
    s_touch_arrow_rep  = false;
}
static void nav_go_up(void);

/* ── Global state ─────────────────────────────────────────── */
static volatile bool g_loading        = false;
static char          g_load_path[512] = {0};
static AppState    g_state        = STATE_FILE_BROWSER;
static FileBrowser g_browser;
static Playlist    g_playlist;
static PlayerUI    g_player_ui;
static InputState  g_input;
static int         g_settings_sel = 0;
static int         g_eq_band      = 0;
static volatile bool g_init_done  = false;

/* ── Render targets ───────────────────────────────────────── */
static C3D_RenderTarget *g_top       = NULL;
static C3D_RenderTarget *g_top_right = NULL;
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

static void load_thread_func(void *arg)
{
    (void)arg;
    Result r = audio_load(g_load_path);
    if (!R_FAILED(r)) {
        audio_set_volume(g_settings.volume);
        audio_play();
        player_ui_load_cover(&g_player_ui, audio_get_metadata());
        strncpy(g_settings.last_path, g_load_path, 511);
        g_settings.last_track    = g_playlist.current;
        g_settings.last_position = 0;
    }
    g_loading = false;
}

static void load_and_play(const char *path)
{
    if (g_loading) return; /* deja en chargement */
    strncpy(g_load_path, path, 511);
    g_loading = true;
    g_state   = STATE_PLAYER;

    Thread t = threadCreate(load_thread_func, NULL, 32*1024, 0x30, -1, true);
    if (!t) {
        /* Fallback synchrone si thread impossible */
        Result r = audio_load(path);
        if (!R_FAILED(r)) {
            audio_set_volume(g_settings.volume);
            audio_play();
            player_ui_load_cover(&g_player_ui, audio_get_metadata());
            strncpy(g_settings.last_path, path, 511);
            g_settings.last_track    = g_playlist.current;
            g_settings.last_position = 0;
        }
        g_loading = false;
    }
}

static void play_current_track(void)
{
    if (g_playlist.current < 0 ||
        g_playlist.current >= g_playlist.count) return;
    load_and_play(g_playlist.items[g_playlist.current].path);
}

/* ── Touch seek ───────────────────────────────────────────── */
/* Position tactile mémorisée pendant le glissement */
static bool  s_touch_seeking = false;
static float s_touch_seek_pct = 0.f;

static void handle_touch_seek(void)
{
    int dur = audio_get_duration();
    if (dur <= 0) return;

    /* Zone tactile : barre de seek sur ecran du BAS
       X : 10 a 310 (300px de large)
       Y : 28 a 48  (barre visible dans player_ui_draw_bottom) */
    int tx = g_input.touch.px;
    int ty = g_input.touch.py;

    if (g_input.touch_held && ty >= 220 && ty <= 240
        && tx >= 10 && tx <= 310)
    {
        /* Pendant le glissement : memoriser la position SANS seek
           pour eviter le freeze du DSP */
        float pct = (float)(tx - 10) / 300.f;
        if (pct < 0.f) pct = 0.f;
        if (pct > 1.f) pct = 1.f;
        s_touch_seeking  = true;
        s_touch_seek_pct = pct;
    }
    else if (s_touch_seeking && !g_input.touch_held)
    {
        /* Au relachement : seek UNE SEULE FOIS */
        audio_seek((int)(s_touch_seek_pct * dur));
        s_touch_seeking = false;
    }
}

/* ── Input: File browser ──────────────────────────────────── */
static void input_file_browser(void)
{
    u32 kd   = g_input.down;
    u32 held = g_input.held;

    /* ── Tactile file browser ────────────────────────────────
       Chaque ligne : Y = 30 + (i - scroll) * 22, H = 22
       Toucher une ligne = sélectionner
       Double tap = ouvrir (simulé par touch_down sur ligne déjà sélectionnée) */
    /* Flèches maintenues → défilement continu */
    if (g_input.touch_held) {
        int tx = g_input.touch.px;
        int ty = g_input.touch.py;
        if (tx >= 300 && tx <= 320 && ty >= 30 && ty <= 52) {
            if (touch_arrow_tick(true))
                fb_select_prev(&g_browser);
        } else if (tx >= 300 && tx <= 320 && ty >= 218 && ty <= 240) {
            if (touch_arrow_tick(false))
                fb_select_next(&g_browser);
        } else if (!g_input.touch_down) {
            /* Doigt tenu ailleurs → reset flèches */
            touch_arrow_reset();
        }
    }
    if (!g_input.touch_held) touch_arrow_reset();

    /* Tap sur liste ou flèches (premier contact) */
    if (g_input.touch_down) {
        int tx = g_input.touch.px;
        int ty = g_input.touch.py;

        /* Zone liste : Y >= 30, X < 300 */
        if (ty >= 30 && tx < 300) {
            int row = (ty - 30) / 22;
            int idx = g_browser.scroll_offset + row;

            if (idx >= 0 && idx < g_browser.count) {
                if (idx == g_browser.selected) {
                    /* Déjà sélectionné → ouvrir */
                    FileEntry *fe = &g_browser.entries[idx];
                    if (fe->is_dir) {
                        if (!strcmp(fe->name, "..")) nav_go_up();
                        else                         nav_enter(fe->full_path);
                    } else if (fe->is_audio) {
                        pl_clear(&g_playlist);
                        pl_add_dir(&g_playlist, g_browser.cwd);
                        for (int i = 0; i < g_playlist.count; i++) {
                            if (!strcmp(g_playlist.items[i].path,
                                        fe->full_path)) {
                                pl_jump(&g_playlist, i);
                                break;
                            }
                        }
                        play_current_track();
                    }
                } else {
                    /* Sélectionner */
                    g_browser.selected = idx;
                    if (g_browser.selected < g_browser.scroll_offset)
                        g_browser.scroll_offset = g_browser.selected;
                    if (g_browser.selected >= g_browser.scroll_offset
                                            + g_browser.visible_rows)
                        g_browser.scroll_offset = g_browser.selected
                                                 - g_browser.visible_rows + 1;
                }
            }
        }
    }

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
            if (!strcmp(fe->name, "..")) nav_go_up();
            else                         nav_enter(fe->full_path);
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

    if (kd & KEY_B) nav_go_up();

    if (kd & KEY_X) {
        if (g_browser.count == 0) return;
        FileEntry *fe = &g_browser.entries[g_browser.selected];
        if (fe->is_audio) pl_add(&g_playlist, fe->full_path, fe->name);
    }

    if (kd & KEY_Y)     pl_add_dir(&g_playlist, g_browser.cwd);
    if (kd & KEY_START) g_state = STATE_PLAYER;
    if (kd & KEY_SELECT)g_state = STATE_SETTINGS;
    if (kd & KEY_R)     fb_cycle_sort(&g_browser);
    /* L = toggle favori sur fichier audio selectionne */
    if (kd & KEY_L) {
        if (g_browser.count > 0) {
            FileEntry *fe = &g_browser.entries[g_browser.selected];
            if (fe->is_audio)
                fav_toggle(fe->full_path, fe->name);
        }
    }

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
        // Forcer prev même en REPEAT_ONE
        RepeatMode saved = g_playlist.repeat;
        if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;
        int n = pl_prev(&g_playlist);
        g_playlist.repeat = saved;
        if (n >= 0) play_current_track();
    }
    if (kd & KEY_RIGHT) {
        // Forcer next même en REPEAT_ONE
        RepeatMode saved = g_playlist.repeat;
        if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;
        int n = pl_next(&g_playlist);
        g_playlist.repeat = saved;
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

    if (kd & KEY_ZL) {
        { FILE *_f=fopen("sdmc:/3DSoundShell/debug.log","a");
          if(_f){fprintf(_f,"ZL pressed pos=%d\n",audio_get_position());fclose(_f);} }
        audio_seek(audio_get_position() - 10);
    }
    if (kd & KEY_ZR) {
        { FILE *_f=fopen("sdmc:/3DSoundShell/debug.log","a");
          if(_f){fprintf(_f,"ZR pressed pos=%d\n",audio_get_position());fclose(_f);} }
        audio_seek(audio_get_position() + 10);
    }
    if (kd & KEY_R) {
        player_ui_cycle_viz(&g_player_ui);
        g_settings.viz_style = g_player_ui.viz_style;
        settings_save();
    }
    if (kd & KEY_Y) { pl_toggle_shuffle(&g_playlist); g_settings.shuffle = g_playlist.shuffle; settings_save(); }
    if (kd & KEY_A)  audio_toggle_pause();
    if (kd & KEY_B)  g_state = STATE_FILE_BROWSER;
    if (kd & KEY_SELECT) g_state = STATE_FILE_BROWSER;
    if (kd & KEY_X)  g_state = STATE_PLAYLIST;

    handle_touch_seek();

    /* ── Boutons tactiles ecran du bas ──────────────────────────
       Coordonnees identiques a player_ui_draw_bottom :
       Prec    : X=14  a 94,  Y=36 a 56
       Pause   : X=110 a 190, Y=36 a 56
       Suiv    : X=222 a 302, Y=36 a 56 */
    if (g_input.touch_down) {
        int tx = g_input.touch.px;
        int ty = g_input.touch.py;

        /* Bouton <- Prec (X=14 a 94, Y=36 a 56) */
        if (tx >= 14 && tx <= 94 && ty >= 36 && ty <= 56) {
            RepeatMode saved = g_playlist.repeat;
            if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;
            int n = pl_prev(&g_playlist);
            g_playlist.repeat = saved;
            if (n >= 0) play_current_track();
        }
        /* Bouton Pause/Play (X=110 a 190, Y=36 a 56) */
        else if (tx >= 110 && tx <= 190 && ty >= 36 && ty <= 56) {
            audio_toggle_pause();
        }
        /* Bouton Suiv -> (X=222 a 302, Y=36 a 56) */
        else if (tx >= 222 && tx <= 302 && ty >= 36 && ty <= 56) {
            RepeatMode saved = g_playlist.repeat;
            if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;
            int n = pl_next(&g_playlist);
            g_playlist.repeat = saved;
            if (n >= 0) play_current_track();
            else        audio_stop();
        }
    }

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

    /* ── Tactile playlist ────────────────────────────────────
       Chaque ligne : Y = 30 + (i - scroll) * 22, H = 22
       Toucher une ligne = sélectionner
       Toucher ligne déjà sélectionnée = lancer la lecture */
    /* Flèches maintenues → défilement continu */
    if (g_input.touch_held) {
        int tx = g_input.touch.px;
        int ty = g_input.touch.py;
        if (tx >= 300 && tx <= 320 && ty >= 30 && ty <= 52) {
            if (touch_arrow_tick(true)) {
                if (g_playlist.selected > 0) {
                    g_playlist.selected--;
                    if (g_playlist.selected < g_playlist.scroll_offset)
                        g_playlist.scroll_offset--;
                }
            }
        } else if (tx >= 300 && tx <= 320 && ty >= 218 && ty <= 240) {
            if (touch_arrow_tick(false)) {
                if (g_playlist.selected < g_playlist.count - 1) {
                    g_playlist.selected++;
                    if (g_playlist.selected >= g_playlist.scroll_offset + vis)
                        g_playlist.scroll_offset++;
                }
            }
        } else if (!g_input.touch_down) {
            touch_arrow_reset();
        }
    }
    if (!g_input.touch_held) touch_arrow_reset();

    /* Tap sur liste */
    if (g_input.touch_down) {
        int tx = g_input.touch.px;
        int ty = g_input.touch.py;

        /* Zone liste : Y >= 30, X < 300 */
        if (ty >= 30 && tx < 300) {
            int row = (ty - 30) / 22;
            int idx = g_playlist.scroll_offset + row;

            if (idx >= 0 && idx < g_playlist.count) {
                if (idx == g_playlist.selected) {
                    /* Déjà sélectionné → lancer lecture */
                    pl_jump(&g_playlist, idx);
                    play_current_track();
                } else {
                    /* Sélectionner */
                    g_playlist.selected = idx;
                    if (g_playlist.selected < g_playlist.scroll_offset)
                        g_playlist.scroll_offset = g_playlist.selected;
                    if (g_playlist.selected >= g_playlist.scroll_offset + vis)
                        g_playlist.scroll_offset = g_playlist.selected
                                                  - vis + 1;
                }
            }
        }
    }

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

    /* Sauvegarder quand on quitte */
    if (g_input.down & KEY_B || g_input.down & KEY_START) {
        for (int i = 0; i < EQ_BANDS; i++)
            g_settings.eq_gains[i] = audio_eq_get_gain(i);
        settings_save();
        g_state = (g_input.down & KEY_B) ? STATE_SETTINGS : STATE_PLAYER;
        return;
    }

    /* Sync en temps reel */
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
        C2D_DrawText(&tx, C2D_AlignCenter|C2D_WithColor,
            200, 110, 0, 0.6f, 0.6f, current_theme->text_accent);

        C2D_TargetClear(g_bot, current_theme->bg_primary);
        C2D_SceneBegin(g_bot);
        C3D_FrameEnd(0);
        return;
    }

    /* Slider 3D (0.0 = plat, 1.0 = max relief) */
    float iod = osGet3DSliderState() * 6.f;

    /* ── Oeil GAUCHE (rendu principal) ── */
    C2D_TargetClear(g_top, current_theme->bg_primary);
    C2D_SceneBegin(g_top);

    if (g_loading) {
        static C2D_TextBuf load_tb = NULL;
        if (!load_tb) load_tb = C2D_TextBufNew(64);
        C2D_TextBufClear(load_tb);
        C2D_Text tx;
        C2D_TextParse(&tx, load_tb, "Chargement...");
        C2D_TextOptimize(&tx);
        C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, TOP_HEIGHT, current_theme->bg_primary);
        C2D_DrawText(&tx, C2D_AlignCenter|C2D_WithColor,
            200, 110, 0, 0.6f, 0.6f, current_theme->text_accent);
    } else {
        g_3d_iod = iod;
        g_3d_eye = 0; /* oeil gauche */
        switch (g_state) {
            case STATE_FILE_BROWSER: fb_draw_top(&g_browser);                             break;
            case STATE_PLAYER:       player_ui_draw_top(&g_player_ui, meta, &g_playlist); break;
            case STATE_PLAYLIST:     pl_draw_top(&g_playlist);                            break;
            case STATE_SETTINGS:     settings_draw_top();                                 break;
            case STATE_EQUALIZER:    eq_draw_top(g_eq_band);                              break;
        }
    }

    /* ── Oeil DROIT (decale pour effet 3D) ── */
    if (iod > 0.f && g_top_right) {
        C2D_TargetClear(g_top_right, current_theme->bg_primary);
        C2D_SceneBegin(g_top_right);
        if (g_loading) {
            static C2D_TextBuf load_tb_r = NULL;
            if (!load_tb_r) load_tb_r = C2D_TextBufNew(64);
            C2D_TextBufClear(load_tb_r);
            C2D_Text tx_r;
            C2D_TextParse(&tx_r, load_tb_r, "Chargement...");
            C2D_TextOptimize(&tx_r);
            C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, TOP_HEIGHT, current_theme->bg_primary);
            C2D_DrawText(&tx_r, C2D_AlignCenter|C2D_WithColor,
                200, 110, 0, 0.6f, 0.6f, current_theme->text_accent);
        } else if (!g_loading) {
            g_3d_eye = 1; /* oeil droit */
            switch (g_state) {
                case STATE_FILE_BROWSER: fb_draw_top(&g_browser);                             break;
                case STATE_PLAYER:       player_ui_draw_top(&g_player_ui, meta, &g_playlist); break;
                case STATE_PLAYLIST:     pl_draw_top(&g_playlist);                            break;
                case STATE_SETTINGS:     settings_draw_top();                                 break;
                case STATE_EQUALIZER:    eq_draw_top(g_eq_band);                              break;
            }
        }
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

/* ── Thread navigation fichiers (evite lag audio) ─────────── */
static volatile bool g_browser_loading = false;
static char s_nav_path[512] = {0};
static bool s_nav_go_up = false;

static void nav_thread_func(void *arg)
{
    (void)arg;
    if (s_nav_go_up)
        fb_go_up(&g_browser);
    else
        fb_enter_dir(&g_browser, s_nav_path);

    g_browser_loading = false;
}

static void nav_enter(const char *path)
{
    if (g_browser_loading) return;  // ignore si déjà en cours
    g_browser_loading = true;
    s_nav_go_up = false;
    strncpy(s_nav_path, path, 511);
    s_nav_path[511] = '\0';
    // Priorité basse (0x3F) pour ne pas perturber l'audio (0x18)
    threadCreate(nav_thread_func, NULL, 16*1024, 0x3F, -1, true);
}

static void nav_go_up(void)
{
    if (g_browser_loading) return;
    g_browser_loading = true;
    s_nav_go_up = true;
    threadCreate(nav_thread_func, NULL, 16*1024, 0x3F, -1, true);
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
    g_top_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    gfxSet3D(true); /* Activer 3D stereoscopique */
    if (!g_top || !g_bot || !g_top_right) {
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
    fav_load();
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

    /* Restaurer viz_style sauvegardé */
    g_player_ui.viz_style = g_settings.viz_style;
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

        /* Bloquer inputs pendant chargement */
        if (!g_loading && !g_browser_loading) {
            switch (g_state) {
                case STATE_FILE_BROWSER: input_file_browser(); break;
                case STATE_PLAYER:       input_player();       break;
                case STATE_PLAYLIST:     input_playlist();     break;
                case STATE_SETTINGS:     input_settings();     break;
                case STATE_EQUALIZER:    input_equalizer();    break;
            }
        }

        /* Synchroniser shuffle/repeat depuis settings vers playlist */
        if (g_playlist.shuffle != g_settings.shuffle) {
            g_playlist.shuffle = g_settings.shuffle;
            if (g_settings.shuffle) pl_rebuild_shuffle(&g_playlist);
        }
        if ((int)g_playlist.repeat != g_settings.repeat)
            g_playlist.repeat = (RepeatMode)g_settings.repeat;

        /* Sync viz_style settings -> player_ui en temps reel */
        if (g_player_ui.viz_style != g_settings.viz_style)
            g_player_ui.viz_style = g_settings.viz_style;

        player_ui_update(&g_player_ui, audio_get_metadata(), dt);

        /* Sauvegarde toutes les 2 min en arriere-plan */
        if (++save_timer > 7200) { /* 2 min a 60fps */
            save_timer = 0;
            if (audio_get_state() == AUDIO_PLAYING) {
                g_settings.last_position = audio_get_position();
                g_settings.last_track    = g_playlist.current;
                /* settings_save() est deja async (thread detache) */
                /* donc pas de freeze sur le thread principal */
                settings_save();
            }
        }

        draw_frame(dt);

        /* Limiteur FPS adaptatif :
           - 48000Hz en cours → limiter à 30fps (33ms) pour laisser CPU au resampling
           - 44100Hz natif   → limiter à 60fps (16ms) */
        u64 frame_end = osGetTime();
        u64 frame_ms  = frame_end - now;
        extern u32 audio_get_sample_rate(void);
        u32 sr = audio_get_sample_rate();
        u64 target_ms = (sr != 0 && sr != 44100) ? 33 : 16;
        if (frame_ms < target_ms)
            svcSleepThread((target_ms - frame_ms) * 1000000LL);
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