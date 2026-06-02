#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include 3ds.h

 Initialisation et déinitialisation
bool audio_init(void);
void audio_deinit(void);

 Contrôle de lecture
void play_current_track(void);
void audio_play(void);
void audio_pause(void);
void audio_stop(void);
void audio_seek(u32 positionMs);
u32 audio_get_position(void);
u32 audio_get_duration(const char path);

 Égaliseur
void audio_eq_set_band(float gainDb, u32 bandIndex);
float audio_eq_get_band(u32 bandIndex);

#endif  AUDIO_PLAYER_H
