#pragma once
#include <stdbool.h>
#include "main.h"

// ─── File entry ───────────────────────────────────────────────
typedef struct {
    char name[MAX_FILENAME];
    char full_path[MAX_PATH];
    bool is_dir;
    bool is_audio;
    long size;
} FileEntry;

// ─── Browser state ────────────────────────────────────────────
typedef struct {
    char        cwd[MAX_PATH];          // current directory
    FileEntry   entries[MAX_FILES];
    int         count;
    int         selected;               // cursor position
    int         scroll_offset;          // for scrolling
    int         visible_rows;           // number of visible rows
} FileBrowser;

void   fb_init(FileBrowser *fb, const char *start_path);
void   fb_enter_dir(FileBrowser *fb, const char *path);
void   fb_go_up(FileBrowser *fb);
void   fb_reload(FileBrowser *fb);
void   fb_select_next(FileBrowser *fb);
void   fb_select_prev(FileBrowser *fb);
bool   fb_is_audio(const char *filename);
void   fb_draw_top(const FileBrowser *fb);   // draws on top screen
void   fb_draw_bottom(const FileBrowser *fb);// draws on bottom screen
