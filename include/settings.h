#pragma once
#include <stdbool.h>
#include "theme.h"
#include "player_ui.h"

typedef struct {
    // Audio
    float   volume;            // 0.0 - 1.0
    bool    resume_on_start;
    char    last_path[512];
    int     last_track;
    int     last_position;

    // UI
    int     theme_index;
    VisualizerStyle viz_style;
    bool    show_cover;
    bool    show_spectrum;
    int     ui_language;       // 0=FR 1=EN

    // Equalizer
    float   eq_gains[8];
    int     eq_preset_index;

    // Playlist
    bool    shuffle;
    int     repeat;            // RepeatMode

    // Start directory
    char    start_dir[512];
} Settings;

extern Settings g_settings;

void settings_init(void);
void settings_load(void);
void settings_save(void);
void settings_draw_top(void);
void settings_draw_bottom(int *selected_item);
void settings_handle_input(int *selected_item, u32 keys_down);
