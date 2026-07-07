#pragma once
#include <3ds.h>
#include <citro2d.h>

// ─── Color helpers ────────────────────────────────────────────
#define RGBA8(r,g,b,a) ((u32)((a)<<24|(b)<<16|(g)<<8|(r)))
#define RGB8(r,g,b)    RGBA8(r,g,b,255)

// ─── Theme structure ──────────────────────────────────────────
typedef struct {
    char name[64];

    // Backgrounds
    u32 bg_primary;
    u32 bg_secondary;
    u32 bg_header;
    u32 bg_selected;
    u32 bg_playing;

    // Text
    u32 text_primary;
    u32 text_secondary;
    u32 text_accent;
    u32 text_disabled;

    // UI elements
    u32 accent;
    u32 accent2;
    u32 border;
    u32 scrollbar;

    // Player
    u32 progress_bg;
    u32 progress_fill;
    u32 visualizer_bars[8];

    // Equalizer
    u32 eq_bar;
    u32 eq_handle;
    u32 eq_bar_selected;
    u32 eq_bar_positive;
    u32 eq_bar_negative;
    u32 eq_zero_line;
    u32 eq_bg;

    // LED clamshell (couleur de la LED notification quand console fermee)
    u32 led_color;
} Theme;

// ─── Max thèmes custom depuis SD ──────────────────────────────
#define MAX_CUSTOM_THEMES 16
#define THEMES_DIR "sdmc:/3DSoundShell/themes"

// ─── Built-in themes ──────────────────────────────────────────
extern Theme theme_dark;
extern Theme theme_light;
extern Theme theme_purple;
extern Theme theme_forest;

// ─── Active theme ─────────────────────────────────────────────
extern Theme *current_theme;

// ─── TextBuf global ───────────────────────────────────────────
extern C2D_TextBuf g_textbuf;
void g_textbuf_init(void);
void g_textbuf_exit(void);

// ─── API ──────────────────────────────────────────────────────
void        theme_init(void);
void        theme_set(Theme *t);
bool        theme_load_from_file(const char *path, Theme *out);
void        theme_save_to_file(const char *path, const Theme *t);
int         theme_count(void);
Theme      *theme_get(int index);
const char *theme_name(int index);
void        theme_scan_custom(void);
