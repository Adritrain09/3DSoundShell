extern bool nd_session_active(void);
/*
** main.c — 3DSoundShell
** 3DSoundShell - Music Player by Adri
*/
#include <3ds.h>
#include <dirent.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
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
#include "preview.h"
#include "power_save.h"
#include "wifi_manager.h"
#include "sleep_timer.h"
#include "navidrome.h"
#include "app_mutex.h"
#include "recorder.h"

/* Variables globales 3D stéréoscopique */
float g_3d_iod = 0.f;
int   g_3d_eye = 0;
extern void audio_viz_frame_begin(void); /* CLOT9e */
int   g_update_notif_timer = 0;
bool  g_is_new3ds = false; /* détecté une fois au démarrage */
u8    s_startup_brightness = 5; /* V0.97 : luminosite au demarrage app */

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
static void check_alarm_trigger(void);
static void nav_go_up(void);

/* ── Global state ─────────────────────────────────────────── */
static u64 s_init_start_time = 0;  /* Timer pour detecter blocage init */
static volatile bool g_loading        = false;
static char          g_load_path[512] = {0};
AppState    g_state        = STATE_FILE_BROWSER;
static FileBrowser g_browser;
static FileBrowser g_picker_fb;
static Playlist    g_playlist;
static PlayerUI    g_player_ui;
static InputState  g_input;
static int         g_settings_sel = 0;
static int         g_eq_band      = 0;
static volatile bool g_init_done  = false;

/* ── Render targets ───────────────────────────────────────── */
C3D_RenderTarget *g_top       = NULL;
C3D_RenderTarget *g_top_right = NULL;
C3D_RenderTarget *g_bot = NULL;

/* ── Debug log ────────────────────────────────────────────── */
static void dbg(const char *msg) { (void)msg; }

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
        if (g_settings.playback_speed > 0.01f)
            audio_set_speed(g_settings.playback_speed);
        audio_play();
        player_ui_load_cover(&g_player_ui, audio_get_metadata());
        strncpy(g_settings.last_path, g_load_path, 511);
        g_settings.last_track    = g_playlist.current;
        g_settings.last_position = 0;
    }
    g_loading = false;
}

void main_restore_local_music(void)
{
    if (g_settings.last_path[0]) {
        audio_load(g_settings.last_path);
        audio_pause(); /* Garder en pause */
        player_ui_load_cover(&g_player_ui, audio_get_metadata());
    } else {
        player_ui_load_cover(&g_player_ui, NULL);
    }
}

void load_and_play(const char *path)
{
    if (g_loading) return; /* deja en chargement */
    app_mutex_claim(APP_OWNER_LOCAL_PLAYER); /* coupe Navidrome ET le magnetophone si actifs */
    nd_clear_playing(); /* une piste locale prend la main sur la lecture ND */
    audio_set_network_source(false);  /* EQ/SoundTouch redeviennent disponibles */
    strncpy(g_load_path, path, 511);
    g_loading = true;
    g_state   = STATE_PLAYER;

    Thread t = threadCreate(load_thread_func, NULL, 32*1024, 0x30, -1, true);
    if (!t) {
        /* Fallback synchrone si thread impossible */
        Result r = audio_load(path);
        if (!R_FAILED(r)) {
            audio_set_volume(g_settings.volume);
    if (g_settings.playback_speed > 0.01f)
        audio_set_speed(g_settings.playback_speed);
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
    /* V0.97 : ne pas forcer STATE_PLAYER, garder l ecran actuel */
    AppState saved_state = g_state;
    load_and_play(g_playlist.items[g_playlist.current].path);
    /* Si on etait ailleurs que dans le player, y rester */
    if (saved_state != STATE_PLAYER) g_state = saved_state;
}

/* ── Touch seek ───────────────────────────────────────────── */
/* Position tactile mémorisée pendant le glissement */
bool  s_touch_seeking = false;
float s_touch_seek_pct = 0.f;

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
        int threshold = 0;

        if (g_settings.prev_seek_threshold == 1) threshold = 2;

        else if (g_settings.prev_seek_threshold == 2) threshold = 5;

        else if (g_settings.prev_seek_threshold == 3) threshold = 10;

        extern int audio_get_position(void);

        if (threshold > 0 && audio_get_position() >= threshold) {

            extern void audio_seek(int seconds);

            audio_seek(0);

        } else {

            RepeatMode saved = g_playlist.repeat;

            if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;

            int n = pl_prev(&g_playlist);

            g_playlist.repeat = saved;

            if (n >= 0) play_current_track();

        }
    }
    if (kd & KEY_RIGHT) {
        RepeatMode saved = g_playlist.repeat;
        if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;
        int n = pl_next(&g_playlist);
        g_playlist.repeat = saved;
        if (n >= 0) play_current_track();
        else        audio_stop();
    }

    /* Volume */
    {   /* VOLBOOST3 : butee 100% ; si deja a 100% au moment de
           l'appui => monte jusqu'a 300% ; descente libre */
        static bool s_up_held = false, s_boost_arm = false;
        if (held & KEY_UP) {
            if (!s_up_held) s_boost_arm = (audio_get_volume() >= 0.999f);
            s_up_held = true;
            float cap = s_boost_arm ? 3.0f : 1.0f;
            float v = audio_get_volume() + 0.02f;
            if (v > cap) v = cap;
            audio_set_volume(v);
            g_settings.volume = v;
        } else {
            s_up_held = false;
        }
    }
    if (held & KEY_DOWN) {
        float v = audio_get_volume() - 0.02f;
        if (v < 0.f) v = 0.f;
        audio_set_volume(v);
        g_settings.volume = v;
    }
    if ((g_input.up & KEY_UP) || (g_input.up & KEY_DOWN))
        settings_mark_dirty();

    if (kd & KEY_ZL) audio_seek(audio_get_position() - 10);
    if (kd & KEY_ZR) audio_seek(audio_get_position() + 10);

    if (kd & KEY_R) {
        player_ui_cycle_viz(&g_player_ui);
        g_settings.viz_style = g_player_ui.viz_style;
        settings_mark_dirty();
    }
    if (kd & KEY_Y) { pl_toggle_shuffle(&g_playlist); g_settings.shuffle = g_playlist.shuffle; settings_mark_dirty(); }
    if (kd & KEY_A)  audio_toggle_pause();
    if (kd & KEY_B)  g_state = STATE_FILE_BROWSER;
    if (kd & KEY_SELECT) g_state = STATE_FILE_BROWSER;
    if (kd & KEY_X) {
        pl_load_favorites(&g_playlist);
        g_state = STATE_PLAYLIST;
    }

    handle_touch_seek();

    /* Boutons tactiles ecran du bas */
    if (g_input.touch_down) {
        int tx = g_input.touch.px;
        int ty = g_input.touch.py;

        if (tx >= 14 && tx <= 94 && ty >= 36 && ty <= 56) {
            int threshold = 0;

            if (g_settings.prev_seek_threshold == 1) threshold = 2;

            else if (g_settings.prev_seek_threshold == 2) threshold = 5;

            else if (g_settings.prev_seek_threshold == 3) threshold = 10;

            extern int audio_get_position(void);

            if (threshold > 0 && audio_get_position() >= threshold) {

                extern void audio_seek(int seconds);

                audio_seek(0);

            } else {

                RepeatMode saved = g_playlist.repeat;

                if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;

                int n = pl_prev(&g_playlist);

                g_playlist.repeat = saved;

                if (n >= 0) play_current_track();

            }
        }
        else if (tx >= 110 && tx <= 190 && ty >= 36 && ty <= 56) {
            audio_toggle_pause();
        }
        else if (tx >= 222 && tx <= 302 && ty >= 36 && ty <= 56) {
            RepeatMode saved = g_playlist.repeat;
            if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;
            int n = pl_next(&g_playlist);
            g_playlist.repeat = saved;
            if (n >= 0) play_current_track();
            else        audio_stop();
        }
        /* V0.97 : Bouton tactile play/pause sleep timer (bas droite) */
        else if (sleep_timer_is_active()
                 && tx >= 230 && tx <= 312
                 && ty >= 180 && ty <= 212) {
            if (sleep_timer_is_paused()) sleep_timer_resume();
            else sleep_timer_pause();
        }
    }

    /* V0.97 : Auto-avance deplacee dans la boucle main pour marcher partout */
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


    /* S7-NAV: Gauche/Droite = saut de 5 elements */
    if (kd & KEY_LEFT) {
        g_playlist.selected -= 5;
        if (g_playlist.selected < 0) g_playlist.selected = 0;
        if (g_playlist.selected < g_playlist.scroll_offset)
            g_playlist.scroll_offset = g_playlist.selected;
    }
    if (kd & KEY_RIGHT) {
        g_playlist.selected += 5;
        if (g_playlist.selected >= g_playlist.count) g_playlist.selected = g_playlist.count - 1;
        if (g_playlist.selected >= g_playlist.scroll_offset + vis)
            g_playlist.scroll_offset = g_playlist.selected - vis + 1;
    }

    /* S7-NAV: Auto-repeat Haut/Bas maintenu (400ms puis 80ms) */
    {
        static u64 s_pl_held_t = 0;
        static bool s_pl_rep = false;
        u32 held = hidKeysHeld();
        u64 now = osGetTime();
        bool want_up = (held & KEY_UP) != 0;
        bool want_dn = (held & KEY_DOWN) != 0;
        if (want_up || want_dn) {
            if (!s_pl_rep) {
                if (now - s_pl_held_t >= 400) { s_pl_rep = true; s_pl_held_t = now; }
                else { want_up = want_dn = false; }
            } else {
                if (now - s_pl_held_t >= 80) { s_pl_held_t = now; }
                else { want_up = want_dn = false; }
            }
        } else { s_pl_rep = false; s_pl_held_t = now; }

        if ((kd & KEY_DOWN || want_dn) && g_playlist.selected < g_playlist.count - 1) {
        g_playlist.selected++;
        if (g_playlist.selected >= g_playlist.scroll_offset + vis)
            g_playlist.scroll_offset++;
    }
    if ((kd & KEY_UP || want_up) && g_playlist.selected > 0) {
        g_playlist.selected--;
        if (g_playlist.selected < g_playlist.scroll_offset)
            g_playlist.scroll_offset--;
    } /* fin bloc auto-repeat */
    }

    /* S7-NAV: Gauche/Droite = saut de 5 elements */
    if (kd & KEY_LEFT) {
        g_playlist.selected -= 5;
        if (g_playlist.selected < 0) g_playlist.selected = 0;
        if (g_playlist.selected < g_playlist.scroll_offset)
            g_playlist.scroll_offset = g_playlist.selected;
    }
    if (kd & KEY_RIGHT) {
        g_playlist.selected += 5;
        if (g_playlist.selected >= g_playlist.count) g_playlist.selected = g_playlist.count - 1;
        if (g_playlist.selected >= g_playlist.scroll_offset + vis)
            g_playlist.scroll_offset = g_playlist.selected - vis + 1;
    }

    if (kd & KEY_A) {
        pl_jump(&g_playlist, g_playlist.selected);
        play_current_track();
    }
    if (kd & KEY_X)  pl_remove(&g_playlist, g_playlist.selected);
    if (kd & KEY_Y) { pl_toggle_shuffle(&g_playlist); g_settings.shuffle = g_playlist.shuffle; settings_mark_dirty(); }
    if (kd & KEY_B)  g_state = STATE_PLAYER;
    if (kd & KEY_SELECT) g_state = STATE_FILE_BROWSER;
    if (kd & KEY_START)  g_state = STATE_PLAYER;

    if ((kd & KEY_L) && (held & KEY_R)) audio_toggle_pause();
    if (kd & KEY_L) {
        const char *cur_path = g_settings.last_path;
        if (cur_path && cur_path[0]) {
            extern const AudioMetadata* audio_get_metadata(void);
            const AudioMetadata *m = audio_get_metadata();
            fav_toggle(cur_path, m->title[0] ? m->title : "Morceau");
        }
    }
    if ((kd & KEY_R) && (held & KEY_L)) audio_toggle_pause();
}

