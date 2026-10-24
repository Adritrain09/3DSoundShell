#ifndef SETTINGS_H
#define SETTINGS_H

#include <3ds.h>
#include <stdbool.h>
#include "theme.h"
#include "player_ui.h"

typedef struct {
    float   volume;
    bool    resume_on_start;
    char    last_path[512];

    bool    alarm_enabled;
    int     alarm_hour;
    int     alarm_minute;
    char    alarm_path[512];
    int     last_track;
    int     last_position;

    int     theme_index;
    VisualizerStyle viz_style;
    bool    show_cover;
    bool    show_spectrum;
    int     ui_language;
    bool    easter_eggs_enabled;
    bool    seasonal_events_enabled;
    bool    lid_playback_enabled;

    float   eq_gains[10];
    float   eq_gains_per_preset[7][8];
    int     eq_preset_index;

    bool    shuffle;
    int     repeat;

    char    start_dir[512];
    float   playback_speed;
    bool    led_enabled;
    int     splash_duration;

    int     language;
    bool    power_save_enabled;
    int     power_save_delay_sec;
    u32     power_save_text_color;
    u8      power_save_r;
    u8      power_save_g;
    u8      power_save_b;
    int     power_save_color_mode;
    bool    wifi_off_in_app;

    int     sleep_timer_min;
    int     sleep_timer_minutes;
    bool    sleep_shutdown;
    int     sleep_timer_action;

    int     eco_backlight_mode;
    int     eco_dim_backlight;
    int     eco_restore_brightness;

    int     settings_use_tabs;
    float   pro_speed;
    int     pitch_semitones;
    bool    viz_enabled;

    int     white_noise_keepalive;
    int     distortion;
    int     prev_seek_threshold;
    float   pause_fade_sec;
    int     nd_endless_radio;

    char    navidrome_url[256];
    char    navidrome_user[64];
    char    navidrome_pass[128];
} Settings;

extern Settings g_settings;

void settings_init(void);
void settings_load(void);
void settings_save(void);
void settings_save_sync(void);
void settings_mark_dirty(void);
void settings_update(void);
void settings_draw_top(void);
void settings_draw_bottom(int *selected_item);
void settings_handle_input(int *selected_item, u32 keys_down);
u32 settings_get_power_save_color(void);

#endif
