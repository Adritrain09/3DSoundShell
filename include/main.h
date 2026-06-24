#pragma once

// ─── Update check ─────────────────────────────────────────
#define VERSION_URL "http://copyparty.hosten.uk/Publique-Adri/version.txt?raw"
extern char g_latest_version[16];
extern bool g_update_available;
extern int  g_update_notif_timer;
void update_check_start(void);

// ─── Version ──────────────────────────────────────────────
#define APP_VERSION "0.91"

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
    STATE_EQUALIZER
} AppState;

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
#define SUPPORTED_EXTS "mp3|ogg|wav|flac|aac|m4a|mp2|opus|wma|aiff"

// ─── TextBuf global partagé ───────────────────────────────────
extern C2D_TextBuf g_textbuf;
void g_textbuf_init(void);