/* ── Input: Settings ──────────────────────────────────────── */
static void input_settings(void)
{
    AppState old_state = g_state;
    settings_handle_input(&g_settings_sel, g_input.down);
    if (g_input.down & KEY_B)      g_state = STATE_PLAYER;
    if (g_input.down & KEY_START)  g_state = STATE_PLAYER;
    if (g_input.down & KEY_SELECT) g_state = STATE_FILE_BROWSER;
    if (g_input.down & KEY_L)      g_state = STATE_EQUALIZER;
    /* V0.96 : force save quand on quitte settings pour eviter perte.
       settings_save_sync() est bloquant (attente + ~30 ecritures SD inline) :
       appele depuis la boucle de rendu, il gelait l'appli a chaque sortie de
       Settings. settings_save() fait la meme chose sur un thread detache. */
    if (old_state == STATE_SETTINGS && g_state != STATE_SETTINGS) {
        settings_save();
    }
}

/* ── Input: Equalizer ─────────────────────────────────────── */
static void input_equalizer(void)
{
    eq_handle_input(&g_eq_band, g_input.down);

    /* Sauvegarder quand on quitte */
    if (g_input.down & KEY_B || g_input.down & KEY_START) {
        for (int i = 0; i < EQ_BANDS; i++)
            g_settings.eq_gains[i] = audio_eq_get_gain(i);
        settings_mark_dirty();
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
    audio_viz_frame_begin(); /* CLOT9f : debut d image = droit a un tick */

    /* Ecran de chargement si init pas terminee */
    if (!g_init_done) {
        /* Demarrer le timer au 1er affichage */
        if (s_init_start_time == 0) s_init_start_time = osGetTime();
        u64 elapsed_ms = osGetTime() - s_init_start_time;
        bool show_help = (elapsed_ms >= 10000);

        /* Detection SELECT pour reboot si aide affichee */
        if (show_help) {
            hidScanInput();
            if (hidKeysDown() & KEY_SELECT) {
                APT_HardwareResetAsync();
            }
        }

        /* Fond theme normal (init n a jamais lieu en mode eco) */
        C2D_TargetClear(g_top, current_theme->bg_primary);
        C2D_SceneBegin(g_top);
        static C2D_TextBuf load_tb = NULL;
        if (!load_tb) load_tb = C2D_TextBufNew(512);
        C2D_TextBufClear(load_tb);
        C2D_Text tx;
        C2D_TextParse(&tx, load_tb, "Chargement...");
        C2D_TextOptimize(&tx);
        C2D_DrawText(&tx, C2D_AlignCenter|C2D_WithColor,
            200, 110, 0, 0.6f, 0.6f, current_theme->text_accent);

        /* Message d aide apres 10 sec */
        if (show_help) {
            u32 help_col = current_theme->text_accent;
            u32 box_bg   = C2D_Color32(0, 0, 0, 180);
            u32 box_br   = help_col;

            float bx = 20, by = 150, bw = TOP_WIDTH - 40, bh = 70;
            C2D_DrawRectSolid(bx, by, 0, bw, bh, box_bg);
            C2D_DrawRectSolid(bx, by, 0, bw, 1, box_br);
            C2D_DrawRectSolid(bx, by + bh - 1, 0, bw, 1, box_br);
            C2D_DrawRectSolid(bx, by, 0, 1, bh, box_br);
            C2D_DrawRectSolid(bx + bw - 1, by, 0, 1, bh, box_br);

            const char *l1_fr = "Si vous restez bloque,";
            const char *l2_fr = "tentez de redemarrer la console.";
            const char *l3_fr = "Appuyez sur SELECT pour redemarrer.";
            const char *l1_en = "If you are stuck,";
            const char *l2_en = "try restarting the console.";
            const char *l3_en = "Press SELECT to restart.";
            const char *l1 = (g_settings.language == 1) ? l1_en : l1_fr;
            const char *l2 = (g_settings.language == 1) ? l2_en : l2_fr;
            const char *l3 = (g_settings.language == 1) ? l3_en : l3_fr;

            C2D_Text t1, t2, t3;
            C2D_TextParse(&t1, load_tb, l1);
            C2D_TextParse(&t2, load_tb, l2);
            C2D_TextParse(&t3, load_tb, l3);
            C2D_TextOptimize(&t1);
            C2D_TextOptimize(&t2);
            C2D_TextOptimize(&t3);
            C2D_DrawText(&t1, C2D_AlignCenter|C2D_WithColor, 200, by + 8,  0, 0.44f, 0.44f, help_col);
            C2D_DrawText(&t2, C2D_AlignCenter|C2D_WithColor, 200, by + 26, 0, 0.44f, 0.44f, help_col);
            C2D_DrawText(&t3, C2D_AlignCenter|C2D_WithColor, 200, by + 50, 0, 0.42f, 0.42f, help_col);
        }

        C2D_TargetClear(g_bot, current_theme->bg_primary);
        C2D_SceneBegin(g_bot);
        C3D_FrameEnd(0);
        return;
    }
    /* Init terminee : reset le timer pour prochaine fois */
    s_init_start_time = 0;

    float iod = osGet3DSliderState() * 6.f;

    /* ── Oeil GAUCHE (rendu principal) ── */
    C2D_TargetClear(g_top, current_theme->bg_primary);
    C2D_SceneBegin(g_top);

    /* V0.97 : "Chargement..." SEULEMENT dans le player, ailleurs on rend normal */
    if (g_loading && g_state == STATE_PLAYER) {
        static C2D_TextBuf load_tb = NULL;
        if (!load_tb) load_tb = C2D_TextBufNew(64);
        C2D_TextBufClear(load_tb);
        C2D_Text tx;
        C2D_TextParse(&tx, load_tb, "Chargement...");
        C2D_TextOptimize(&tx);
        if (power_save_is_active()) {
            C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, TOP_HEIGHT, C2D_Color32(0, 0, 0, 255));
            C2D_DrawText(&tx, C2D_AlignCenter|C2D_WithColor,
                200, 110, 0, 0.7f, 0.7f, g_settings.power_save_text_color);
        } else {
            C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, TOP_HEIGHT, current_theme->bg_primary);
            C2D_DrawText(&tx, C2D_AlignCenter|C2D_WithColor,
                200, 110, 0, 0.6f, 0.6f, current_theme->text_accent);
        }
    } else if (power_save_is_active()) {
        /* V0.96 : Mode economie energie - ecran haut minimaliste */
        power_save_draw_top(meta);
    } else {
        g_3d_iod = iod;
        g_3d_eye = 0; /* oeil gauche */
        switch (g_state) {
            case STATE_FILE_BROWSER: fb_draw_top(&g_browser);                             break;
            case STATE_PLAYER:       player_ui_draw_top(&g_player_ui, meta, &g_playlist); break;
            case STATE_PLAYLIST:     pl_draw_top(&g_playlist);                            break;
            case STATE_SETTINGS:     settings_draw_top();                                 break;
            case STATE_DIR_PICKER:   dir_picker_draw_top();                              break;
            case STATE_NIGHT_MODE:   night_mode_draw();                                   break;
            case STATE_EQUALIZER:    eq_draw_top(g_eq_band);                              break;
            case STATE_NAVIDROME:   navidrome_draw_top();                             break;
            case STATE_RECORDER:    recorder_draw_top();                                 break;  /* ND1 */
        }
    }

    /* ── Oeil DROIT (decale pour effet 3D) ── */
    if (iod > 0.f && g_top_right) {
        C2D_TargetClear(g_top_right, current_theme->bg_primary);
        C2D_SceneBegin(g_top_right);
        if (g_loading && g_state == STATE_PLAYER) {
            static C2D_TextBuf load_tb_r = NULL;
            if (!load_tb_r) load_tb_r = C2D_TextBufNew(64);
            C2D_TextBufClear(load_tb_r);
            C2D_Text tx_r;
            C2D_TextParse(&tx_r, load_tb_r, "Chargement...");
            C2D_TextOptimize(&tx_r);
            if (power_save_is_active()) {
                C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, TOP_HEIGHT, C2D_Color32(0, 0, 0, 255));
                C2D_DrawText(&tx_r, C2D_AlignCenter|C2D_WithColor,
                    200, 110, 0, 0.7f, 0.7f, g_settings.power_save_text_color);
            } else {
                C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, TOP_HEIGHT, current_theme->bg_primary);
                C2D_DrawText(&tx_r, C2D_AlignCenter|C2D_WithColor,
                    200, 110, 0, 0.6f, 0.6f, current_theme->text_accent);
            }
        } else if (power_save_is_active()) {
            power_save_draw_top(meta);
        } else {
            g_3d_eye = 1; /* oeil droit (affiche toujours la page) */
            switch (g_state) {
                case STATE_FILE_BROWSER: fb_draw_top(&g_browser);                             break;
                case STATE_PLAYER:       player_ui_draw_top(&g_player_ui, meta, &g_playlist); break;
                case STATE_PLAYLIST:     pl_draw_top(&g_playlist);                            break;
                case STATE_SETTINGS:     settings_draw_top();                                 break;
            case STATE_DIR_PICKER:   dir_picker_draw_top();                              break;
            case STATE_NIGHT_MODE:   night_mode_draw();                                   break;
                case STATE_EQUALIZER:    eq_draw_top(g_eq_band);                              break;
                case STATE_NAVIDROME:   navidrome_draw_top();                             break;
            case STATE_RECORDER:    recorder_draw_top();                                 break;  /* ND1 */
            }
        }
    }

    /* Bottom screen */
    g_3d_eye = 0; /* CLOT9g : la passe du bas est le deuxieme droit au tick */
    C2D_TargetClear(g_bot, current_theme->bg_primary);
    C2D_SceneBegin(g_bot);
    if (power_save_is_active()) {
        /* V0.97 : mode eco - overlay noir sur ecran bas (backlight non touche) */
        C2D_DrawRectSolid(0, 0, 0, BOT_WIDTH, BOT_HEIGHT, C2D_Color32(0, 0, 0, 255));
    } else {
    switch (g_state) {
        case STATE_FILE_BROWSER: fb_draw_bottom(&g_browser);                           break;
        case STATE_PLAYER:       player_ui_draw_bottom(&g_player_ui, meta, &g_playlist); break;
        case STATE_PLAYLIST:     pl_draw_bottom(&g_playlist);                          break;
        case STATE_SETTINGS:     settings_draw_bottom(&g_settings_sel);               break;
        case STATE_DIR_PICKER:   fb_draw_bottom(&g_picker_fb);                         break;
        case STATE_NIGHT_MODE:   night_mode_draw();                                   break;
        case STATE_EQUALIZER:    eq_draw_bottom(g_eq_band);                           break;
        case STATE_NAVIDROME:   navidrome_draw_bottom();   break;
        case STATE_RECORDER:    recorder_draw_bottom();                          break;  /* ND1 */
        }
        extern void ddlc_draw_monika_stalker(void);
        if (g_state != STATE_PLAYER && g_state != STATE_NIGHT_MODE) {
            ddlc_draw_monika_stalker();
    }
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
        

        int found = 0;
        for (int i = 0; i < g_playlist.count; i++) {
            if (!strcmp(g_playlist.items[i].path, g_settings.last_path)) {
                pl_jump(&g_playlist, i);
                found = 1;
                break;
            }
        }
        if (!found) {
            if (g_playlist.count > 0) pl_jump(&g_playlist, 0);
        }

        /* Restaurer shuffle */
        g_playlist.shuffle = saved_shuffle;
        if (saved_shuffle) pl_rebuild_shuffle(&g_playlist);
    }

    g_init_done = true;
}


