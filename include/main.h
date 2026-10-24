// main.h
#pragma once

// ─── Update check ─────────────────────────────────────────
#define VERSION_URL "http://copyparty.hosten.uk/Publique-Adri/version.txt?raw"
extern char g_latest_version[16];
extern bool g_update_available;
extern bool g_server_unreachable;   /* V0.96 : serveur MAJ inaccessible */
extern bool g_version_ahead;        /* V0.96 : version installee > enregistree */
extern int  g_update_notif_timer;
extern bool g_is_new3ds;
void update_check_start(void);
const char* main_get_current_cwd(void);
const char* main_get_start_dir(void);
const char* main_get_current_cwd(void);
void draw_update_notif_timed(void); /* affiche notif MAJ si timer actif */

// ─── Version ──────────────────────────────────────────────
#define APP_VERSION "0.99"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

// ─── App states ───────────────────────────────────────────────
typedef enum {
    STATE_FILE_BROWSER,
    STATE_PLAYER,
    STATE_PLAYLIST,
    STATE_SETTINGS,
    STATE_DIR_PICKER,
    STATE_NIGHT_MODE,
    STATE_EQUALIZER,
    STATE_NAVIDROME,
    STATE_RECORDER   /* ND1 V0.99 */
} AppState;

/* ND1 V0.99 : mode Navidrome (source/navidrome.c) */
void nd_enter(void);
void navidrome_draw_top(void);
void navidrome_draw_bottom(void);
void navidrome_input(AppState *state);

// ─── Screen dimensions ────────────────────────────────────────
#define TOP_WIDTH    400
#define TOP_HEIGHT   240
#define BOT_WIDTH    320
#define BOT_HEIGHT   240

// ─── Max sizes ────────────────────────────────────────────────
#define MAX_FILES       512
#define MAX_PATH        512
#define MAX_PLAYLIST    256
#define MAX_FILENAME    256

// ─── Supported extensions ─────────────────────────────────────
#define SUPPORTED_EXTS "mp3|ogg|wav|flac|aac|m4a|mp2|opus|wma|aiff|mp4"

// ─── TextBuf global partagé ───────────────────────────────────
extern C2D_TextBuf g_textbuf;
void g_textbuf_init(void);

void night_mode_start(void);
void night_mode_draw(void);
void input_night_mode(void);


void dir_picker_start(void);
void file_picker_start(void);
void input_dir_picker(void);
void dir_picker_draw_top(void);
void night_mode_start(void);
void night_mode_stop(void);
void input_night_mode(void);
void night_mode_draw(void);
const char* main_get_current_cwd(void);
const char* main_get_start_dir(void);
