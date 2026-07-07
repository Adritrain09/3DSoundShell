// theme.c — Système multi-thèmes
// Thèmes intégrés : Dark, Light, Neon Purple, Forest
// Thèmes custom   : sdmc:/3DSoundShell/themes/*.ini (max 16)

#include "theme.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <citro2d.h>

/* ── TextBuf global ─────────────────────────────────────────── */
C2D_TextBuf g_textbuf = NULL;

void g_textbuf_init(void)
{
    if (!g_textbuf)
        g_textbuf = C2D_TextBufNew(4096);
}

void g_textbuf_exit(void)
{
    if (g_textbuf) {
        C2D_TextBufDelete(g_textbuf);
        g_textbuf = NULL;
    }
}

/* ── Convertir 0xRRGGBB -> RGBA8 ───────────────────────────── */
static u32 parse_color(unsigned int raw, int digits)
{
    u8 r, g, b;
    if (digits == 8) {
        r = (raw >> 24) & 0xFF;
        g = (raw >> 16) & 0xFF;
        b = (raw >>  8) & 0xFF;
    } else {
        r = (raw >> 16) & 0xFF;
        g = (raw >>  8) & 0xFF;
        b = (raw      ) & 0xFF;
    }
    return RGB8(r, g, b);
}

/* ══════════════════════════════════════════════════════════════
   THÈMES INTÉGRÉS
   ══════════════════════════════════════════════════════════════ */

Theme theme_dark = {
    .name           = "Dark",
    .bg_primary     = RGB8(18,  18,  24),
    .bg_secondary   = RGB8(30,  30,  40),
    .bg_header      = RGB8(12,  12,  18),
    .bg_selected    = RGB8(50,  50,  80),
    .bg_playing     = RGB8(30,  60,  90),
    .text_primary   = RGB8(230, 230, 240),
    .text_secondary = RGB8(160, 160, 180),
    .text_accent    = RGB8(100, 180, 255),
    .text_disabled  = RGB8(80,  80,  100),
    .accent         = RGB8(80,  160, 255),
    .accent2        = RGB8(255, 100, 180),
    .border         = RGB8(50,  50,  70),
    .scrollbar      = RGB8(80,  80,  110),
    .progress_bg    = RGB8(40,  40,  60),
    .progress_fill  = RGB8(80,  160, 255),
    .visualizer_bars = {
        RGB8(80,  120, 255), RGB8(90,  140, 255),
        RGB8(100, 170, 255), RGB8(120, 200, 255),
        RGB8(140, 220, 240), RGB8(160, 230, 200),
        RGB8(180, 230, 150), RGB8(200, 220, 100),
    },
    .eq_bar          = RGB8(80,  160, 255),
    .eq_handle       = RGB8(255, 255, 255),
    .eq_bar_selected = RGB8(255, 220, 80),
    .eq_bar_positive = RGB8(80,  200, 120),
    .eq_bar_negative = RGB8(255, 80,  80),
    .eq_zero_line    = RGB8(100, 100, 130),
    .eq_bg           = RGB8(10,  10,  18),
    .led_color       = RGB8(128, 0,   255),  /* Violet */
};

Theme theme_light = {
    .name           = "Light",
    .bg_primary     = RGB8(245, 245, 250),
    .bg_secondary   = RGB8(255, 255, 255),
    .bg_header      = RGB8(230, 230, 240),
    .bg_selected    = RGB8(200, 220, 255),
    .bg_playing     = RGB8(180, 210, 255),
    .text_primary   = RGB8(20,  20,  30),
    .text_secondary = RGB8(80,  80,  100),
    .text_accent    = RGB8(0,   100, 220),
    .text_disabled  = RGB8(160, 160, 180),
    .accent         = RGB8(0,   120, 255),
    .accent2        = RGB8(220, 50,  150),
    .border         = RGB8(200, 200, 215),
    .scrollbar      = RGB8(160, 160, 190),
    .progress_bg    = RGB8(210, 210, 225),
    .progress_fill  = RGB8(0,   120, 255),
    .visualizer_bars = {
        RGB8(30,  100, 220), RGB8(50,  130, 230),
        RGB8(70,  160, 240), RGB8(90,  190, 245),
        RGB8(120, 210, 230), RGB8(150, 220, 180),
        RGB8(180, 220, 120), RGB8(210, 210, 60),
    },
    .eq_bar          = RGB8(0,   120, 255),
    .eq_handle       = RGB8(30,  30,  60),
    .eq_bar_selected = RGB8(255, 160, 0),
    .eq_bar_positive = RGB8(0,   160, 80),
    .eq_bar_negative = RGB8(200, 40,  40),
    .eq_zero_line    = RGB8(140, 140, 160),
    .eq_bg           = RGB8(210, 210, 225),
    .led_color       = RGB8(0,   200, 255),  /* Cyan */
};