/* ============================================================
   SPLASH SCREEN — CONTINUATION DU LOGO BCMA
   Duree totale : 2 secondes
   
   Phase 1 (0.0 - 0.4s) : Ecran NOIR + "3DSOUNDSHELL" statique
                          = transition douce depuis le logo BCMA
   Phase 2 (0.4 - 1.5s) : EQ + particules apparaissent autour du texte
   Phase 3 (1.5 - 2.0s) : Fade out vers le file browser
   ============================================================ */
typedef struct {
    float x, y;
    float vx, vy;
    float life;
    u8 r, g, b;
} SplashParticle;

#define SP_PARTICLES 35

static void draw_splash_screen(void)
{
    static C2D_TextBuf splash_tb = NULL;
    if (!splash_tb) splash_tb = C2D_TextBufNew(512);

    int SPLASH_FRAMES;
    /* Duree splash selon settings : 0=OFF, 1-7 = 0.2s a 3s */
    const int dur_frames[] = {0, 12, 30, 60, 90, 120, 150, 180};
    int d = g_settings.splash_duration;
    if (d < 0 || d > 7) d = 5;
    SPLASH_FRAMES = dur_frames[d];
    if (SPLASH_FRAMES == 0) return;  /* OFF = pas de splash */  /* 2s a 60fps */
    const int   NUM_BARS      = 20;
    float       bar_heights[20] = {0};
    float       bar_targets[20];
    SplashParticle particles[SP_PARTICLES];

    Theme *th = current_theme;

    /* Init barres EQ */
    for (int i = 0; i < NUM_BARS; i++)
        bar_targets[i] = 0.3f + (float)(rand() % 70) / 100.f;

    /* Init particules */
    for (int i = 0; i < SP_PARTICLES; i++) {
        particles[i].x = (float)(rand() % TOP_WIDTH);
        particles[i].y = (float)TOP_HEIGHT + (float)(rand() % 50);
        particles[i].vx = ((float)(rand() % 20) - 10.f) * 0.05f;
        particles[i].vy = -0.4f - (float)(rand() % 20) / 50.f;
        particles[i].life = (float)(rand() % 100) / 100.f;
        u32 col = th->visualizer_bars[rand() % 8];
        particles[i].r = (col >> 0)  & 0xff;
        particles[i].g = (col >> 8)  & 0xff;
        particles[i].b = (col >> 16) & 0xff;
    }

    for (int frame = 0; frame < SPLASH_FRAMES; frame++) {
        if (!aptMainLoop()) return;
        hidScanInput();
        if (hidKeysDown() & (KEY_START | KEY_A | KEY_B)) return;

        float t = (float)frame / (float)SPLASH_FRAMES;

        /* Phases :
           0.0  - 0.4  : Texte 3DSOUNDSHELL deja visible (transition BCMA)
           0.2  - 0.75 : EQ + particules apparaissent
           0.8  - 1.0  : Fade out global */
        float text_alpha = 1.f;
        float eq_alpha   = 0.f;
        float fadeout    = 1.f;

        if (t > 0.2f && t < 0.75f) eq_alpha = (t - 0.2f) / 0.55f;
        else if (t >= 0.75f)       eq_alpha = 1.f;

        if (t > 0.8f) fadeout = 1.f - (t - 0.8f) / 0.2f;

        /* Update particules */
        for (int i = 0; i < SP_PARTICLES; i++) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].life -= 0.006f;
            if (particles[i].y < -10.f || particles[i].life <= 0.f) {
                particles[i].x = (float)(rand() % TOP_WIDTH);
                particles[i].y = (float)TOP_HEIGHT + 10.f;
                particles[i].life = 1.f;
            }
        }

        /* Update barres EQ */
        for (int i = 0; i < NUM_BARS; i++) {
            float target = bar_targets[i] * eq_alpha
                         * (0.6f + 0.4f * sinf((float)frame * 0.15f + i * 0.5f));
            bar_heights[i] += (target - bar_heights[i]) * 0.2f;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        /* ===== ECRAN HAUT ===== */
        C2D_TargetClear(g_top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(g_top);

        /* Fond noir profond (transition douce depuis BCMA) */
        C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, TOP_HEIGHT,
            C2D_Color32(0, 0, 0, 255));

        /* Particules en fond */
        for (int i = 0; i < SP_PARTICLES; i++) {
            u8 pa = (u8)(180 * particles[i].life * eq_alpha * fadeout);
            C2D_DrawRectSolid(particles[i].x, particles[i].y, 0, 2, 2,
                C2D_Color32(particles[i].r, particles[i].g, particles[i].b, pa));
        }

        /* APPEL DU VRAI VISUALISEUR DU THÈME DANS LE SPLASH */
        float splash_vw = 260.f; /* Taille reduite elegante pour l'ecran de demarrage */
        float splash_vh = 42.f;
        float splash_vx = (TOP_WIDTH - splash_vw) / 2.f;
        float splash_vy = 152.f;
        player_ui_draw_splash_viz(g_settings.viz_style, splash_vx, splash_vy, splash_vw, splash_vh, eq_alpha * fadeout, frame);

        /* LOGO "3DSOUNDSHELL" - GROS, CENTRE, BLANC sur NOIR
           Position fixe pour continuite avec le BCMA */
        C2D_TextBufClear(splash_tb);
        C2D_Text tx;
        C2D_TextParse(&tx, splash_tb, "3DSOUNDSHELL");
        C2D_TextOptimize(&tx);
        u8 alpha = (u8)(255 * text_alpha * fadeout);

        /* Effet glow autour du texte (4 directions) */
        u32 glow_col = C2D_Color32(180, 200, 255,
            (u8)(80 * text_alpha * fadeout));
        float glow_offsets[][2] = {{-2,-2},{2,-2},{-2,2},{2,2}};
        for (int gi = 0; gi < 4; gi++) {
            C2D_DrawText(&tx, C2D_AlignCenter | C2D_WithColor,
                TOP_WIDTH/2.f + glow_offsets[gi][0], 95 + glow_offsets[gi][1],
                0, 0.85f, 0.85f, glow_col);
        }

        /* Texte BLANC principal */
        u32 white = C2D_Color32(255, 255, 255, alpha);
        C2D_DrawText(&tx, C2D_AlignCenter | C2D_WithColor,
            TOP_WIDTH/2.f, 95, 0, 0.85f, 0.85f, white);

        /* MAJ disponible : affiche "Nouvelle version disponible !" */
        if (g_update_available && g_latest_version[0]) {
            C2D_TextBufClear(splash_tb);
            C2D_Text tx_maj;
            char maj_str[64];
            snprintf(maj_str, 64, "Nouvelle version V%s disponible !",
                g_latest_version);
            C2D_TextParse(&tx_maj, splash_tb, maj_str);
            C2D_TextOptimize(&tx_maj);
            u8 maj_a = (u8)(220 * eq_alpha * fadeout);
            u32 maj_col = C2D_Color32(255, 200, 80, maj_a);
            C2D_DrawText(&tx_maj, C2D_AlignCenter | C2D_WithColor,
                TOP_WIDTH/2.f, 220, 0, 0.45f, 0.45f, maj_col);
        }

        /* ===== ECRAN BAS ===== */
        C2D_TargetClear(g_bot, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(g_bot);
        C2D_DrawRectSolid(0, 0, 0, BOT_WIDTH, BOT_HEIGHT,
            C2D_Color32(0, 0, 0, 255));

        /* Hint discret */
        if (eq_alpha > 0.3f && fadeout > 0.5f) {
            C2D_TextBufClear(splash_tb);
            C2D_Text tx2;
            C2D_TextParse(&tx2, splash_tb, "START pour passer");
            C2D_TextOptimize(&tx2);
            u8 hint_a = (u8)(80 * fadeout);
            u32 hint_col = C2D_Color32(100, 100, 120, hint_a);
            C2D_DrawText(&tx2, C2D_AlignCenter | C2D_WithColor,
                BOT_WIDTH/2.f, 220, 0, 0.40f, 0.40f, hint_col);
        }

        C3D_FrameEnd(0);
    }
}



