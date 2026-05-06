#pragma once
#include <3ds.h>
#include "audio.h"
#include "playlist.h"
#include "theme.h"

// ─── Visualizer styles ────────────────────────────────────────
typedef enum {
    VIZ_BARS,       // Classic bar spectrum
    VIZ_WAVE,       // Waveform
    VIZ_CIRCLE,     // Circular / radial
    VIZ_COUNT
} VisualizerStyle;

// ─── Player UI state ──────────────────────────────────────────
typedef struct {
    VisualizerStyle viz_style;
    float           viz_data[EQ_BANDS];
    float           viz_smooth[EQ_BANDS]; // smoothed values
    int             cover_tex_id;         // -1 if none
    bool            cover_loaded;
    float           scroll_title_x;      // scrolling title animation
    u64             last_tick;
} PlayerUI;

void player_ui_init(PlayerUI *ui);
void player_ui_update(PlayerUI *ui, const AudioMetadata *meta, float dt);
void player_ui_draw_top(const PlayerUI *ui, const AudioMetadata *meta,
                        const Playlist *pl);
void player_ui_draw_bottom(PlayerUI *ui, const AudioMetadata *meta,
                           const Playlist *pl);
void player_ui_cycle_viz(PlayerUI *ui);
void player_ui_load_cover(PlayerUI *ui, const AudioMetadata *meta);
