/* soundtouch_wrapper.cpp - Wrapper C autour de SoundTouch C++
   Version INTEGER_SAMPLES : pas de conversion float, direct int16.
   Optimise pour Old 3DS. */

#include "soundtouch_wrapper.h"
#include "SoundTouch.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

/* Taille du chunk interne de decodage par iteration */
/* CLOT8c : 4096 = taille du creneau du thread audio (avant : 2 tours par buffer) */
#define ST_CHUNK_FRAMES 4096

static soundtouch::SoundTouch *s_st   = nullptr;
static bool   s_initialized           = false;
static bool   s_source_eof            = false;
static bool   s_flushed               = false;
static float  s_speed                 = 1.0f;
static float  s_pitch                 = 1.0f;
static int    s_channels              = 2;

/* Callback vers le decodeur */
static st_source_fn s_source_fn       = nullptr;
static void        *s_source_ctx      = nullptr;

/* Buffer interne pour recevoir les samples traites */
static int16_t s_out_buf[8192 * 2];

extern "C" {

void st_init(int sample_rate, int channels)
{
    if (s_st) { delete s_st; s_st = nullptr; }

    s_st = new soundtouch::SoundTouch();
    s_channels = channels;
    s_st->setChannels((uint)channels);
    s_st->setSampleRate((uint)sample_rate);
    s_st->setTempo(1.0f);
    s_st->setPitch(1.0f);

    /* Settings optimises pour Old 3DS :
       - QUICKSEEK=1 : moins precis mais plus rapide
       - SEQUENCE_MS plus court = moins de latence
       - SEEKWINDOW_MS plus court = moins de calculs */
    s_st->setSetting(SETTING_USE_QUICKSEEK,  1);
    s_st->setSetting(SETTING_USE_AA_FILTER, 1); /* CLOT3b : filtre AA remise comme en V0.98 (rythme de sortie a 0.99x) */
    s_st->setSetting(SETTING_SEQUENCE_MS,   15); /* CLOT3c : sequence 15ms V0.98 (la 25 de FIX1b est le soupcon de POC periodiques) */
    s_st->setSetting(SETTING_SEEKWINDOW_MS,  8);
    s_st->setSetting(SETTING_OVERLAP_MS,     4);

    s_speed       = 1.0f;
    s_pitch       = 1.0f;
    s_source_eof  = false;
    s_flushed     = false;
    s_initialized = true;
}

void st_set_lowcpu(bool on)
{
    if (!s_st) return;
    /* Le gros du cout WSOLA est la recherche de correlation, proportionnelle a
       SEEKWINDOW x OVERLAP. On les resserre sur Old 3DS pour rendre du temps
       CPU (mesure perfdiag : le thread audio saturait ~90% d un coeur avec
       EQ + SoundTouch, ce qui affamait l interface).
       Volontairement PAS touche ici : SETTING_USE_AA_FILTER (le couper avait
       fait deriver le rythme de sortie a 0.99x, cf CLOT3b) et SEQUENCE_MS
       (25 ms avait ete soupconne de POC periodiques, cf CLOT3c). */
    s_st->setSetting(SETTING_SEEKWINDOW_MS, on ? 5 : 8);
    s_st->setSetting(SETTING_OVERLAP_MS,    on ? 3 : 4);
}

void st_set_speed(float speed)
{
    if (!s_st) return;
    if (speed < 0.25f) speed = 0.25f;
    if (speed > 4.0f)  speed = 4.0f;
    if (std::fabs(s_speed - speed) < 1e-6f) return;
    s_speed = speed;
    s_st->setTempo(speed);
}

void st_set_pitch(float pitch_factor)
{
    if (!s_st) return;
    if (pitch_factor < 0.1f) pitch_factor = 0.1f;
    if (pitch_factor > 8.0f) pitch_factor = 8.0f;
    if (std::fabs(s_pitch - pitch_factor) < 1e-6f) return;
    s_pitch = pitch_factor;
    s_st->setPitch(pitch_factor);
}

bool st_is_identity(void)
{
    return std::fabs(s_speed - 1.0f) < 1e-4f
        && std::fabs(s_pitch - 1.0f) < 1e-4f;
}

void st_set_source(st_source_fn fn, void *ctx)
{
    s_source_fn  = fn;
    s_source_ctx = ctx;
}

/* Fonction principale : remplir want_frames de sortie
   Pattern PULL : on appelle s_source_fn quand SoundTouch veut plus
   Version INTEGER : pas de conversion, direct int16 */
int st_fill_buffer(int16_t *out, int want_frames)
{
    if (!s_st || !s_initialized || !out || want_frames <= 0) return 0;
    if (!s_source_fn) return 0;

    int produced = 0;

    while (produced < want_frames) {
        int rem = want_frames - produced;

        /* 1. Tenter de recevoir des frames deja traitees */
        uint got = s_st->receiveSamples(
            (soundtouch::SAMPLETYPE*)(out + produced * s_channels),
            (uint)(rem < 4096 ? rem : 4096));

        if (got > 0) {
            produced += (int)got;
            continue;
        }

        /* 2. SoundTouch n a plus rien en stock */
        if (s_source_eof) {
            if (s_flushed) break;
            s_st->flush();
            s_flushed = true;
            continue;
        }

        /* 3. Demander plus de donnees au decodeur */
        static int16_t chunk_i16[ST_CHUNK_FRAMES * 2];
        int decoded = s_source_fn(chunk_i16, ST_CHUNK_FRAMES, s_source_ctx);

        if (decoded <= 0) {
            s_source_eof = true;
            continue;
        }

        /* 4. Passer directement a SoundTouch (pas de conversion) */
        s_st->putSamples((const soundtouch::SAMPLETYPE*)chunk_i16, (uint)decoded);
    }

    return produced;
}

/* Ancienne interface conservee pour compatibilite */
int st_process(const int16_t *in, int in_frames, int16_t *out, int max_out_frames)
{
    if (!s_st || !s_initialized) {
        int copy = in_frames < max_out_frames ? in_frames : max_out_frames;
        if (out && in) memcpy(out, in, (size_t)copy * 2 * sizeof(int16_t));
        return copy;
    }
    if (st_is_identity()) {
        int copy = in_frames < max_out_frames ? in_frames : max_out_frames;
        if (out && in) memcpy(out, in, (size_t)copy * 2 * sizeof(int16_t));
        return copy;
    }

    /* Direct int16 */
    s_st->putSamples((const soundtouch::SAMPLETYPE*)in, (uint)in_frames);

    int produced = 0;
    while (produced < max_out_frames) {
        uint got = s_st->receiveSamples(
            (soundtouch::SAMPLETYPE*)(out + produced * s_channels),
            (uint)(max_out_frames - produced));
        if (got == 0) break;
        produced += (int)got;
    }
    return produced;
}

void st_flush(void)
{
    if (!s_st) return;
    s_st->flush();
    uint got;
    do {
        got = s_st->receiveSamples((soundtouch::SAMPLETYPE*)s_out_buf, 256);
    } while (got > 0);
    s_st->clear();
    s_source_eof = false;
    s_flushed    = false;
}

void st_reset(int sample_rate)
{
    if (!s_st) return;
    s_st->clear();
    s_st->setSampleRate((uint)sample_rate);
    s_st->setTempo(s_speed);
    s_st->setPitch(s_pitch);
    s_source_eof = false;
    s_flushed    = false;
}

void st_destroy(void)
{
    if (s_st) { delete s_st; s_st = nullptr; }
    s_initialized = false;
    s_source_fn   = nullptr;
    s_source_ctx  = nullptr;
}

} /* extern "C" */