/* ============================================================
   APT HOOK - re-applique LED apres fermeture/notification
   ============================================================ */
static aptHookCookie s_apt_cookie;
static volatile bool s_force_led_refresh = false;

static void apt_hook_callback(APT_HookType hook, void* param)
{
    (void)param;
    switch (hook) {
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            { extern void recorder_pause_for_sleep(void); recorder_pause_for_sleep(); }
            /* V0.96 : avant HOME/veille → forcer restauration ecran
               pour ne pas laisser le systeme geler avec un ecran off custom */
            power_save_force_restore();
            /* V0.97 : power_save_force_restore a deja restaure la vraie lum */
            /* Rien de plus a faire ici */
            /* V0.96 : IMPERATIF reactiver le Wi-Fi avant HOME/veille
               sinon le systeme reste sans Wi-Fi ! */
            wifi_manager_enable();
            break;
        case APTHOOK_ONWAKEUP:
            { extern void recorder_resume_from_sleep(void); recorder_resume_from_sleep(); }
        case APTHOOK_ONRESTORE:
            /* Console reouverte ou retour depuis HOME */
            /* REVEIL1 : au reveil, le volume retourne a 100% */
            audio_set_volume(1.0f);
            g_settings.volume = 1.0f;
            settings_mark_dirty();
            s_force_led_refresh = true;
            /* V0.97 : resync lum systeme (user peut l avoir change en HOME) */
            power_save_sync_brightness();
            /* V0.96 : ré-appliquer le mode eco si actif */
            power_save_refresh();
            /* V0.96 : recouper le wifi si option ON (sauf si on est sur Navidrome !) */
            extern bool nd_session_active(void);
            if (g_settings.wifi_off_in_app && !nd_session_active()) wifi_manager_disable();
            break;

        case APTHOOK_ONEXIT:
            /* V0.97 : IMPERATIF reactiver le wifi avant fermeture brutale */
            wifi_manager_enable();
            /* V0.97 : juste rallumer les backlight (on ne touche pas au brightness) */
            {
                gspLcdInit();
                GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM);
                gspLcdExit();
            }
            break;
        default:
            break;
    }
}

/* ============================================================
   LED NOTIFICATION
   - Utilise la couleur du theme actuel (current_theme->led_color)
   - Pulse doucement quand l audio joue
   - Eteinte sinon
   ============================================================ */
static bool s_led_active = false;
static u64  s_led_last_check = 0;
static u32  s_led_last_color = 0;

static void update_led(void)
{
    u64 now = osGetTime();

    /* Si refresh force (wakeup/restore), reset l etat */
    if (s_force_led_refresh) {
        s_force_led_refresh = false;
        s_led_active = false;  /* Force la re-application */
        s_led_last_check = 0;
    }

    /* Check toutes les 500ms */
    if (now - s_led_last_check < 500) return;
    s_led_last_check = now;

    /* Etat desire */
    bool should_be_on = false;
    if (g_settings.led_enabled) {
        AudioState st = audio_get_state();
        if (st == AUDIO_PLAYING) should_be_on = true;
    }
    /* V0.96 : eteindre LED en mode eco energie */
    if (power_save_is_active()) should_be_on = false;

    /* === DETECTION DES NOTIFICATIONS SYSTEME ===
       On lit l etat actuel de la LED via MCUHWC_ReadRegister
       Si une notification est en cours, on attend qu elle finisse */
    static u32 s_notif_wait_until = 0;
    static bool s_was_notif_active = false;

    if (should_be_on) {
        /* Lire l etat actuel de la LED (registre 0x2D = info LED state) */
        u8 led_state = 0;
        Result rc = MCUHWC_ReadRegister(0x28, &led_state, 1);

        /* Si une notification est detectee (LED a un pattern non-null),
           et que ce n est PAS notre LED, on attend
           led_state != 0 et !s_led_active = quelqu un d autre a pris la LED */
        if (R_SUCCEEDED(rc) && led_state != 0 && !s_led_active) {
            /* Notification active : on attend 5 sec qu elle finisse */
            if (!s_was_notif_active) {
                s_was_notif_active = true;
                s_notif_wait_until = (u32)(now + 5000); /* attendre 5 sec */
            }
            /* Pas encore fini d attendre ? */
            if (now < s_notif_wait_until) {
                return; /* Skip cette frame, laisser la notif tranquille */
            }
            /* La notif devrait etre finie, on reprend la main */
            s_was_notif_active = false;
        } else {
            s_was_notif_active = false;
        }
    }

    /* Re-applique si la couleur du theme a change */
    u32 cur_color = current_theme->led_color;
    bool color_changed = (s_led_active && cur_color != s_led_last_color);

    /* Re-application periodique pour reprendre apres notifications
       (mais moins agressif maintenant : toutes les 5 sec) */
    static u64 s_led_force_refresh_time = 0;
    bool force_refresh = false;
    if (should_be_on && (now - s_led_force_refresh_time >= 5000)) {
        s_led_force_refresh_time = now;
        force_refresh = true;
    }

    if ((should_be_on && !s_led_active) || color_changed || force_refresh) {
        InfoLedPattern p = {0};
        p.delay = 0x10;
        p.smoothing = 0x40;
        p.loopDelay = 0x00;
        p.blinkSpeed = 0x00;

        u8 base_r = (cur_color >> 0)  & 0xff;
        u8 base_g = (cur_color >> 8)  & 0xff;
        u8 base_b = (cur_color >> 16) & 0xff;

        /* Pulsation : 32 steps sinusoidaux */
        for (int i = 0; i < 32; i++) {
            float t = (float)i / 32.f;
            float intensity = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t * 6.28318f));
            p.redPattern[i]   = (u8)(base_r * intensity);
            p.greenPattern[i] = (u8)(base_g * intensity);
            p.bluePattern[i]  = (u8)(base_b * intensity);
        }
        MCUHWC_SetInfoLedPattern(&p);
        s_led_active = true;
        s_led_last_color = cur_color;
    }
    else if (!should_be_on && s_led_active) {
        InfoLedPattern off = {0};
        off.delay = 0x10;
        off.loopDelay = 0xFF;
        MCUHWC_SetInfoLedPattern(&off);
        s_led_active = false;
    }
}

/* ── Main ─────────────────────────────────────────────────── */
/* Prototypes externes */
extern void ping_stats_start(void);


