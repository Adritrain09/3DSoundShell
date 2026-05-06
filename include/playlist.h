#pragma once
#include <stdbool.h>
#include "audio.h"
#include "main.h"

// ─── Playlist entry ───────────────────────────────────────────
typedef struct {
    char path[MAX_PATH];
    char title[MAX_FILENAME];
    int  duration_sec;
} PlaylistEntry;

// ─── Playlist ─────────────────────────────────────────────────
typedef struct {
    PlaylistEntry items[MAX_PLAYLIST];
    int           count;
    int           current;          // index of playing track
    int           selected;         // cursor in playlist view
    int           scroll_offset;
    bool          shuffle;
    RepeatMode    repeat;
    int           shuffle_order[MAX_PLAYLIST]; // permutation
} Playlist;

void pl_init(Playlist *pl);
void pl_add(Playlist *pl, const char *path, const char *title);
void pl_add_dir(Playlist *pl, const char *dir_path);
void pl_remove(Playlist *pl, int index);
void pl_clear(Playlist *pl);

// Navigation
int  pl_next(Playlist *pl);     // returns new index, -1 if end
int  pl_prev(Playlist *pl);
int  pl_jump(Playlist *pl, int index);

// Shuffle
void pl_toggle_shuffle(Playlist *pl);
void pl_rebuild_shuffle(Playlist *pl);

// Save/load .m3u
bool pl_save(const Playlist *pl, const char *path);
bool pl_load(Playlist *pl, const char *path);

// Draw
void pl_draw_top(const Playlist *pl);
void pl_draw_bottom(Playlist *pl);

// Expose RepeatMode for audio.h include order
