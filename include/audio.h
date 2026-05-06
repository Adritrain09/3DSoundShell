#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>

// ─── Playback states ──────────────────────────────────────────
typedef enum {
    AUDIO_STOPPED,
    AUDIO_PLAYING,
    AUDIO_PAUSED
} AudioState;

// ─── Shuffle / Repeat modes ───────────────────────────────────
typedef enum {
    REPEAT_NONE,
    REPEAT_ONE,
    REPEAT_ALL
} RepeatMode;

// ─── Metadata ─────────────────────────────────────────────────
typedef struct {
    char title[256];
    char artist[256];
    char album[256];
    char genre[64];
    int  year;
    int  track_num;
    int  duration_sec;    // total duration in seconds
    bool has_cover;
    u8  *cover_data;      // JPEG/PNG raw bytes
    u32  cover_size;
    int  cover_width;
    int  cover_height;
} AudioMetadata;

// ─── Equalizer bands ─────────────────────────────────────────
#define EQ_BANDS 8
typedef struct {
    float gain[EQ_BANDS];  // -12.0 to +12.0 dB
    const char *name;
} EQPreset;

// ─── Audio engine API ─────────────────────────────────────────
Result audio_init(void);
void   audio_exit(void);

Result audio_load(const char *path);
void   audio_play(void);
void   audio_pause(void);
void   audio_stop(void);
void   audio_toggle_pause(void);

void   audio_seek(int seconds);
int    audio_get_position(void);   // current position in seconds
int    audio_get_duration(void);
float  audio_get_position_pct(void);

void   audio_set_volume(float vol); // 0.0 - 1.0
float  audio_get_volume(void);

AudioState audio_get_state(void);
bool       audio_is_finished(void);

// Metadata
const AudioMetadata *audio_get_metadata(void);

// Visualizer (returns amplitude per band, 0.0-1.0)
void  audio_get_visualizer(float out[EQ_BANDS]);

// Equalizer
void  audio_eq_set_gain(int band, float db);
float audio_eq_get_gain(int band);
void  audio_eq_apply_preset(const EQPreset *preset);

// Built-in EQ presets
extern EQPreset eq_preset_flat;
extern EQPreset eq_preset_bass_boost;
extern EQPreset eq_preset_vocal;
extern EQPreset eq_preset_rock;
extern EQPreset eq_preset_classical;
extern EQPreset eq_preset_electronic;