static void clean_temp_dir(void) {
    remove("sdmc:/3DSoundShell/Temp/nd_play.mp3");
    remove("sdmc:/3DSoundShell/Temp/cover_temp.png");
    remove("sdmc:/3DSoundShell/Temp/cover_temp.jpg");

    DIR *d = opendir("sdmc:/3DSoundShell/Temp/pochettes");
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_name[0] == '.') continue;
            char p[256];
            snprintf(p, sizeof(p), "sdmc:/3DSoundShell/Temp/pochettes/%s", dir->d_name);
            remove(p);
        }
        closedir(d);
    }
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* ── 1. Services système de base ─────────────────────── */
    romfsInit();
    gfxInitDefault();

    /* Créer le dossier debug AVANT tout */
    mkdir("sdmc:/3DSoundShell", 0777);
    mkdir("sdmc:/3DSoundShell/Temp", 0777);
    mkdir("sdmc:/3DSoundShell/Temp/pochettes", 0777);
    clean_temp_dir();
    mkdir("sdmc:/Music",        0777);

    /* Vider / créer le log */

    /* ── 2. Citro ─────────────────────────────────────────── */
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        gfxExit(); romfsExit(); return 1;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini(); gfxExit(); romfsExit(); return 1;
    }
    C2D_Prepare();

    /* ── 3. Render targets ────────────────────────────────── */
    g_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_top_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    gfxSet3D(true); /* Activer 3D stereoscopique */
    if (!g_top || !g_bot || !g_top_right) {
        C2D_Fini(); C3D_Fini(); gfxExit(); romfsExit(); return 1;
    }

    /* =========================================================================
       3DSS-FIX: MODE SANS ECHEC INTERACTIF - Touche SELECT au demarrage
       ========================================================================= */
    hidScanInput();
    if (hidKeysHeld() & KEY_SELECT) {
        bool do_reset = false;
        
        C2D_TextBuf safe_tb = C2D_TextBufNew(512);
        C2D_Text txt_title, txt_question, txt_yes, txt_no;
        C2D_TextParse(&txt_title, safe_tb, "[ MODE SANS ECHEC ]");
        C2D_TextParse(&txt_question, safe_tb, "Reinitialiser l'application ?");
        C2D_TextParse(&txt_yes, safe_tb, "A : OUI (Effacer cache & parametres)");
        C2D_TextParse(&txt_no, safe_tb, "B : NON (Lancer normalement)");
        
        C2D_TextOptimize(&txt_title);
        C2D_TextOptimize(&txt_question);
        C2D_TextOptimize(&txt_yes);
        C2D_TextOptimize(&txt_no);

        /* Boucle d attente du choix utilisateur */
        while (aptMainLoop()) {
        /* Tick de relance automatique Navidrome universel à 60 FPS (Zéro délai) */
        extern void nd_pending_play_tick(void);
        nd_pending_play_tick();

            hidScanInput();
            u32 kDown = hidKeysDown();
        if (power_save_is_active() || power_save_should_suppress_b()) kDown &= ~KEY_B;

            if (kDown & KEY_A) { do_reset = true; break; }
            if (kDown & KEY_B) { do_reset = false; break; }

            /* Rendu 3D/2D securise avec FrameBegin / FrameEnd */
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

            /* Ecran du haut (Alerte sombre) */
            C2D_TargetClear(g_top, C2D_Color32(40, 20, 20, 255));
            C2D_SceneBegin(g_top);
            C2D_DrawText(&txt_title, C2D_WithColor, 100, 40, 0.5f, 0.7f, 0.7f, C2D_Color32(255, 80, 80, 255));
            C2D_DrawText(&txt_question, C2D_WithColor, 60, 110, 0.5f, 0.55f, 0.55f, C2D_Color32(255, 255, 255, 255));

            /* Ecran du bas (Choix A / B) */
            C2D_TargetClear(g_bot, C2D_Color32(20, 20, 30, 255));
            C2D_SceneBegin(g_bot);
            C2D_DrawRectSolid(30, 50, 0, 260, 50, C2D_Color32(120, 30, 30, 255));
            C2D_DrawText(&txt_yes, C2D_WithColor, 40, 65, 0.5f, 0.45f, 0.45f, C2D_Color32(255, 255, 255, 255));

            C2D_DrawRectSolid(30, 130, 0, 260, 50, C2D_Color32(30, 120, 30, 255));
            C2D_DrawText(&txt_no, C2D_WithColor, 40, 145, 0.5f, 0.45f, 0.45f, C2D_Color32(255, 255, 255, 255));

            C3D_FrameEnd(0);
        }

        C2D_TextBufDelete(safe_tb);

        /* Execution du reset uniquement si l utilisateur a appuyé sur A */
        if (do_reset) {
            remove("sdmc:/3DSoundShell/settings.ini");
            remove("sdmc:/3DSoundShell/theme.ini");
            remove("sdmc:/3DSoundShell/favorites.txt");
            remove("sdmc:/3DSoundShell/nd_favorites.dat");
            remove("sdmc:/3DSoundShell/version.txt");
            remove("sdmc:/3DSoundShell/soundshell_perf.txt");
            remove("sdmc:/3DSoundShell/debug.log");
            remove("sdmc:/3DSoundShell/screen_debug.log");
            remove("sdmc:/3DSoundShell/perf_v2.log");
            remove("sdmc:/3DSoundShell/update_debug.log");
            remove("sdmc:/3DSoundShell/shutdown_debug.log");
            remove("sdmc:/3DSoundShell/ping_v10.log");
            remove("sdmc:/3DSoundShell/lum_cfgu_725029.log");
            remove("sdmc:/3DSoundShell/Temp/nd_play.mp3");

            /* Vider le cache des pochettes */
            DIR *d = opendir("sdmc:/3DSoundShell/Temp/pochettes");
            if (d) {
                struct dirent *dir;
                while ((dir = readdir(d)) != NULL) {
                    char p[256];
                    snprintf(p, sizeof(p), "sdmc:/3DSoundShell/Temp/pochettes/%s", dir->d_name);
                    remove(p);
                }
                closedir(d);
            }

            /* Quitte proprement vers le menu HOME de la 3DS */
            C2D_Fini();
            C3D_Fini();
            gfxExit();
            romfsExit();
            return 0;
        }
    }
    /* ========================================================================= */

    /* ── 4. Services optionnels ───────────────────────────── */
    cfguInit();
    ptmuInit();
    mcuHwcInit();

    /* V0.97 : test CFGU pour lum */
    {
        u8 lum = 0;
        Result r = CFGU_GetConfigInfoBlk2(1, 0x00050001, &lum);

        if (R_SUCCEEDED(r) && lum >= 1 && lum <= 5) s_startup_brightness = lum;
        else s_startup_brightness = 5;
    }

    srand((unsigned)osGetTime());

    /* Enregistrer hook APT pour refresh LED apres wakeup/restore */
    aptHook(&s_apt_cookie, apt_hook_callback, NULL);
    APT_CheckNew3DS(&g_is_new3ds); /* détection modèle une seule fois */

    /* V0.97 : Sur New 3DS, liberer les cores caches (30% -> 80% CPU dispo)
       Necessaire pour le WSOLA en temps reel */
    if (g_is_new3ds) {
        APT_SetAppCpuTimeLimit(g_is_new3ds ? 80 : 30); /* CLOT10g : le Old a 2 coeurs, la quote lui sert aussi */
    }

    /* ── 5. Thème & settings ──────────────────────────────── */
    theme_init();
    settings_load();
    fav_load();
    theme_set(theme_get(g_settings.theme_index));
    acInit(); /* Réseau pour vérif MAJ */
    /* V0.96 : ne lance QUE update_check, qui declenchera ping_stats
       apres pour eviter contention HTTPC */
    update_check_start(); /* timer demarre dans update.c */

    /* ── 6. Audio (NDSP) ──────────────────────────────────── */
    Result audio_rc = audio_init();
    
    if (R_FAILED(audio_rc)) {
        /* On ne quitte pas, on continue en mode silencieux */
    }
    audio_set_volume(g_settings.volume);
    if (g_settings.playback_speed > 0.01f)
        audio_set_speed(g_settings.playback_speed);

    /* EQ : les reglages utilisateur sont ecrits dans eq_gains_per_preset[]
       (equalizer.c), pas dans le vieux tableau plat eq_gains[] - celui-ci
       n est plus mis a jour que par les presets d usine au demarrage. Lire
       eq_gains[] ici rechargeait donc les valeurs par defaut/perimees a
       chaque lancement au lieu des reglages EQ personnalises de l utilisateur
       (bien sauvegardes sur la carte SD, juste jamais relus). */
    {
        int pi = g_settings.eq_preset_index;
        if (pi < 0 || pi > 6) pi = 0;
        for (int i = 0; i < EQ_BANDS; i++)
            audio_eq_set_gain(i, g_settings.eq_gains_per_preset[pi][i]);
    }

    /* V0.95 : Preview metadonnees asynchrone */
    preview_init();

    /* V0.96 : Mode economie energie */
    power_save_init();

    /* V0.96 : Wi-Fi manager (init tot mais coupe le wifi seulement apres 20s si option ON) */
    wifi_manager_init();

    /* V0.97 : Sleep timer */
    sleep_timer_init();
    if (g_settings.sleep_timer_min > 0)
        sleep_timer_start(g_settings.sleep_timer_min);

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
        main_get_start_dir(),
        MAX_PATH-1);

    /* Restaurer viz_style sauvegardé */
    g_player_ui.viz_style = g_settings.viz_style;

    /* ── 7b. SPLASH SCREEN ANIME ───────────────────────── */
    draw_splash_screen();

    /* ── 8. Thread d init SD (scan en arriere-plan) ─────── */
    static char s_init_path[512];
    strncpy(s_init_path,
        main_get_start_dir(),
        511);

    threadCreate(init_thread_func, s_init_path, 32*1024, 0x30, -1, true);

    /* ── Boucle principale ────────────────────────────────── */
    u64 last_time    = osGetTime();
    int save_timer   = 0;
    bool resumed     = false;

    while (aptMainLoop()) {
        extern void audio_update_wobble(void); audio_update_wobble();
        extern void audio_update_fade(void); audio_update_fade();
        /* Tick de relance automatique Navidrome universel à 60 FPS (Zéro délai) */
        extern void nd_pending_play_tick(void);
        nd_pending_play_tick();
        /* --- 3DSS: Maintien du flux micro a 60 FPS pendant l enregistrement --- */
        if (g_state == STATE_RECORDER) {
            extern void recorder_update(void);
            recorder_update();
        }
        /* Gestion dynamique temps-reel du Bruit Blanc & detection Jack.
           Etait avant dans input_playlist() : ne tournait donc QUE sur
           l ecran Playlist, quasiment jamais visite en usage normal (la
           lecture se passe sur l ecran Player) - la fonctionnalite "anti-
           veille Bluetooth" ne se declenchait donc pour ainsi dire jamais.
           Deplace ici : tourne a chaque image, quel que soit l ecran. */
        if (g_settings.white_noise_keepalive && (audio_is_finished() || audio_get_state() == AUDIO_PAUSED || !nd_get_playing())) {
            bool hp = false;
            if (R_SUCCEEDED(DSP_GetHeadphoneStatus(&hp)) && hp) {
                if (!audio_is_white_noise_active()) {
                    audio_start_white_noise();
                } else {
                    extern void audio_update_white_noise_volume(void);
                    audio_update_white_noise_volume();
                }
            } else {
                if (audio_is_white_noise_active()) {
                    audio_stop_white_noise();
                }
            }
        } else {
            if (audio_is_white_noise_active()) {
                audio_stop_white_noise();
            }
        }
        /* ── GESTION LECTURE CLAPET FERMÉ ───────────────────────── */
        if (g_settings.lid_playback_enabled) {
            u8 shell_open = 1;
            PTMU_GetShellState(&shell_open);
            static bool s_lid_was_closed = false;

            if (!shell_open) {
                if (!s_lid_was_closed) {
                    /* On accompagne l'extinction matérielle pour économiser le GPU */
                    GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP, 1);
                    GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
                    s_lid_was_closed = true;
                }
                svcSleepThread(50000000ULL); /* Sommeil 50ms (Economie batterie) */
            } else if (s_lid_was_closed) {
                GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
                extern u8 s_startup_brightness;
                GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP, s_startup_brightness > 0 ? s_startup_brightness : 5);
                /* REVEIL1B : ouverture clapet = reveil (pas de veille
                   systeme quand l'option lecture clapet est active,
                   donc les hooks APT ne tirent pas ici) -> volume 100% */
                audio_set_volume(1.0f);
                g_settings.volume = 1.0f;
                settings_mark_dirty();
                s_lid_was_closed = false;
            }
        }

        /* -- REVEIL2 : clapet ferme + option lecture clapet + pas de
             casque => pause audio jusqu'a casque branche ou clapet
             ouvert. Erreur de lecture jack = considere branche. -- */
        if (g_settings.lid_playback_enabled) {
            u8 sh = 1;
            PTMU_GetShellState(&sh);
            bool hp = true;
            if (R_FAILED(DSP_GetHeadphoneStatus(&hp))) hp = true;
            static bool s_jack_paused = false;
            if (!sh && !hp) {
                if (!s_jack_paused && audio_get_state() == AUDIO_PLAYING) {
                    audio_pause();
                    s_jack_paused = true;
                }
            } else if (s_jack_paused) {
                if (audio_get_state() == AUDIO_PAUSED) audio_play();
                s_jack_paused = false;
            }
        }

        check_alarm_trigger();
        u64   now = osGetTime();
        float dt  = (now - last_time) / 1000.f;
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        input_update(&g_input);

        /* V0.97 : power_save_update gere B hold 500ms pour quitter */
        power_save_update(g_input.down | g_input.held, g_input.touch_down);

        /* V0.97 : En mode eco, intercepter TOUTES les touches
           Seules certaines actions sont autorisees (economie CPU) */
        if (power_save_is_active()) {
            /* A = pause/play */
            if (g_input.down & KEY_A) {
                audio_toggle_pause();
            }

            /* Fleche gauche/droite = changer de piste */
            if (g_input.down & KEY_LEFT) {
                int threshold = 0;

                if (g_settings.prev_seek_threshold == 1) threshold = 2;

                else if (g_settings.prev_seek_threshold == 2) threshold = 5;

                else if (g_settings.prev_seek_threshold == 3) threshold = 10;

                extern int audio_get_position(void);

                if (threshold > 0 && audio_get_position() >= threshold) {

                    extern void audio_seek(int seconds);

                    audio_seek(0);

                } else {

                    RepeatMode saved = g_playlist.repeat;

                    if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;

                    int n = pl_prev(&g_playlist);

                    g_playlist.repeat = saved;

                    if (n >= 0) play_current_track();

                }
            }
            if (g_input.down & KEY_RIGHT) {
                RepeatMode saved = g_playlist.repeat;
                if (saved == REPEAT_ONE) g_playlist.repeat = REPEAT_ALL;
                int n = pl_next(&g_playlist);
                g_playlist.repeat = saved;
                if (n >= 0) play_current_track();
                else        audio_stop();
            }

            /* L + R = changer couleur horloge (cycle presets) */
            if ((g_input.down & KEY_L) && (g_input.held & KEY_R)) {
                g_settings.power_save_color_mode = (g_settings.power_save_color_mode + 1) % 11;
                settings_mark_dirty();
            } else if ((g_input.down & KEY_R) && (g_input.held & KEY_L)) {
                g_settings.power_save_color_mode = (g_settings.power_save_color_mode + 1) % 11;
                settings_mark_dirty();
            }

            /* Y = pause/play sleep timer si actif */
            if (g_input.down & KEY_Y) {
                if (sleep_timer_is_active()) {
                    if (sleep_timer_is_paused()) sleep_timer_resume();
                    else sleep_timer_pause();
                }
            }

            /* B = quitter mode eco (gere par power_save_update via hold 500ms) */
            /* Consommer B pour ne pas faire retour */
            g_input.down &= ~KEY_B;
            g_input.held &= ~KEY_B;

            /* TOUT LE RESTE = consomme (rien ne passe) */
            g_input.down = 0;
            g_input.held = 0;
            g_input.touch_down = false;
            g_input.touch_held = false;
        }

        /* Resume session une fois init terminee */
        if (g_init_done && !resumed) {
            resumed = true;
            if (g_settings.resume_on_start &&
                g_settings.last_path[0] &&
                g_playlist.count > 0) {
                play_current_track();
                if (g_settings.last_position > 0)
                    audio_seek(g_settings.last_position);
                g_state = STATE_PLAYER;
            }
        }

        /* Bloquer inputs pendant chargement */
        /* Autoriser les touches pendant le chargement Navidrome pour pouvoir faire "Suivant" (Skip) */
        extern int nd_get_playing(void);
        extern bool nd_session_active(void);
        if ((!g_loading && !g_browser_loading) || nd_get_playing() || nd_session_active()) {
            switch (g_state) {
                case STATE_FILE_BROWSER: input_file_browser(); break;
                case STATE_PLAYER:       input_player();       break;
                case STATE_PLAYLIST:     input_playlist();     break;
                case STATE_SETTINGS:     input_settings();     break;
                case STATE_DIR_PICKER:   input_dir_picker();   break;
                case STATE_NIGHT_MODE:   input_night_mode();   break;
                case STATE_EQUALIZER:    input_equalizer();    break;
                case STATE_NAVIDROME:   navidrome_input(&g_state);   break;
            case STATE_RECORDER:    recorder_input((int*)&g_state); break;  /* ND1 */
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

        /* V0.96 : skip player_ui_update en mode eco (economie CPU) */
        if (!power_save_is_active())
            player_ui_update(&g_player_ui, audio_get_metadata(), dt);

        /* V0.97 : Update sleep timer (peut arreter l audio) */
        sleep_timer_update();

        /* Debounce : save si settings modifies depuis 3sec (fix freeze SD lente) */
        settings_update();

        /* Update LED notification */
        update_led();

        /* V0.97 : Auto-avance GLOBALE - marche dans toutes les pages
           FIX : ne pas declencher pendant chargement (etat transitoire finished)
           FIX : ni pendant une lecture Navidrome. Un titre ND n est pas dans
           g_playlist ; sans ce garde, la fin (ou meme les audio_stop()
           transitoires) d un titre ND faisait croire qu une piste locale venait
           de finir, et pl_next() lancait un morceau au hasard de la carte SD
           par-dessus le flux Navidrome. */
        if (audio_is_finished() && !g_loading && !nd_get_playing() && g_state != STATE_NIGHT_MODE && g_state != STATE_NAVIDROME && !nd_session_active()) {
            int n = pl_next(&g_playlist);
            if (n >= 0) play_current_track();
            else        audio_stop();
        }

        /* Sauvegarde intelligente uniquement sur changement d'état (Zero freeze SD en plein morceau) */
        if (++save_timer > 7200) {
            save_timer = 0;
            if (audio_get_state() == AUDIO_PLAYING) {
                g_settings.last_position = audio_get_position();
                g_settings.last_track    = g_playlist.current;
                /* Pas de settings_mark_dirty() automatique en plein milieu d'une chanson ! */
            }
        }

        /* V0.95 : demande preview UNIQUEMENT quand la selection change
           (evite de spammer le worker thread 60x/sec = lag audio) */
        static int last_prev_sel = -999;
        static int last_prev_count = -1;
        static AppState last_prev_state = STATE_PLAYER;
        if (g_state == STATE_FILE_BROWSER && !g_browser_loading
            && g_browser.count > 0
            && g_browser.selected >= 0
            && g_browser.selected < g_browser.count
            && (g_browser.selected != last_prev_sel
                || g_browser.count != last_prev_count
                || g_state != last_prev_state))
        {
            last_prev_sel = g_browser.selected;
            last_prev_count = g_browser.count;
            last_prev_state = g_state;
            FileEntry *fe = &g_browser.entries[g_browser.selected];
            if (fe->is_audio) {
                preview_request(fe->full_path);
            } else {
                for (int i = 0; i < g_browser.count; i++) {
                    if (g_browser.entries[i].is_audio) {
                        preview_request(g_browser.entries[i].full_path);
                        break;
                    }
                }
            }
        }

        /* V0.95 : upload GPU covers preview UNIQUEMENT dans file browser */
        if (g_state == STATE_FILE_BROWSER)
            preview_update_gpu();

        if (g_update_notif_timer > 0) g_update_notif_timer--;
        draw_frame(dt);

        /* V0.96 : en mode eco, on peut se permettre 1 FPS (economie CPU/GPU) */
        if (power_save_is_active()) {
            svcSleepThread(500000000LL); /* 500ms = 2 FPS max */
        }

        /* Limiteur 60fps */
        u64 frame_end = osGetTime();
        u64 frame_ms  = frame_end - now;
        if (frame_ms < 16)
            svcSleepThread((16 - frame_ms) * 1000000LL);
    }

    /* ── Nettoyage (UNE SEULE FOIS chaque service) ────────── */
    g_settings.last_position = audio_get_position();
    g_settings.last_track    = g_playlist.current;
    settings_save_sync();
    nd_temp_clear();   /* les morceaux Navidrome ecoutes ne survivent pas a la session */

    /* Eteindre LED */
    {
        InfoLedPattern off = {0};
        off.delay = 0x10;
        off.loopDelay = 0xFF;
        MCUHWC_SetInfoLedPattern(&off);
    }

    /* V0.97 : juste rallumer les backlight avant de quitter */
    gspLcdInit();
    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM);
    gspLcdExit();

    /* Retirer hook APT */
    aptUnhook(&s_apt_cookie);

    power_save_exit();

    /* V0.97 : Cleanup sleep timer */
    sleep_timer_exit();

    /* V0.96 : IMPERATIF reactiver le Wi-Fi avant de quitter ! */
    wifi_manager_exit();

    preview_exit();

    player_ui_exit(&g_player_ui);
    audio_exit();

    C2D_Fini();
    C3D_Fini();

    ptmuExit();
    mcuHwcExit();
    cfguExit();
    gfxExit();
    romfsExit();
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
   V0.98 : GETTERS ET FONCTIONS BROWSER / PICKER / REVEIL
   ═══════════════════════════════════════════════════════════════ */