Theme theme_purple = {
    .name           = "Neon Purple",
    .bg_primary     = RGB8(10,  5,   20),
    .bg_secondary   = RGB8(20,  10,  40),
    .bg_header      = RGB8(5,   2,   15),
    .bg_selected    = RGB8(80,  20,  120),
    .bg_playing     = RGB8(60,  10,  100),
    .text_primary   = RGB8(240, 220, 255),
    .text_secondary = RGB8(180, 150, 210),
    .text_accent    = RGB8(220, 100, 255),
    .text_disabled  = RGB8(100, 70,  130),
    .accent         = RGB8(200, 80,  255),
    .accent2        = RGB8(80,  200, 255),
    .border         = RGB8(60,  20,  90),
    .scrollbar      = RGB8(120, 50,  180),
    .progress_bg    = RGB8(30,  10,  50),
    .progress_fill  = RGB8(200, 80,  255),
    .visualizer_bars = {
        RGB8(150, 0,   255), RGB8(170, 20,  255),
        RGB8(190, 50,  255), RGB8(210, 80,  255),
        RGB8(220, 100, 255), RGB8(230, 130, 255),
        RGB8(240, 160, 255), RGB8(255, 200, 255),
    },
    .eq_bar          = RGB8(200, 80,  255),
    .eq_handle       = RGB8(255, 255, 255),
    .eq_bar_selected = RGB8(255, 220, 80),
    .eq_bar_positive = RGB8(80,  255, 180),
    .eq_bar_negative = RGB8(255, 60,  120),
    .eq_zero_line    = RGB8(80,  30,  110),
    .eq_bg           = RGB8(5,   2,   15),
    .led_color       = RGB8(255, 0,   255),  /* Magenta */
};

Theme theme_forest = {
    .name           = "Forest",
    .bg_primary     = RGB8(10,  20,  12),
    .bg_secondary   = RGB8(18,  35,  20),
    .bg_header      = RGB8(5,   12,  7),
    .bg_selected    = RGB8(30,  80,  40),
    .bg_playing     = RGB8(20,  60,  30),
    .text_primary   = RGB8(200, 230, 200),
    .text_secondary = RGB8(140, 180, 140),
    .text_accent    = RGB8(100, 220, 120),
    .text_disabled  = RGB8(70,  100, 70),
    .accent         = RGB8(80,  200, 100),
    .accent2        = RGB8(200, 180, 80),
    .border         = RGB8(30,  60,  35),
    .scrollbar      = RGB8(60,  120, 70),
    .progress_bg    = RGB8(20,  40,  22),
    .progress_fill  = RGB8(80,  200, 100),
    .visualizer_bars = {
        RGB8(40,  160, 60),  RGB8(60,  180, 70),
        RGB8(80,  200, 80),  RGB8(100, 210, 90),
        RGB8(130, 220, 100), RGB8(160, 225, 110),
        RGB8(190, 230, 120), RGB8(220, 235, 130),
    },
    .eq_bar          = RGB8(80,  200, 100),
    .eq_handle       = RGB8(220, 235, 200),
    .eq_bar_selected = RGB8(220, 200, 60),
    .eq_bar_positive = RGB8(60,  220, 100),
    .eq_bar_negative = RGB8(220, 80,  60),
    .eq_zero_line    = RGB8(50,  90,  55),
    .eq_bg           = RGB8(5,   12,  7),
    .led_color       = RGB8(80,  255, 100),  /* Vert lime */
};

/* ══════════════════════════════════════════════════════════════
   REGISTRE DES THÈMES
   4 intégrés + jusqu'à MAX_CUSTOM_THEMES custom
   ══════════════════════════════════════════════════════════════ */

#define BUILTIN_COUNT 4

