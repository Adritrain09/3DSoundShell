/*
 * audio.c — Moteur audio 3DS avec streaming NDSP
 */
#include "audio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <3ds.h>

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.h"
#include <mpg123.h>
#include <opus/opusfile.h>

#define NDSP_CHANNEL     0
#define SAMPLE_RATE      44100
#define SAMPLES_PER_BUF  4096
#define NUM_BUFS         3
#define BYTES_PER_SAMPLE 2
#define CHANNELS         2

typedef enum {
    FMT_UNKNOWN, FMT_OGG, FMT_WAV, FMT_FLAC, FMT_MP3, FMT_OPUS
} AudioFormat;

static AudioState     s_state       = AUDIO_STOPPED;
static float          s_volume      = 0.8f;
static AudioFormat    s_fmt         = FMT_UNKNOWN;
static AudioMetadata  s_meta;
static float          s_viz[EQ_BANDS] = {0};

static ndspWaveBuf    s_wbufs[NUM_BUFS];
static s16           *s_pcm_buf     = NULL;
static int            s_buf_idx     = 0;

static Thread         s_thread      = NULL;
static volatile bool  s_thread_run  = false;

static u64            s_samples_played = 0;
static u32            s_sample_rate    = SAMPLE_RATE;

static stb_vorbis    *s_vorbis   = NULL;
static stb_vorbis_info s_vinfo;
static drflac        *s_flac     = NULL;
static drwav          s_wav;
static bool           s_wav_open = false;
static mpg123_handle *s_mpg      = NULL;
static OggOpusFile   *s_opus     = NULL;

/* ── Format detection ─────────────────────────────────────── */
static AudioFormat detect_fmt(const char *path)
{
    const char *e = strrchr(path, '.');
    if (!e) return FMT_UNKNOWN;
    e++;
    if (!strcasecmp(e, "ogg") || !strcasecmp(e, "oga"))  return FMT_OGG;
    if (!strcasecmp(e, "flac"))                           return FMT_FLAC;
    if (!strcasecmp(e, "wav") || !strcasecmp(e, "aiff")) return FMT_WAV;
    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2"))  return FMT_MP3;
    if (!strcasecmp(e, "opus"))                           return FMT_OPUS;
    return FMT_UNKNOWN;
}

/* ── Metadata from filename (fallback) ───────────────────── */
static void meta_from_filename(const char *path)
{
    const char *fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;
    snprintf(s_meta.title,  256, "%s", fn);
    strncpy(s_meta.artist, "Artiste inconnu", 256);
    strncpy(s_meta.album,  "Album inconnu",   256);
    s_meta.has_cover  = false;
    s_meta.cover_data = NULL;
    s_meta.cover_size = 0;
}

/* ── NDSP mix helper ─────────────────────────────────────── */
static void set_mix(float vol)
{
    float mix[12] = {0};
    mix[0] = mix[1] = vol;
    ndspChnSetMix(NDSP_CHANNEL, mix);
}

/* ── Decode one chunk ────────────────────────────────────── */
static int decode_chunk(s16 *out, int max_pairs)
{
    switch (s_fmt) {
    case FMT_OGG: {
        if (!s_vorbis) return 0;
        int got = 0;
        while (got < max_pairs) {
            float **pcm;
            int n = stb_vorbis_get_frame_float(s_vorbis, NULL, &pcm);
            if (n == 0) return got;
            int ch = s_vinfo.channels;
            for (int i = 0; i < n && got < max_pairs; i++, got++) {
                float l = pcm[0][i];
                float r = (ch > 1) ? pcm[1][i] : l;
                l = l > 1.f ? 1.f : (l < -1.f ? -1.f : l);
                r = r > 1.f ? 1.f : (r < -1.f ? -1.f : r);
                out[got*2]   = (s16)(l * 32767.f);
                out[got*2+1] = (s16)(r * 32767.f);
            }
        }
        return got;
    }
    case FMT_WAV: {
        if (!s_wav_open) return 0;
        drwav_uint64 n = drwav_read_pcm_frames_s16(&s_wav, max_pairs, out);
        if (s_wav.channels == 1)
            for (int i = (int)n - 1; i >= 0; i--) {
                out[i*2+1] = out[i];
                out[i*2]   = out[i];
            }
        return (int)n;
    }
    case FMT_FLAC: {
        if (!s_flac) return 0;
        drflac_uint64 n = drflac_read_pcm_frames_s16(s_flac, max_pairs, out);
        if (s_flac->channels == 1)
            for (int i = (int)n - 1; i >= 0; i--) {
                out[i*2+1] = out[i];
                out[i*2]   = out[i];
            }
        return (int)n;
    }
    case FMT_MP3: {
        if (!s_mpg) return 0;
        unsigned char *audio;
        size_t bytes;
        off_t num;
        int err = mpg123_decode_frame(s_mpg, &num, &audio, &bytes);
        if (err == MPG123_DONE || bytes == 0) return 0;
        int pairs = (int)(bytes / (2 * CHANNELS));
        if (pairs > max_pairs) pairs = max_pairs;
        memcpy(out, audio, pairs * CHANNELS * 2);
        return pairs;
    }
    case FMT_OPUS: {
        if (!s_opus) return 0;
        int n = op_read_stereo(s_opus, out, max_pairs * 2);
        return n <= 0 ? 0 : n;
    }
    default: return 0;
    }
}