const char* main_get_current_cwd(void) {
    return g_browser.cwd[0] ? g_browser.cwd : "sdmc:/Music";
}

const char* main_get_start_dir(void) {
    const char *d = g_settings.start_dir;
    if (strncmp(d, "custom:", 7) == 0) d += 7;
    return (d[0] && strcmp(d, "sdmc:/Music") != 0) ? d : "sdmc:/Music";
}

/* Helper dessin texte Citro2D */
static void picker_draw_text(float x, float y, float scale, u32 color, const char *text) {
    if (!text || !text[0]) return;
    static C2D_TextBuf buf = NULL;
    if (!buf) buf = C2D_TextBufNew(256);
    C2D_TextBufClear(buf);
    C2D_Text c2d_text;
    C2D_TextParse(&c2d_text, buf, text);
    C2D_TextOptimize(&c2d_text);
    C2D_DrawText(&c2d_text, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

/* Mini-explorateur intelligent (Dossier Custom & Musique Reveil) */
static FileBrowser g_picker_fb;
static bool        s_picker_is_file_mode = false;

void dir_picker_start(void)
{
    s_picker_is_file_mode = false; /* Mode Choix de Dossier */
    const char *start = main_get_start_dir();
    fb_init(&g_picker_fb, start);
    g_state = STATE_DIR_PICKER;
}

void file_picker_start(void)
{
    s_picker_is_file_mode = true; /* Mode Choix de Fichier Audio */
    /* REVEIL4C : redemarrer dans le DOSSIER de la musique deja choisie
       et non sur le chemin du fichier lui-meme (bug : on se retrouvait
       "dans" SD:/Music/reveil.mp3). Fallback : dossier de depart. */
    const char *start = main_get_start_dir();
    if (g_settings.alarm_path[0]) {
        static char s_pick_dir[512];
        strncpy(s_pick_dir, g_settings.alarm_path, 511);
        s_pick_dir[511] = 0;
        char *slash = strrchr(s_pick_dir, '/');
        if (slash && slash[1]) {
            *slash = 0;
            /* "sdmc:" seul -> "sdmc:/" */
            if (!strchr(s_pick_dir, '/') &&
                s_pick_dir[strlen(s_pick_dir) - 1] == ':' &&
                strlen(s_pick_dir) < 500)
                strcat(s_pick_dir, "/");
            if (s_pick_dir[0]) start = s_pick_dir;
        }
    }
    fb_init(&g_picker_fb, start);
    g_state = STATE_DIR_PICKER;
}

void input_dir_picker(void)
{
    u32 kd   = g_input.down;
    u32 held = g_input.held;

    /* V0.98 : Commandes Tactiles pour le mini-explorateur */
    if (g_input.touch_down) {
        int tx = g_input.touch.px;
        int ty = g_input.touch.py;

        /* Fleche HAUT a droite */
        if (tx >= 280 && ty >= 20 && ty < 60) {
            fb_select_prev(&g_picker_fb);
        }
        /* Fleche BAS a droite */
        else if (tx >= 280 && ty >= 180 && ty < 220) {
            fb_select_next(&g_picker_fb);
        }
        /* Liste principale */
        else if (ty >= 20 && ty < 220) {
            int row = (ty - 20) / 20;
            int idx = g_picker_fb.scroll_offset + row;
            if (idx >= 0 && idx < g_picker_fb.count) {
                if (idx == g_picker_fb.selected) {
                    kd |= KEY_A; /* Double tap = Simule la touche A */
                } else {
                    g_picker_fb.selected = idx;
                    if (g_picker_fb.selected < g_picker_fb.scroll_offset)
                        g_picker_fb.scroll_offset = g_picker_fb.selected;
                    if (g_picker_fb.selected >= g_picker_fb.scroll_offset + g_picker_fb.visible_rows)
                        g_picker_fb.scroll_offset = g_picker_fb.selected - g_picker_fb.visible_rows + 1;
                }
            }
        }
    }

    /* B = Annuler et revenir aux parametres */
    if (kd & KEY_B) {
        g_state = STATE_SETTINGS;
        return;
    }

    /* Y = Remonter au dossier parent (..) */
    if (kd & KEY_Y) {
        fb_go_up(&g_picker_fb);
        return;
    }

    /* UNIFIED SCROLL PATTERN : Picker (Auto-repeat Haut/Bas + Gauche/Droite +-5) */
    static u64 pk_ud_held = 0;
    static bool pk_ud_rep = false;
    if (held & (KEY_DOWN | KEY_UP)) {
        if (!pk_ud_rep) {
            if (pk_ud_held == 0) pk_ud_held = osGetTime();
            if (osGetTime() - pk_ud_held > 400) pk_ud_rep = true;
        }
        if (pk_ud_rep) {
            if (held & KEY_DOWN) fb_select_next(&g_picker_fb);
            else                 fb_select_prev(&g_picker_fb);
        }
    } else { pk_ud_held = 0; pk_ud_rep = false; }

    if (kd & KEY_DOWN) fb_select_next(&g_picker_fb);
    if (kd & KEY_UP)   fb_select_prev(&g_picker_fb);
    if (kd & KEY_RIGHT) for (int i = 0; i < 5; i++) fb_select_next(&g_picker_fb);
    if (kd & KEY_LEFT)  for (int i = 0; i < 5; i++) fb_select_prev(&g_picker_fb);

    if (s_picker_is_file_mode) {
        /* === MODE SELECTION FICHIER AUDIO (REVEIL) === */
        if (kd & KEY_A) {
            if (g_picker_fb.count > 0 && g_picker_fb.selected >= 0 && g_picker_fb.selected < g_picker_fb.count) {
                FileEntry *fe = &g_picker_fb.entries[g_picker_fb.selected];
                if (!strcmp(fe->name, "..")) {
                    fb_go_up(&g_picker_fb);
                } else if (fe->is_dir) {
                    fb_enter_dir(&g_picker_fb, fe->full_path);
                } else if (fe->is_audio) {
                    /* Fichier audio choisi -> Enregistrer pour le reveil */
                    strncpy(g_settings.alarm_path, fe->full_path, 511);
                    g_settings.alarm_path[511] = 0;
                    settings_mark_dirty();
                    g_state = STATE_SETTINGS;
                }
            }
        }
    } else {
        /* === MODE SELECTION DOSSIER (START DIR) === */
        if (kd & KEY_X) {
            /* X = Valider le dossier courant immediatement */
            snprintf(g_settings.start_dir, 512, "custom:%s", g_picker_fb.cwd);
            settings_mark_dirty();
            g_state = STATE_SETTINGS;
            return;
        }

        if (kd & KEY_A) {
            if (g_picker_fb.count > 0 && g_picker_fb.selected >= 0 && g_picker_fb.selected < g_picker_fb.count) {
                FileEntry *fe = &g_picker_fb.entries[g_picker_fb.selected];
                if (!strcmp(fe->name, "..")) {
                    fb_go_up(&g_picker_fb);
                } else if (fe->is_dir) {
                    fb_enter_dir(&g_picker_fb, fe->full_path);
                } else {
                    snprintf(g_settings.start_dir, 512, "custom:%s", g_picker_fb.cwd);
                    settings_mark_dirty();
                    g_state = STATE_SETTINGS;
                }
            } else {
                snprintf(g_settings.start_dir, 512, "custom:%s", g_picker_fb.cwd);
                settings_mark_dirty();
                g_state = STATE_SETTINGS;
            }
        }
    }
}

void dir_picker_draw_top(void)
{
    Theme *th = current_theme;
    u32 bg_col = th ? th->bg_primary : C2D_Color32(20,20,30,255);
    u32 bg_sec = th ? th->bg_secondary : C2D_Color32(40,40,60,255);
    u32 text_p = th ? th->text_primary : C2D_Color32(255,255,255,255);
    u32 text_s = th ? th->text_secondary : C2D_Color32(200,200,200,255);
    u32 accent = th ? th->accent : C2D_Color32(0,180,255,255);

    C2D_TargetClear(g_3d_eye == 0 ? g_top : (g_top_right ? g_top_right : g_top), bg_col);
    C2D_SceneBegin(g_3d_eye == 0 ? g_top : (g_top_right ? g_top_right : g_top));

    /* Barre superieure */
    C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, 24, bg_sec);
    
    if (s_picker_is_file_mode) {
        picker_draw_text(10, 3, 0.5f, text_p, "CHOISIR UNE MUSIQUE DE REVEIL");
        picker_draw_text(10, 38, 0.44f, accent, "Dossier actuel :");
        picker_draw_text(10, 58, 0.42f, text_p, g_picker_fb.cwd);

        picker_draw_text(10, 110, 0.38f, text_s, "A : Entrer dossier / Choisir musique");
        picker_draw_text(10, 130, 0.38f, text_s, "Y : Dossier parent (..)");
        picker_draw_text(10, 150, 0.38f, text_s, "B : Annuler");
    } else {
        picker_draw_text(10, 3, 0.5f, text_p, "CHOISIR DOSSIER DE DEMARRAGE");
        picker_draw_text(10, 38, 0.44f, accent, "Dossier actuel :");
        picker_draw_text(10, 58, 0.42f, text_p, g_picker_fb.cwd);

        picker_draw_text(10, 105, 0.38f, text_s, "A : Entrer dans le dossier");
        picker_draw_text(10, 125, 0.38f, text_s, "X : Valider ce dossier comme Custom");
        picker_draw_text(10, 145, 0.38f, text_s, "Y : Dossier parent (..)");
        picker_draw_text(10, 165, 0.38f, text_s, "B : Annuler");
    }
}

/* ═══════════════════════════════════════════════════════════════
   V0.98 : MODE NUIT (REVEIL ECO+) - 100% ECO BATTERIE
   ═══════════════════════════════════════════════════════════════ */
void night_mode_start(void)
{
    g_state = STATE_NIGHT_MODE;

    /* 1. Mettre en pause la musique pour conserver la piste et la position actuelle sans consommer de CPU */
    if (audio_get_state() == AUDIO_PLAYING) {
        audio_pause();
    }

    /* 2. Mettre le volume Master NDSP a 0 */
    ndspSetMasterVol(0.0f);

    /* 3. Bloquer la touche HOME pendant la nuit */
    aptSetHomeAllowed(false);

    /* 4. Couper la 3D et sa LED verte */
    gfxSet3D(false);

    /* 5. Eteindre le retroeclairage des DEUX ecrans (Haut + Bas) */
    gspLcdInit();
    GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM);
    gspLcdExit();

    /* 6. Couper le Wi-Fi pour economiser la batterie */
    wifi_manager_disable();

    /* 7. Desactiver les visualiseurs */
    audio_set_viz_active(false);

    /* 8. Baisser la frequence CPU sur New 3DS */
    if (g_is_new3ds) {
        osSetSpeedupEnable(false);
    }

    /* 9. Eteindre la LED de notification */
    {
        mcuHwcInit();
        InfoLedPattern off = {0};
        off.delay = 0x10;
        off.loopDelay = 0xFF;
        MCUHWC_SetInfoLedPattern(&off);
        mcuHwcExit();
    }
}

