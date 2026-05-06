#include "theme.h"
#include <stdio.h>
#include <string.h>

// ─── Dark theme (default) ─────────────────────────────────────
Theme theme_dark = {
    .name          = "Dark",
    .bg_primary    = RGB8(18,  18,  24),
    .bg_secondary  = RGB8(30,  30,  40),
    .bg_header     = RGB8(12,  12,  18),
    .bg_selected   = RGB8(50,  50,  80),
    .bg_playing    = RGB8(30,  60,  90),
    .text_primary  = RGB8(230, 230, 240),
    .text_secondary= RGB8(160, 160, 180),
    .text_accent   = RGB8(100, 180, 255),
    .text_disabled = RGB8(80,  80,  100),
    .accent        = RGB8(80,  160, 255),
    .accent2       = RGB8(255, 100, 180),
    .border        = RGB8(50,  50,  70),
    .scrollbar     = RGB8(80,  80,  110),
    .progress_bg   = RGB8(40,  40,  60),
    .progress_fill = RGB8(80,  160, 255),
    .visualizer_bars = {
        RGB8(80,  120, 255),
        RGB8(90,  140, 255),
        RGB8(100, 170, 255),
        RGB8(120, 200, 255),
        RGB8(140, 220, 240),
        RGB8(160, 230, 200),
        RGB8(180, 230, 150),
        RGB8(200, 220, 100),
    },
    .eq_bar   = RGB8(80, 160, 255),
    .eq_handle= RGB8(255, 255, 255),
};

// ─── Light theme ──────────────────────────────────────────────
Theme theme_light = {
    .name          = "Light",
    .bg_primary    = RGB8(245, 245, 250),
    .bg_secondary  = RGB8(255, 255, 255),
    .bg_header     = RGB8(230, 230, 240),
    .bg_selected   = RGB8(200, 220, 255),
    .bg_playing    = RGB8(180, 210, 255),
    .text_primary  = RGB8(20,  20,  30),
    .text_secondary= RGB8(80,  80,  100),
    .text_accent   = RGB8(0,   100, 220),
    .text_disabled = RGB8(160, 160, 180),
    .accent        = RGB8(0,   120, 255),
    .accent2       = RGB8(220, 50,  150),
    .border        = RGB8(200, 200, 215),
    .scrollbar     = RGB8(160, 160, 190),
    .progress_bg   = RGB8(210, 210, 225),
    .progress_fill = RGB8(0,   120, 255),
    .visualizer_bars = {
        RGB8(30,  100, 220),
        RGB8(50,  130, 230),
        RGB8(70,  160, 240),
        RGB8(90,  190, 245),
        RGB8(120, 210, 230),
        RGB8(150, 220, 180),
        RGB8(180, 220, 120),
        RGB8(210, 210, 60),
    },
    .eq_bar   = RGB8(0,  120, 255),
    .eq_handle= RGB8(30, 30, 60),
};

// ─── Purple Neon theme ────────────────────────────────────────
Theme theme_purple = {
    .name          = "Neon Purple",
    .bg_primary    = RGB8(10,  5,   20),
    .bg_secondary  = RGB8(20,  10,  40),
    .bg_header     = RGB8(5,   2,   15),
    .bg_selected   = RGB8(60,  20,  100),
    .bg_playing    = RGB8(40,  10,  80),
    .text_primary  = RGB8(240, 220, 255),
    .text_secondary= RGB8(180, 140, 210),
    .text_accent   = RGB8(200, 100, 255),
    .text_disabled = RGB8(80,  60,  100),
    .accent        = RGB8(180, 60,  255),
    .accent2       = RGB8(255, 60,  180),
    .border        = RGB8(60,  30,  90),
    .scrollbar     = RGB8(100, 50,  150),
    .progress_bg   = RGB8(30,  15,  50),
    .progress_fill = RGB8(180, 60,  255),
    .visualizer_bars = {
        RGB8(180, 60,  255),
        RGB8(200, 50,  240),
        RGB8(220, 40,  210),
        RGB8(240, 50,  180),
        RGB8(255, 70,  150),
        RGB8(255, 100, 130),
        RGB8(255, 140, 120),
        RGB8(255, 180, 100),
    },
    .eq_bar   = RGB8(180, 60, 255),
    .eq_handle= RGB8(255, 200, 255),
};

