/* soundtouch_wrapper.h - Interface C pour SoundTouch */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callback decodeur : remplit buf avec max_frames frames stereo int16.
   Retourne le nombre de frames ecrites, 0 ou negatif = fin de flux. */
typedef int (*st_source_fn)(int16_t *buf, int max_frames, void *ctx);

/* Init / destroy */
void st_init(int sample_rate, int channels);
void st_destroy(void);

/* Parametres */
/* Reglage economique du WSOLA (Old 3DS) : reduit la fenetre de recherche de
   correlation, ce qui baisse le cout CPU du time-stretch. A appeler apres
   st_init(). Ne touche ni au filtre AA ni a SEQUENCE_MS (deux reglages dont
   l historique du projet a montre qu ils degradent le rythme de sortie). */
void st_set_lowcpu(bool on);
void st_set_speed(float speed);          /* ratio : 1.0 = normal */
void st_set_pitch(float pitch_factor);   /* ratio : 1.0 = normal, 2^(s/12) pour semitones */
bool st_is_identity(void);

/* Source (decodeur) */
void st_set_source(st_source_fn fn, void *ctx);

/* === NOUVELLE API PRINCIPALE === */
/* Remplit out avec want_frames frames stereo int16.
   Appelle la source (decodeur) en interne via pattern pull.
   Retourne le nombre de frames effectivement ecrites. */
int st_fill_buffer(int16_t *out, int want_frames);

/* Ancienne API (compatible, mais sans callback) */
int st_process(const int16_t *in, int in_frames,
               int16_t *out, int max_out_frames);

/* Controle */
void st_flush(void);    /* vider + clear (seek, fin de piste) */
void st_reset(int sample_rate);

#ifdef __cplusplus
}
#endif