void night_mode_stop(void)
{
    /* 1. Restaurer le volume Master NDSP */
    ndspSetMasterVol(1.0f);

    /* REVEIL1 : reveil alarme/manuel -> volume retourne a 100% */
    audio_set_volume(1.0f);
    g_settings.volume = 1.0f;
    settings_mark_dirty();

    /* 2. Reactiver la 3D et sa LED */
    gfxSet3D(true);

    /* 3. Re-autoriser le bouton HOME */
    aptSetHomeAllowed(true);

    /* 4. Rallumer le retroeclairage des DEUX ecrans et restaurer la luminosite */
    gspLcdInit();
    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM);
    int lum = g_settings.eco_restore_brightness;
    if (lum < 1 || lum > 5) lum = 5;
    GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM, lum);
    gspLcdExit();

    /* 5. Reactiver le Wi-Fi si autorise */
    if (!g_settings.wifi_off_in_app) {
        wifi_manager_enable();
    }

    /* 6. Reactiver les visualiseurs */
    audio_set_viz_active(true);

    /* 7. Restaurer le CPU Speedup sur New 3DS */
    if (g_is_new3ds) {
        osSetSpeedupEnable(true);
    }

    if (g_state == STATE_NIGHT_MODE) {
        g_state = STATE_SETTINGS;
    }
}