/* ── Visualizer update ───────────────────────────────────── */
static void update_viz(const s16 *buf, int pairs)
{
    int bs = pairs / EQ_BANDS;
    if (bs < 1) bs = 1;
    for (int b = 0; b < EQ_BANDS; b++) {
        float energy = 0;
        int start = b * bs;
        int end   = start + bs;
        if (end > pairs) end = pairs;
        for (int i = start; i < end; i++) {
            float s = buf[i*2] / 32767.f;
            energy += s * s;
        }
        energy = sqrtf(energy / bs);
        float spd = (energy > s_viz[b]) ? 0.4f : 0.06f;
        s_viz[b] += (energy - s_viz[b]) * spd;
    }
}

/* ── Audio streaming thread ──────────────────────────────── */
static void audio_thread(void *arg)
{
    (void)arg;
    while (s_thread_run) {
        if (s_state != AUDIO_PLAYING) {
            svcSleepThread(16000000LL); /* 16 ms */
            continue;
        }

        ndspWaveBuf *wb = &s_wbufs[s_buf_idx];
        if (wb->status == NDSP_WBUF_QUEUED || wb->status == NDSP_WBUF_PLAYING) {
            svcSleepThread(8000000LL); /* 8 ms */
            continue;
        }

        s16 *pcm   = s_pcm_buf + s_buf_idx * SAMPLES_PER_BUF * CHANNELS;
        int  pairs = decode_chunk(pcm, SAMPLES_PER_BUF);

        if (pairs <= 0) {
            s_state = AUDIO_STOPPED;
            continue;
        }

        s_samples_played += pairs;
        update_viz(pcm, pairs);

        /* Volume scaling */
        if (s_volume != 1.0f) {
            for (int i = 0; i < pairs * CHANNELS; i++) {
                float s = pcm[i] * s_volume;
                pcm[i] = (s16)(s > 32767.f ? 32767.f : (s < -32768.f ? -32768.f : s));
            }
        }

        DSP_FlushDataCache(pcm, pairs * CHANNELS * BYTES_PER_SAMPLE);

        wb->data_vaddr = pcm;
        wb->nsamples   = pairs;
        wb->looping    = false;
        wb->status     = NDSP_WBUF_FREE;
        ndspChnWaveBufAdd(NDSP_CHANNEL, wb);

        s_buf_idx = (s_buf_idx + 1) % NUM_BUFS;
    }
}

/* ── Public API ──────────────────────────────────────────── */

