#pragma once
#include <citro2d.h>
#include <3ds.h>
#include "audio.h"
#include "playlist.h"
#include "theme.h"

// ─── Visualizer styles ────────────────────────────────────────
typedef enum {
    VIZ_BARS,
    VIZ_WAVE,
    VIZ_CIRCLE,
    VIZ_EQ,
    VIZ_COUNT
} VisualizerStyle;

// ─── Player UI state ──────────────────────────────────────────
typedef struct {
    VisualizerStyle   viz_style;
    float             viz_data[EQ_BANDS];
    float             viz_smooth[EQ_BANDS];
    int               cover_tex_id;
    bool              cover_loaded;
    float             scroll_title_x;
    int               scroll_pause_timer;
    u64               last_tick;
    C3D_Tex           cover_tex;
    Tex3DS_SubTexture cover_subtex;
    bool              cover_tex_init;
    int               cover_w;
    int               cover_h;
} PlayerUI;

void player_ui_init(PlayerUI *ui);
void player_ui_update(PlayerUI *ui, const AudioMetadata *meta, float dt);
void player_ui_draw_top(const PlayerUI *ui, const AudioMetadata *meta,
                        const Playlist *pl);
void player_ui_draw_bottom(PlayerUI *ui, const AudioMetadata *meta,
                           const Playlist *pl);
void player_ui_cycle_viz(PlayerUI *ui);
void player_ui_exit(PlayerUI *ui);
void player_ui_load_cover(PlayerUI *ui, const AudioMetadata *meta);