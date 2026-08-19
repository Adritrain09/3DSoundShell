// audio.h
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
    char format[8];
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
void   audio_set_speed(float speed);  // 0.5 - 2.0
float  audio_get_volume(void);

AudioState audio_get_state(void);
bool       audio_is_finished(void);

// Metadata
const AudioMetadata *audio_get_metadata(void);

// Visualizer (returns amplitude per band, 0.0-1.0)
void  audio_get_visualizer(float out[EQ_BANDS]);
void  audio_get_visualizer_fft(float out[EQ_BANDS]);

/* V0.96 : desactiver temporairement le calcul viz (mode eco energie) */
void  audio_set_viz_active(bool active);

/* V0.97 : Crossfade - precache la piste suivante en PCM
   Retourne true si le precache a reussi
   duration_sec : combien de secondes precacher (max 5) */
bool  audio_xfade_precache(const char *path, int duration_sec);

/* V0.97 : Precache asynchrone - lance dans un thread separe (pas de freeze) */
void  audio_xfade_precache_async(const char *path, int duration_sec);

/* V0.97 : Demarre la lecture du buffer precache sur canal 1
   Doit etre appele quand on veut declencher le crossfade */
void  audio_xfade_start(void);

/* V0.97 : Applique les volumes crossfade
   vol_current : 0.0 a 1.0 (canal 0, piste actuelle)
   vol_next    : 0.0 a 1.0 (canal 1, piste suivante) */
void  audio_xfade_set_mix(float vol_current, float vol_next);

/* V0.97 : Arrete le crossfade et libere le buffer precache */
void  audio_xfade_stop(void);

/* V0.97 : Retourne true si le buffer est preche pour ce path */
bool  audio_xfade_is_precached(const char *path);

/* V0.97 : Retourne true si le crossfade est en cours */
bool  audio_xfade_is_active(void);

/* V0.97 : recupere les samples bruts pour l oscilloscope
   Retourne le nombre de samples ecrits (max 256)
   Les samples sont normalises entre -1.0 et 1.0 (mono, moyenne stereo) */
int   audio_get_oscillo(float out[256]);

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