// ─── Forest Green theme ───────────────────────────────────────
Theme theme_forest = {
    .name          = "Forest",
    .bg_primary    = RGB8(10,  20,  15),
    .bg_secondary  = RGB8(18,  35,  25),
    .bg_header     = RGB8(5,   12,  8),
    .bg_selected   = RGB8(30,  70,  45),
    .bg_playing    = RGB8(20,  55,  35),
    .text_primary  = RGB8(210, 240, 220),
    .text_secondary= RGB8(140, 190, 160),
    .text_accent   = RGB8(80,  220, 130),
    .text_disabled = RGB8(70,  100, 80),
    .accent        = RGB8(60,  200, 110),
    .accent2       = RGB8(180, 240, 80),
    .border        = RGB8(30,  60,  40),
    .scrollbar     = RGB8(50,  100, 65),
    .progress_bg   = RGB8(20,  40,  28),
    .progress_fill = RGB8(60,  200, 110),
    .visualizer_bars = {
        RGB8(40,  180, 90),
        RGB8(60,  200, 100),
        RGB8(90,  220, 110),
        RGB8(130, 230, 110),
        RGB8(170, 235, 100),
        RGB8(200, 230, 80),
        RGB8(220, 215, 60),
        RGB8(235, 195, 50),
    },
    .eq_bar   = RGB8(60,  200, 110),
    .eq_handle= RGB8(200, 255, 220),
};

// ─── Registry ─────────────────────────────────────────────────
static Theme *themes[]  = { &theme_dark, &theme_light, &theme_purple, &theme_forest };
static int    theme_cnt = 4;
Theme *current_theme    = &theme_dark;

void theme_init(void)   { current_theme = &theme_dark; }
void theme_set(Theme *t){ current_theme = t; }
int  theme_count(void)  { return theme_cnt; }
Theme *theme_get(int i) { if(i<0||i>=theme_cnt) return &theme_dark; return themes[i]; }
const char *theme_name(int i) { return theme_get(i)->name; }

// ─── INI file loader/saver (basic) ───────────────────────────
bool theme_load_from_file(const char *path, Theme *out)
{
    FILE *f = fopen(path, "r");
    if(!f) return false;
    // Simple key=value parser
    char line[128]; char key[64]; unsigned int val;
    while(fgets(line, sizeof(line), f)) {
        if(sscanf(line, "name=%63s", out->name)==1) continue;
        if(sscanf(line, "bg_primary=0x%x", &val)==1){ out->bg_primary=val; continue; }
        if(sscanf(line, "bg_secondary=0x%x",&val)==1){ out->bg_secondary=val; continue; }
        if(sscanf(line, "accent=0x%x",&val)==1){ out->accent=val; continue; }
        if(sscanf(line, "accent2=0x%x",&val)==1){ out->accent2=val; continue; }
        if(sscanf(line, "text_primary=0x%x",&val)==1){ out->text_primary=val; continue; }
        if(sscanf(line, "text_secondary=0x%x",&val)==1){ out->text_secondary=val; continue; }
        if(sscanf(line, "text_accent=0x%x",&val)==1){ out->text_accent=val; continue; }
        if(sscanf(line, "progress_fill=0x%x",&val)==1){ out->progress_fill=val; continue; }
    }
    fclose(f);
    return true;
}

void theme_save_to_file(const char *path, const Theme *t)
{
    FILE *f = fopen(path, "w");
    if(!f) return;
    fprintf(f, "name=%s\n",            t->name);
    fprintf(f, "bg_primary=0x%08X\n",  t->bg_primary);
    fprintf(f, "bg_secondary=0x%08X\n",t->bg_secondary);
    fprintf(f, "accent=0x%08X\n",      t->accent);
    fprintf(f, "accent2=0x%08X\n",     t->accent2);
    fprintf(f, "text_primary=0x%08X\n",t->text_primary);
    fprintf(f, "text_secondary=0x%08X\n",t->text_secondary);
    fprintf(f, "text_accent=0x%08X\n", t->text_accent);
    fprintf(f, "progress_fill=0x%08X\n",t->progress_fill);
    fclose(f);
}