Result audio_init(void)
{
    Result rc = ndspInit();
    if (R_FAILED(rc)) return rc;

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(NDSP_CHANNEL);
    ndspChnSetInterp(NDSP_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(NDSP_CHANNEL, (float)SAMPLE_RATE);
    ndspChnSetFormat(NDSP_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    set_mix(s_volume);

    size_t sz = NUM_BUFS * SAMPLES_PER_BUF * CHANNELS * BYTES_PER_SAMPLE;
    s_pcm_buf = (s16*)linearAlloc(sz);
    if (!s_pcm_buf) { ndspExit(); return -1; }
    memset(s_pcm_buf, 0, sz);
    memset(s_wbufs,   0, sizeof(s_wbufs));

    mpg123_init();

    memset(&s_meta, 0, sizeof(s_meta));
    strncpy(s_meta.title,  "Aucune piste",    256);
    strncpy(s_meta.artist, "Artiste inconnu", 256);

    s_thread_run = true;
    s_thread = threadCreate(audio_thread, NULL, 32*1024, 0x18, -1, false);
    if (!s_thread) {
        s_thread_run = false;
        linearFree(s_pcm_buf);
        s_pcm_buf = NULL;
        ndspExit();
        return -2;
    }

    return 0;
}

void audio_exit(void)
{
    s_thread_run = false;
    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    audio_stop();
    mpg123_exit();
    if (s_pcm_buf) { linearFree(s_pcm_buf); s_pcm_buf = NULL; }
    ndspExit();
}

Result audio_load(const char *path)
{
    audio_stop();
    memset(&s_meta, 0, sizeof(s_meta));
    meta_from_filename(path);

    s_fmt = detect_fmt(path);
    if (s_fmt == FMT_UNKNOWN) return -1;

    s_samples_played = 0;
    s_buf_idx        = 0;

    ndspChnReset(NDSP_CHANNEL);
    ndspChnSetInterp(NDSP_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(NDSP_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    set_mix(s_volume);

    switch (s_fmt) {
    case FMT_OGG: {
        int err = 0;
        s_vorbis = stb_vorbis_open_filename(path, &err, NULL);
        if (!s_vorbis) return -1;
        s_vinfo = stb_vorbis_get_info(s_vorbis);
        ndspChnSetRate(NDSP_CHANNEL, (float)s_vinfo.sample_rate);
        s_sample_rate = s_vinfo.sample_rate;
        int total = stb_vorbis_stream_length_in_samples(s_vorbis);
        if (s_vinfo.sample_rate > 0)
            s_meta.duration_sec = total / s_vinfo.sample_rate;
        stb_vorbis_comment c = stb_vorbis_get_comment(s_vorbis);
        for (int i = 0; i < c.comment_list_length; i++) {
            char *kv = c.comment_list[i];
            if (!strncasecmp(kv,"TITLE=",6))  snprintf(s_meta.title,256,"%s",kv+6);
            if (!strncasecmp(kv,"ARTIST=",7)) snprintf(s_meta.artist,256,"%s",kv+7);
            if (!strncasecmp(kv,"ALBUM=",6))  snprintf(s_meta.album,256,"%s",kv+6);
            if (!strncasecmp(kv,"DATE=",5))   s_meta.year = atoi(kv+5);
            if (!strncasecmp(kv,"GENRE=",6))  snprintf(s_meta.genre,64,"%s",kv+6);
        }
        break;
    }
    case FMT_WAV: {
        if (!drwav_init_file(&s_wav, path, NULL)) return -1;
        s_wav_open = true;
        s_sample_rate = s_wav.sampleRate;
        ndspChnSetRate(NDSP_CHANNEL, (float)s_wav.sampleRate);
        s_meta.duration_sec = (int)(s_wav.totalPCMFrameCount / s_wav.sampleRate);
        break;
    }
    case FMT_FLAC: {
        s_flac = drflac_open_file(path, NULL);
        if (!s_flac) return -1;
        s_sample_rate = s_flac->sampleRate;
        ndspChnSetRate(NDSP_CHANNEL, (float)s_flac->sampleRate);
        if (s_flac->sampleRate > 0)
            s_meta.duration_sec = (int)(s_flac->totalPCMFrameCount / s_flac->sampleRate);
        break;
    }
    case FMT_MP3: {
        int err;
        s_mpg = mpg123_new(NULL, &err);
        if (!s_mpg) return -1;
        mpg123_param(s_mpg, MPG123_FORCE_RATE, SAMPLE_RATE, 0);
        mpg123_param(s_mpg, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO, 0);
        if (mpg123_open(s_mpg, path) != MPG123_OK) {
            mpg123_delete(s_mpg); s_mpg = NULL; return -1;
        }
        long rate; int ch, enc;
        mpg123_getformat(s_mpg, &rate, &ch, &enc);
        mpg123_format_none(s_mpg);
        mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);
        ndspChnSetRate(NDSP_CHANNEL, (float)rate);
        s_sample_rate = rate;
        off_t len = mpg123_length(s_mpg);
        if (len > 0 && rate > 0)
            s_meta.duration_sec = (int)(len / rate);
        mpg123_id3v1 *v1; mpg123_id3v2 *v2;
        if (mpg123_id3(s_mpg, &v1, &v2) == MPG123_OK) {
            if (v2) {
                if (v2->title)  snprintf(s_meta.title,256,"%s",v2->title->p);
                if (v2->artist) snprintf(s_meta.artist,256,"%s",v2->artist->p);
                if (v2->album)  snprintf(s_meta.album,256,"%s",v2->album->p);
            } else if (v1) {
                snprintf(s_meta.title,256,"%s",v1->title);
                snprintf(s_meta.artist,256,"%s",v1->artist);
            }
        }
        break;
    }
    case FMT_OPUS: {
        int err = 0;
        s_opus = op_open_file(path, &err);
        if (!s_opus) return -1;
        ndspChnSetRate(NDSP_CHANNEL, 48000.f);
        s_sample_rate = 48000;
        ogg_int64_t len = op_pcm_total(s_opus, -1);
        if (len > 0) s_meta.duration_sec = (int)(len / 48000);
        const OpusTags *tags = op_tags(s_opus, -1);
        if (tags) {
            const char *t = opus_tags_query(tags,"TITLE",0);
            const char *a = opus_tags_query(tags,"ARTIST",0);
            const char *b = opus_tags_query(tags,"ALBUM",0);
            if (t) snprintf(s_meta.title,256,"%s",t);
            if (a) snprintf(s_meta.artist,256,"%s",a);
            if (b) snprintf(s_meta.album,256,"%s",b);
        }
        break;
    }
    default: return -1;
    }
    return 0;
}

void audio_play(void)
{
    if (s_fmt == FMT_UNKNOWN) return;
    s_state = AUDIO_PLAYING;
    ndspChnSetPaused(NDSP_CHANNEL, false);
}

void audio_pause(void)
{
    s_state = AUDIO_PAUSED;
    ndspChnSetPaused(NDSP_CHANNEL, true);
}

void audio_stop(void)
{
    s_state = AUDIO_STOPPED;
    ndspChnWaveBufClear(NDSP_CHANNEL);
    svcSleepThread(16000000LL); /* laisser le thread finir */

    if (s_vorbis)   { stb_vorbis_close(s_vorbis); s_vorbis = NULL; }
    if (s_flac)     { drflac_close(s_flac);        s_flac   = NULL; }
    if (s_wav_open) { drwav_uninit(&s_wav);         s_wav_open = false; }
    if (s_mpg)      { mpg123_close(s_mpg); mpg123_delete(s_mpg); s_mpg = NULL; }
    if (s_opus)     { op_free(s_opus);              s_opus   = NULL; }

    s_samples_played = 0;
    s_fmt            = FMT_UNKNOWN;
    s_sample_rate    = SAMPLE_RATE;
    memset(s_wbufs, 0, sizeof(s_wbufs));
    s_buf_idx = 0;
}

void audio_toggle_pause(void)
{
    if      (s_state == AUDIO_PLAYING) audio_pause();
    else if (s_state == AUDIO_PAUSED)  audio_play();
}

void audio_seek(int seconds)
{
    if (seconds < 0) seconds = 0;
    u64 target = (u64)seconds * s_sample_rate;
    switch (s_fmt) {
    case FMT_OGG:  if (s_vorbis)   stb_vorbis_seek(s_vorbis, (unsigned int)target); break;
    case FMT_WAV:  if (s_wav_open) drwav_seek_to_pcm_frame(&s_wav, target);         break;
    case FMT_FLAC: if (s_flac)     drflac_seek_to_pcm_frame(s_flac, target);        break;
    case FMT_MP3:  if (s_mpg)      mpg123_seek(s_mpg, (off_t)target, SEEK_SET);     break;
    case FMT_OPUS: if (s_opus)     op_pcm_seek(s_opus, (ogg_int64_t)target);        break;
    default: break;
    }
    s_samples_played = target;
    ndspChnWaveBufClear(NDSP_CHANNEL);
    s_buf_idx = 0;
}

int   audio_get_position(void)     { return s_sample_rate ? (int)(s_samples_played / s_sample_rate) : 0; }
int   audio_get_duration(void)     { return s_meta.duration_sec; }
float audio_get_position_pct(void) { return s_meta.duration_sec > 0 ? (float)audio_get_position() / s_meta.duration_sec : 0.f; }

void  audio_set_volume(float v)
{
    s_volume = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    set_mix(s_volume);
}
float      audio_get_volume(void)      { return s_volume; }
AudioState audio_get_state(void)       { return s_state;  }
bool       audio_is_finished(void)     { return s_state == AUDIO_STOPPED && s_meta.duration_sec > 0; }
const AudioMetadata *audio_get_metadata(void) { return &s_meta; }

void  audio_get_visualizer(float out[EQ_BANDS])
{
    for (int i = 0; i < EQ_BANDS; i++) out[i] = s_viz[i];
}

void  audio_eq_set_gain(int b, float db)    { if (b >= 0 && b < EQ_BANDS) (void)db; /* TODO: apply EQ */ }
float audio_eq_get_gain(int b)              { return 0.f; (void)b; }
void  audio_eq_apply_preset(const EQPreset *p) { (void)p; }

EQPreset eq_preset_flat       = {{0,0,0,0,0,0,0,0},    "Flat"};
EQPreset eq_preset_bass_boost = {{6,5,4,1,0,0,0,0},    "Bass Boost"};
EQPreset eq_preset_vocal      = {{-2,-1,2,4,4,3,1,0},  "Vocal"};
EQPreset eq_preset_rock       = {{4,3,0,-1,0,2,4,5},   "Rock"};
EQPreset eq_preset_classical  = {{3,2,0,0,0,0,2,3},    "Classical"};
EQPreset eq_preset_electronic = {{5,4,0,-1,1,3,4,5},   "Electronic"};