static Theme   s_custom_themes[MAX_CUSTOM_THEMES];
static Theme  *s_all_themes[BUILTIN_COUNT + MAX_CUSTOM_THEMES];
static int     s_theme_count  = BUILTIN_COUNT;
Theme         *current_theme  = &theme_dark;

/* ── Scan le dossier themes/ et charge tous les .ini ────────── */
void theme_scan_custom(void)
{
    /* Reset thèmes custom */
    s_theme_count = BUILTIN_COUNT;

    /* Créer le dossier si inexistant */
    mkdir(THEMES_DIR, 0777);

    DIR *d = opendir(THEMES_DIR);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) && s_theme_count < BUILTIN_COUNT + MAX_CUSTOM_THEMES) {
        /* Filtrer uniquement les .ini */
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcasecmp(dot, ".ini") != 0) continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", THEMES_DIR, ent->d_name);

        int idx = s_theme_count - BUILTIN_COUNT;
        Theme *t = &s_custom_themes[idx];

        /* Valeurs par défaut = Dark */
        t->led_color = RGB8(128, 0, 255);  /* Violet par defaut */
        *t = theme_dark;

        /* Nom par défaut = nom du fichier sans .ini */
        snprintf(t->name, 64, "%s", ent->d_name);
        char *ext = strrchr(t->name, '.');
        if (ext) *ext = '\0';

        if (theme_load_from_file(path, t)) {
            s_all_themes[s_theme_count] = t;
            s_theme_count++;
        }
    }
    closedir(d);
}

/* ── Init ───────────────────────────────────────────────────── */
void theme_init(void)
{
    /* Enregistrer les thèmes intégrés */
    s_all_themes[0] = &theme_dark;
    s_all_themes[1] = &theme_light;
    s_all_themes[2] = &theme_purple;
    s_all_themes[3] = &theme_forest;
    s_theme_count   = BUILTIN_COUNT;

    /* Scanner les thèmes custom */
    theme_scan_custom();

    current_theme = &theme_dark;
}

void        theme_set(Theme *t)      { current_theme = t; }
int         theme_count(void)        { return s_theme_count; }
Theme      *theme_get(int i)         { if (i < 0 || i >= s_theme_count) return &theme_dark; return s_all_themes[i]; }
const char *theme_name(int i)        { return theme_get(i)->name; }

/* ══════════════════════════════════════════════════════════════
   LOADER / SAVER INI
   ══════════════════════════════════════════════════════════════ */