void input_night_mode(void)
{
    u32 kd = g_input.down;

    /* SEULEMENT la touche B permet de quitter le Mode Nuit manuellement */
    if (kd & KEY_B) {
        night_mode_stop();
    }
}

void night_mode_draw(void)
{
    /* Ecran Noir total sur les deux ecrans */
    C2D_TargetClear(g_top, C2D_Color32(0, 0, 0, 255));
    if (g_top_right) C2D_TargetClear(g_top_right, C2D_Color32(0, 0, 0, 255));
    C2D_TargetClear(g_bot, C2D_Color32(0, 0, 0, 255));

    /* Sleep 500ms = Boucle a 2 FPS -> Sommeil profond CPU 99.8% ! */
    svcSleepThread(500000000ULL);
}

/* ND1 V0.99 : entree du mode Navidrome (appele depuis settings.c) */
void navidrome_start(void)
{
    extern void nd_enter(void);
    g_state = STATE_NAVIDROME;
    nd_enter();
}

/* Declencheur Alarme Reveil en arriere-plan */
static int s_last_alarm_min = -1;

static void check_alarm_trigger(void)
{
    /* REVEIL4A : l'alarme vient d'etre (re)activee ou l'heure vient
       d'etre modifiee ? Si l'heure choisie == heure actuelle, on marque
       la minute comme deja traitee : pas de sonnerie immediate, ca
       sonnera demain. (retour pote V0.98) */
    {
        static int s_pa_en = -1, s_pa_h = -1, s_pa_m = -1;
        if ((int)g_settings.alarm_enabled != s_pa_en ||
            g_settings.alarm_hour != s_pa_h ||
            g_settings.alarm_minute != s_pa_m) {
            s_pa_en = (int)g_settings.alarm_enabled;
            s_pa_h  = g_settings.alarm_hour;
            s_pa_m  = g_settings.alarm_minute;
            if (g_settings.alarm_enabled) {
                time_t t0 = time(NULL);
                struct tm *l0 = localtime(&t0);
                if (l0 && l0->tm_hour == g_settings.alarm_hour &&
                    l0->tm_min == g_settings.alarm_minute)
                    s_last_alarm_min = l0->tm_min;
            }
        }
    }

    if (!g_settings.alarm_enabled) return;

    time_t now_t = time(NULL);
    struct tm *lt = localtime(&now_t);
    if (!lt) return;

    int hour = lt->tm_hour;
    int minute = lt->tm_min;

    if (hour == g_settings.alarm_hour && minute == g_settings.alarm_minute) {
        if (s_last_alarm_min != minute) {
            s_last_alarm_min = minute;

            /* REVEIL1C : le reveil sonne -> volume 100% quel que
               soit l'etat d'ou il part (nuit, lecteur, navigate ur) */
            audio_set_volume(1.0f);
            g_settings.volume = 1.0f;
            settings_mark_dirty();

            /* Sortir du Mode Nuit si actif (rallume ecrans, 3D, wifi, home, volume...) */
            if (g_state == STATE_NIGHT_MODE) {
                night_mode_stop();
            }

            /* Determiner quelle piste jouer (Custom ou piste actuelle) */
            const char *target_path = NULL;
            if (g_settings.alarm_path[0] != '\0') {
                target_path = g_settings.alarm_path;
            } else if (g_playlist.count > 0 && g_playlist.current >= 0 && g_playlist.current < g_playlist.count) {
                target_path = g_playlist.items[g_playlist.current].path;
            }

            if (target_path && target_path[0] != '\0') {
                load_and_play(target_path); /* Charge la musique + LA POCHETTE + passe en STATE_PLAYER ! */
            } else if (audio_get_state() != AUDIO_PLAYING) {
                audio_play();
                g_state = STATE_PLAYER;
            }
        }
    } else {
        s_last_alarm_min = -1;
    }
}
