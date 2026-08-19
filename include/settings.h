#include <3ds.h>
// settings.h
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

    // Vitesse lecture
    float   playback_speed; // 0.5 a 2.0

    // LED notification (utilise couleur du theme)
    bool    led_enabled;

    // Duree du splash au demarrage (index 0-7)
    // 0=OFF, 1=0.2s, 2=0.5s, 3=1s, 4=1.5s, 5=2s, 6=2.5s, 7=3s
    int     splash_duration;

    // Langue interface : 0=FR, 1=EN
    // V0.96 : Mode economie energie
    bool    power_save_enabled;   // ON/OFF
    int     power_save_delay_sec; // delai en secondes (5-60)
    u32     power_save_text_color;// couleur RGB du texte (calcul depuis R/G/B ou theme)
    u8      power_save_r;         // Rouge 0-255 (mode Custom)
    u8      power_save_g;         // Vert 0-255
    u8      power_save_b;         // Bleu 0-255
    int     power_save_color_mode;// 0=Custom RGB, 1=Depuis theme (accent), 2=Depuis theme (text)
    bool    wifi_off_in_app;      // V0.96 : couper Wi-Fi quand app active (eco batterie)

    // V0.97 : Sleep timer (arret auto apres X minutes)
    int     sleep_timer_min;      // 0=OFF, 5, 10, 15, 30, 45, 60, 90, 120 minutes
    bool    sleep_shutdown;

    // V0.97 : Eteindre le retroeclairage en mode eco (ON par defaut)
    // V0.97 : Mode retroeclairage eco : 0=OFF, 1=REDUIRE, 2=ETEINDRE
    int     eco_backlight_mode;
    // V0.97 : Luminosite a restaurer apres eco (mode REDUIRE), 1-5
    int     eco_restore_brightness;

    // V0.97 : Style du menu settings (0=Direct/liste, 1=Onglets)
    int     settings_use_tabs;

    // V0.97 : Pitch/Speed separes (WSOLA)
    float   pro_speed;            // 0.5 a 2.0 (1.0 = neutre) - vitesse sans changer pitch
    int     pitch_semitones;      // -12 a +12 demi-tons (0 = neutre) - pitch sans changer vitesse

    // V0.96 : Activer/desactiver le visualiseur (economie CPU)
    bool    viz_enabled;

    int     language;
} Settings;

extern Settings g_settings;

void settings_init(void);

/* Marque les settings comme modifies, save 3sec apres le dernier changement
   (evite les freeze SD sur cartes lentes) */
void settings_mark_dirty(void);

/* Appelle dans la boucle main pour effectuer le save si necessaire */
void settings_update(void);
void settings_load(void);
void settings_save(void);
void settings_save_sync(void); /* save garanti avant quitter */
void settings_draw_top(void);
void settings_draw_bottom(int *selected_item);
void settings_handle_input(int *selected_item, u32 keys_down);

/* V0.96 : obtient la vraie couleur du texte eco selon le mode
   - mode 0 (Custom) : couleur R/G/B choisie
   - mode 1 (Theme accent) : couleur accent du theme actuel
   - mode 2 (Theme text) : couleur text_primary du theme actuel */
u32 settings_get_power_save_color(void);
