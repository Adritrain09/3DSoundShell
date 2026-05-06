#pragma once
#include <3ds.h>
#include <stdbool.h>

// ─── Input context (passed each frame) ───────────────────────
typedef struct {
    u32 held;
    u32 down;
    u32 up;
    touchPosition touch;
    bool touch_down;
    bool touch_held;
} InputState;

void input_update(InputState *inp);

// ─── Key combo helpers ────────────────────────────────────────
// L+R = pause/play
#define COMBO_PAUSE     (KEY_L | KEY_R)
// Left/Right = prev/next track
#define KEY_PREV        KEY_LEFT
#define KEY_NEXT        KEY_RIGHT
// Up/Down = volume
#define KEY_VOL_UP      KEY_UP
#define KEY_VOL_DOWN    KEY_DOWN
// A = confirm / enter dir
// B = back
// X = add to playlist
// Y = toggle shuffle
// Start = go to player
// Select = go to file browser
// ZL / ZR (New3DS) = seek -10s / +10s
#define KEY_SEEK_BACK   KEY_ZL
#define KEY_SEEK_FWD    KEY_ZR
// DPAD in player: prev/next track already mapped
// Touch drag on progress bar = seek