bool theme_load_from_file(const char *path, Theme *out)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    bool vis_start_set = false;
    bool vis_end_set   = false;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        line[strcspn(line, "\r\n")] = 0;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        while (*key == ' ') key++;
        while (*val == ' ') val++;

        /* ── Nom ── */
        if (!strcmp(key, "name")) {
            snprintf(out->name, 64, "%s", val);
            continue;
        }

        /* ── Couleur ── */
        unsigned int raw = 0;
        u32 ival = 0;
        bool color_ok = false;

        if (val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
            char *endp = val + 2;
            int digits = 0;
            while ((endp[digits] >= '0' && endp[digits] <= '9') ||
                   (endp[digits] >= 'a' && endp[digits] <= 'f') ||
                   (endp[digits] >= 'A' && endp[digits] <= 'F'))
                digits++;

            if (sscanf(val, "0x%x", &raw) == 1) {
                ival = parse_color(raw, digits);
                color_ok = true;
            }
        }

        if (!color_ok) continue;

        if (!strcmp(key,"bg_primary"))        out->bg_primary        = ival;
        if (!strcmp(key,"bg_secondary"))      out->bg_secondary      = ival;
        if (!strcmp(key,"bg_header"))         out->bg_header         = ival;
        if (!strcmp(key,"bg_selected"))       out->bg_selected       = ival;
        if (!strcmp(key,"bg_playing"))        out->bg_playing        = ival;
        if (!strcmp(key,"text_primary"))      out->text_primary      = ival;
        if (!strcmp(key,"text_secondary"))    out->text_secondary    = ival;
        if (!strcmp(key,"text_accent"))       out->text_accent       = ival;
        if (!strcmp(key,"text_disabled"))     out->text_disabled     = ival;
        if (!strcmp(key,"accent"))            out->accent            = ival;
        if (!strcmp(key,"accent2"))           out->accent2           = ival;
        if (!strcmp(key,"border"))            out->border            = ival;
        if (!strcmp(key,"scrollbar"))         out->scrollbar         = ival;
        if (!strcmp(key,"progress_bg"))       out->progress_bg       = ival;
        if (!strcmp(key,"progress_fill"))     out->progress_fill     = ival;
        if (!strcmp(key,"eq_bar"))            out->eq_bar            = ival;
        if (!strcmp(key,"eq_handle"))         out->eq_handle         = ival;
        if (!strcmp(key,"eq_bar_selected"))   out->eq_bar_selected   = ival;
        if (!strcmp(key,"eq_bar_positive"))   out->eq_bar_positive   = ival;
        if (!strcmp(key,"eq_bar_negative"))   out->eq_bar_negative   = ival;
        if (!strcmp(key,"eq_zero_line"))      out->eq_zero_line      = ival;
        if (!strcmp(key,"eq_bg"))             out->eq_bg             = ival;
        if (!strcmp(key,"led_color"))         out->led_color         = ival;

        if (!strcmp(key,"vis_start")) { out->visualizer_bars[0] = ival; vis_start_set = true; }
        if (!strcmp(key,"vis_end"))   { out->visualizer_bars[7] = ival; vis_end_set   = true; }

        for (int vi = 0; vi < 8; vi++) {
            char vk[8]; snprintf(vk, 8, "vis%d", vi);
            if (!strcmp(key, vk)) { out->visualizer_bars[vi] = ival; break; }
        }
    }

    /* Dégradé auto vis_start → vis_end */
    if (vis_start_set && vis_end_set) {
        u32 c0 = out->visualizer_bars[0];
        u32 c7 = out->visualizer_bars[7];
        int r0=(c0>>0)&0xff, g0=(c0>>8)&0xff, b0=(c0>>16)&0xff;
        int r7=(c7>>0)&0xff, g7=(c7>>8)&0xff, b7=(c7>>16)&0xff;
        for (int vi = 1; vi < 7; vi++) {
            float t = (float)vi / 7.f;
            out->visualizer_bars[vi] = RGBA8(
                (u8)(r0 + (r7-r0)*t),
                (u8)(g0 + (g7-g0)*t),
                (u8)(b0 + (b7-b0)*t),
                255);
        }
    }

    fclose(f);
    return true;
}

void theme_save_to_file(const char *path, const Theme *t)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "; Theme: %s\n", t->name);
    fprintf(f, "; Copiez ce fichier dans sdmc:/3DSoundShell/themes/\n\n");
    fprintf(f, "name=%s\n",               t->name);
    fprintf(f, "bg_primary=0x%06lX\n",    (unsigned long)((t->bg_primary   >>0)&0xff | (t->bg_primary   >>8&0xff)<<8  | (t->bg_primary  >>16&0xff)<<16));
    fprintf(f, "bg_secondary=0x%06lX\n",  (unsigned long)((t->bg_secondary >>0)&0xff | (t->bg_secondary >>8&0xff)<<8  | (t->bg_secondary>>16&0xff)<<16));
    fprintf(f, "accent=0x%06lX\n",        (unsigned long)((t->accent       >>0)&0xff | (t->accent       >>8&0xff)<<8  | (t->accent      >>16&0xff)<<16));
    fprintf(f, "accent2=0x%06lX\n",       (unsigned long)((t->accent2      >>0)&0xff | (t->accent2      >>8&0xff)<<8  | (t->accent2     >>16&0xff)<<16));
    fprintf(f, "text_primary=0x%06lX\n",  (unsigned long)((t->text_primary >>0)&0xff | (t->text_primary >>8&0xff)<<8  | (t->text_primary>>16&0xff)<<16));
    fprintf(f, "progress_fill=0x%06lX\n", (unsigned long)((t->progress_fill>>0)&0xff | (t->progress_fill>>8&0xff)<<8  | (t->progress_fill>>16&0xff)<<16));
    fprintf(f, "vis_start=0x%06lX\n",     (unsigned long)((t->visualizer_bars[0]>>0)&0xff | (t->visualizer_bars[0]>>8&0xff)<<8 | (t->visualizer_bars[0]>>16&0xff)<<16));
    fprintf(f, "vis_end=0x%06lX\n",       (unsigned long)((t->visualizer_bars[7]>>0)&0xff | (t->visualizer_bars[7]>>8&0xff)<<8 | (t->visualizer_bars[7]>>16&0xff)<<16));
    fclose(f);
}
