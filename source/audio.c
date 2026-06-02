/*
** audio.c - Moteur audio 3DS avec streaming NDSP
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
#include <opusfile.h>

#define NDSP_CHANNEL    0
#define SAMPLE_RATE     44100
#define SAMPLES_PER_BUF 8192
#define NUM_BUFS        3
#define BYTES_PER_SAMPLE 2
#define CHANNELS        2

typedef enum {
    FMT_UNKNOWN, FMT_OGG, FMT_WAV, FMT_FLAC, FMT_MP3, FMT_OPUS
} AudioFormat;

static AudioState        s_state       = AUDIO_STOPPED;
static float             s_volume      = 0.8f;
static AudioFormat       s_fmt         = FMT_UNKNOWN;
static AudioMetadata     s_meta;
static float             s_viz[EQ_BANDS] = {0};

static ndspWaveBuf       s_wbufs[NUM_BUFS];
static s16              *s_pcm_buf    = NULL;
static int               s_buf_idx    = 0;

static Thread            s_thread     = NULL;
static volatile bool     s_thread_run = false;

static u64               s_samples_played = 0;
static u32               s_sample_rate    = SAMPLE_RATE;

static stb_vorbis       *s_vorbis    = NULL;
static stb_vorbis_info   s_vinfo;
static drflac           *s_flac      = NULL;
static drwav             s_wav;
static bool              s_wav_open  = false;
static mpg123_handle    *s_mpg       = NULL;
static OggOpusFile      *s_opus      = NULL;

/* ── Cover art ──────────────────────────────────────────────── */
static void free_cover(void)
{
    if (s_meta.cover_data) { free(s_meta.cover_data); s_meta.cover_data = NULL; }
    s_meta.cover_size = 0;
    s_meta.has_cover  = false;
}

static void extract_id3v2_cover(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    u8 hdr[10];
    if (fread(hdr, 1, 10, f) != 10 || memcmp(hdr, "ID3", 3)) { fclose(f); return; }
    int ver = hdr[3];
    u32 tag_size = ((hdr[6]&0x7F)<<21)|((hdr[7]&0x7F)<<14)|((hdr[8]&0x7F)<<7)|(hdr[9]&0x7F);
    u32 pos = 0;
    u8 fh[10];
    while (pos + 10 < tag_size) {
        if (fread(fh, 1, 10, f) != 10) break;
        pos += 10;
        u32 fsz;
        if (ver >= 4)
            fsz = ((fh[4]&0x7F)<<21)|((fh[5]&0x7F)<<14)|((fh[6]&0x7F)<<7)|(fh[7]&0x7F);
        else
            fsz = (fh[4]<<24)|(fh[5]<<16)|(fh[6]<<8)|fh[7];
        if (fsz == 0 || fsz > tag_size) break;
        if (memcmp(fh, "APIC", 4) == 0) {
            u8 *data = malloc(fsz);
            if (!data) break;
            if (fread(data, 1, fsz, f) != (size_t)fsz) { free(data); break; }
            u32 off = 1;
            while (off < fsz && data[off]) off++;
            off++;
            if (off >= fsz) { free(data); break; }
            off++;
            while (off < fsz && data[off]) off++;
            off++;
            if (off < fsz) {
                u32 isz = fsz - off;
                s_meta.cover_data = malloc(isz);
                if (s_meta.cover_data) {
                    memcpy(s_meta.cover_data, data + off, isz);
                    s_meta.cover_size = isz;
                    s_meta.has_cover  = true;
                }
            }
            free(data);
            break;
        } else {
            fseek(f, (long)fsz, SEEK_CUR);
            pos += fsz;
        }
    }
    fclose(f);
}

static const signed char b64t[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static u8 *b64_decode(const char *src, u32 *out_len)
{
    u32 len = (u32)strlen(src);
    u8 *out = malloc((len / 4) * 3 + 4);
    if (!out) return NULL;
    u32 j = 0;
    for (u32 i = 0; i + 3 < len; i += 4) {
        s32 a = b64t[(u8)src[i]];
        s32 b2= b64t[(u8)src[i+1]];
        s32 c = b64t[(u8)src[i+2]];
        s32 d = b64t[(u8)src[i+3]];
        if (a < 0 || b2 < 0) break;
        out[j++] = (u8)((a << 2) | (b2 >> 4));
        if (src[i+2] != '=' && c >= 0) out[j++] = (u8)((b2 << 4) | (c >> 2));
        if (src[i+3] != '=' && d >= 0) out[j++] = (u8)((c  << 6) | d);
    }
    *out_len = j;
    return out;
}

static void extract_ogg_cover(stb_vorbis_comment *vc)
{
    for (int i = 0; i < vc->comment_list_length; i++) {
        const char *kv = vc->comment_list[i];
        if (strncasecmp(kv, "METADATA_BLOCK_PICTURE=", 23) != 0) continue;
        u32 blen = 0;
        u8 *bin = b64_decode(kv + 23, &blen);
        if (!bin || blen < 32) { free(bin); continue; }
        u32 off = 4;
        u32 ml = (bin[off]<<24)|(bin[off+1]<<16)|(bin[off+2]<<8)|bin[off+3];
        off += 4 + ml;
        if (off + 4 > blen) { free(bin); continue; }
        u32 dl = (bin[off]<<24)|(bin[off+1]<<16)|(bin[off+2]<<8)|bin[off+3];
        off += 4 + dl + 16;
        if (off + 4 > blen) { free(bin); continue; }
        u32 isz = (bin[off]<<24)|(bin[off+1]<<16)|(bin[off+2]<<8)|bin[off+3];
        off += 4;
        if (off + isz <= blen && isz > 0) {
            s_meta.cover_data = malloc(isz);
            if (s_meta.cover_data) {
                memcpy(s_meta.cover_data, bin + off, isz);
                s_meta.cover_size = isz;
                s_meta.has_cover  = true;
            }
        }
        free(bin);
        break;
    }
}

/* ── Format detection ───────────────────────────────────────── */
static AudioFormat detect_fmt(const char *path)
{
    const char *e = strrchr(path, '.');
    if (!e) return FMT_UNKNOWN;
    e++;
    if (!strcasecmp(e,"ogg") || !strcasecmp(e,"oga"))  return FMT_OGG;
    if (!strcasecmp(e,"flac"))                          return FMT_FLAC;
    if (!strcasecmp(e,"wav") || !strcasecmp(e,"aiff")) return FMT_WAV;
    if (!strcasecmp(e,"mp3") || !strcasecmp(e,"mp2") || !strcasecmp(e,"aac")) return FMT_MP3;
    if (!strcasecmp(e,"opus"))                          return FMT_OPUS;
    return FMT_UNKNOWN;
}

static void meta_from_filename(const char *path)
{
    const char *fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;
    snprintf(s_meta.title,  sizeof(s_meta.title),  "%s", fn);
    strncpy(s_meta.artist, "Artiste inconnu", sizeof(s_meta.artist)-1);
    strncpy(s_meta.album,  "Album inconnu",   sizeof(s_meta.album)-1);
    free_cover();
}

static void set_mix(float vol)
{
    float mix[12] = {0};
    mix[0] = mix[1] = vol;
    ndspChnSetMix(NDSP_CHANNEL, mix);
}

/* ── Decode one chunk ───────────────────────────────────────── */
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
                    l = l >  1.f ?  1.f : (l < -1.f ? -1.f : l);
                    r = r >  1.f ?  1.f : (r < -1.f ? -1.f : r);
                    out[got*2]   = (s16)(l * 32767.f);
                    out[got*2+1] = (s16)(r * 32767.f);
                }
            }
            return got;
        }
        case FMT_WAV: {
            if (!s_wav_open) return 0;
            drwav_uint64 n = drwav_read_pcm_frames_s16(&s_wav, max_pairs, out);
            if (s_wav.channels == 1) {
                for (int i = (int)n - 1; i >= 0; i--) {
                    out[i*2+1] = out[i];
                    out[i*2]   = out[i];
                }
            }
            return (int)n;
        }
        case FMT_FLAC: {
            if (!s_flac) return 0;
            drflac_uint64 n = drflac_read_pcm_frames_s16(s_flac, max_pairs, out);
            if (s_flac->channels == 1) {
                for (int i = (int)n - 1; i >= 0; i--) {
                    out[i*2+1] = out[i];
                    out[i*2]   = out[i];
                }
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

/* ── Visualizer ─────────────────────────────────────────────── */
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

/* ── Audio thread ───────────────────────────────────────────── */
static LightLock s_audio_lock;
static bool      s_lock_ready = false;

static void audio_lock_init(void)
{
    if (!s_lock_ready) {
        LightLock_Init(&s_audio_lock);
        s_lock_ready = true;
    }
}

static void audio_thread(void *arg)
{
    (void)arg;
    while (s_thread_run) {
        if (s_state != AUDIO_PLAYING) {
            svcSleepThread(16000000LL);
            continue;
        }
        ndspWaveBuf *wb = &s_wbufs[s_buf_idx];
        if (wb->status == NDSP_WBUF_QUEUED || wb->status == NDSP_WBUF_PLAYING) {
            svcSleepThread(8000000LL);
            continue;
        }

        /* Lock pendant le decode */
        LightLock_Lock(&s_audio_lock);
        s16 *pcm   = s_pcm_buf + s_buf_idx * SAMPLES_PER_BUF * CHANNELS;
        int  pairs = decode_chunk(pcm, SAMPLES_PER_BUF);
        LightLock_Unlock(&s_audio_lock);

        if (pairs <= 0) {
            s_state = AUDIO_STOPPED;
            continue;
        }
        s_samples_played += pairs;
        update_viz(pcm, pairs);
        if (s_volume != 1.0f) {
            for (int i = 0; i < pairs * CHANNELS; i++) {
                float sv = pcm[i] * s_volume;
                pcm[i] = (s16)(sv > 32767.f ? 32767.f : (sv < -32768.f ? -32768.f : sv));
            }
        }
        DSP_FlushDataCache(pcm, pairs * CHANNELS * BYTES_PER_SAMPLE);
        wb->data_vaddr = pcm;
        wb->nsamples   = pairs;
        wb->looping    = false;
        ndspChnWaveBufAdd(NDSP_CHANNEL, wb);
        s_buf_idx = (s_buf_idx + 1) % NUM_BUFS;
    }
}

/* ── Public API ─────────────────────────────────────────────── */
Result audio_init(void)
{
    audio_lock_init();
    Result rc = ndspInit();
    if (R_FAILED(rc)) {
        if (rc != (Result)0xD880A7FA) return rc;
    }

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(NDSP_CHANNEL);
    ndspChnSetInterp(NDSP_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(NDSP_CHANNEL, (float)SAMPLE_RATE);
    ndspChnSetFormat(NDSP_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    set_mix(s_volume);

    size_t sz = NUM_BUFS * SAMPLES_PER_BUF * CHANNELS * BYTES_PER_SAMPLE;
    s_pcm_buf = (s16*)linearAlloc(sz);
    if (!s_pcm_buf) { ndspExit(); return (Result)-1; }
    memset(s_pcm_buf, 0, sz);
    memset(s_wbufs,   0, sizeof(s_wbufs));

    mpg123_init();
    memset(&s_meta, 0, sizeof(s_meta));
    strncpy(s_meta.title,  "Aucune piste",    sizeof(s_meta.title)-1);
    strncpy(s_meta.artist, "Artiste inconnu", sizeof(s_meta.artist)-1);

    s_thread_run = true;
    s_thread = threadCreate(audio_thread, NULL, 32*1024, 0x18, -1, false);
    if (!s_thread) {
        s_thread_run = false;
        free_cover();
        linearFree(s_pcm_buf);
        s_pcm_buf = NULL;
        mpg123_exit();
        ndspExit();
        return (Result)-2;
    }
    return 0;
}

void audio_exit(void)
{
    s_thread_run = false;
    s_state      = AUDIO_STOPPED;

    svcSleepThread(50000000ULL);
    ndspChnWaveBufClear(NDSP_CHANNEL);

    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }

    if (s_vorbis)   { stb_vorbis_close(s_vorbis); s_vorbis   = NULL; }
    if (s_flac)     { drflac_close(s_flac);        s_flac     = NULL; }
    if (s_wav_open) { drwav_uninit(&s_wav);         s_wav_open = false; }
    if (s_mpg)      { mpg123_close(s_mpg); mpg123_delete(s_mpg); s_mpg = NULL; }
    if (s_opus)     { op_free(s_opus);              s_opus     = NULL; }

    free_cover();
    mpg123_exit();
    if (s_pcm_buf) { linearFree(s_pcm_buf); s_pcm_buf = NULL; }
    ndspExit();
}

Result audio_load(const char *path)
{
    audio_lock_init();
    /* Log timestamp */
    { FILE *_f=fopen("sdmc:/3DSoundShell/debug.log","a");
      if(_f){fprintf(_f,"audio_load START: %s\n",path);fclose(_f);} }
    LightLock_Lock(&s_audio_lock);
    audio_stop();
    { FILE *_f=fopen("sdmc:/3DSoundShell/debug.log","a");
      if(_f){fprintf(_f,"audio_load: apres stop\n");fclose(_f);} }
    memset(&s_meta, 0, sizeof(s_meta));
    meta_from_filename(path);

    s_fmt = detect_fmt(path);
    if (s_fmt == FMT_UNKNOWN) return (Result)-1;

    {
        const char *_e = strrchr(path, '.');
        if (_e) {
            strncpy(s_meta.format, _e+1, 7);
            s_meta.format[7] = 0;
            for (int i = 0; s_meta.format[i]; i++)
                if (s_meta.format[i] >= 'a' && s_meta.format[i] <= 'z')
                    s_meta.format[i] -= 32;
        } else {
            strcpy(s_meta.format, "???");
        }
    }

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
            if (!s_vorbis) return (Result)-1;
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
            extract_ogg_cover(&c);
            break;
        }
        case FMT_WAV: {
            if (!drwav_init_file(&s_wav, path, NULL)) return (Result)-1;
            s_wav_open    = true;
            s_sample_rate = s_wav.sampleRate;
            ndspChnSetRate(NDSP_CHANNEL, (float)s_wav.sampleRate);
            s_meta.duration_sec = (int)(s_wav.totalPCMFrameCount / s_wav.sampleRate);
            break;
        }
        case FMT_FLAC: {
            s_flac = drflac_open_file(path, NULL);
            if (!s_flac) return (Result)-1;
            s_sample_rate = s_flac->sampleRate;
            ndspChnSetRate(NDSP_CHANNEL, (float)s_flac->sampleRate);
            if (s_flac->sampleRate > 0)
                s_meta.duration_sec = (int)(s_flac->totalPCMFrameCount / s_flac->sampleRate);
            break;
        }
        case FMT_MP3: {
            int err = 0;
            s_mpg = mpg123_new(NULL, &err);
            if (!s_mpg) return (Result)-1;
            mpg123_param(s_mpg, MPG123_FORCE_RATE, SAMPLE_RATE, 0);
            mpg123_param(s_mpg, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO, 0);
            if (mpg123_open(s_mpg, path) != MPG123_OK) {
                mpg123_delete(s_mpg); s_mpg = NULL; return (Result)-1;
            }
            long rate; int ch, enc;
            mpg123_getformat(s_mpg, &rate, &ch, &enc);
            mpg123_format_none(s_mpg);
            mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);
            ndspChnSetRate(NDSP_CHANNEL, (float)rate);
            s_sample_rate = rate;
            off_t len = mpg123_length(s_mpg);
            if (len > 0 && rate > 0) s_meta.duration_sec = (int)(len / rate);
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
            extract_id3v2_cover(path);
            break;
        }
        case FMT_OPUS: {
            int err = 0;
            s_opus = op_open_file(path, &err);
            if (!s_opus) return (Result)-1;
            ndspChnSetRate(NDSP_CHANNEL, 48000.f);
            s_sample_rate = 48000;
            ogg_int64_t len = op_pcm_total(s_opus, -1);
            if (len > 0) s_meta.duration_sec = (int)(len / 48000);
            const OpusTags *tags = op_tags(s_opus, -1);
            if (tags) {
                const char *t = opus_tags_query(tags, "TITLE",  0);
                const char *a = opus_tags_query(tags, "ARTIST", 0);
                const char *b = opus_tags_query(tags, "ALBUM",  0);
                if (t) snprintf(s_meta.title,256,"%s",t);
                if (a) snprintf(s_meta.artist,256,"%s",a);
                if (b) snprintf(s_meta.album,256,"%s",b);
            }
            break;
        }
        default:
            LightLock_Unlock(&s_audio_lock);
            return (Result)-1;
    }
    LightLock_Unlock(&s_audio_lock);
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
    { FILE *_f=fopen("sdmc:/3DSoundShell/debug.log","a");
      if(_f){fprintf(_f,"audio_stop: avant sleep\n");fclose(_f);} }
    svcSleepThread(16000000LL);
    { FILE *_f=fopen("sdmc:/3DSoundShell/debug.log","a");
      if(_f){fprintf(_f,"audio_stop: apres sleep\n");fclose(_f);} }

    if (s_vorbis)   { stb_vorbis_close(s_vorbis); s_vorbis   = NULL; }
    if (s_flac)     { drflac_close(s_flac);        s_flac     = NULL; }
    if (s_wav_open) { drwav_uninit(&s_wav);         s_wav_open = false; }
    if (s_mpg)      { mpg123_close(s_mpg); mpg123_delete(s_mpg); s_mpg = NULL; }
    if (s_opus)     { op_free(s_opus);              s_opus     = NULL; }

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
        case FMT_WAV:  if (s_wav_open) drwav_seek_to_pcm_frame(&s_wav, target);          break;
        case FMT_FLAC: if (s_flac)     drflac_seek_to_pcm_frame(s_flac, target);         break;
        case FMT_MP3:  if (s_mpg)      mpg123_seek(s_mpg, (off_t)target, SEEK_SET);      break;
        case FMT_OPUS: if (s_opus)     op_pcm_seek(s_opus, (ogg_int64_t)target);         break;
        default: break;
    }
    s_samples_played = target;
    ndspChnWaveBufClear(NDSP_CHANNEL);
    s_buf_idx = 0;
}

int   audio_get_position(void)     { return s_sample_rate ? (int)(s_samples_played / s_sample_rate) : 0; }
int   audio_get_duration(void)     { return s_meta.duration_sec; }
float audio_get_position_pct(void) { return s_meta.duration_sec > 0 ? (float)audio_get_position() / s_meta.duration_sec : 0.f; }

void  audio_set_volume(float v)    { s_volume = v < 0.f ? 0.f : (v > 1.f ? 1.f : v); set_mix(s_volume); }
float audio_get_volume(void)       { return s_volume; }
AudioState audio_get_state(void)   { return s_state; }
bool  audio_is_finished(void)      { return s_state == AUDIO_STOPPED && s_meta.duration_sec > 0; }
const AudioMetadata *audio_get_metadata(void) { return &s_meta; }

void audio_get_visualizer(float out[EQ_BANDS])
{
    for (int i = 0; i < EQ_BANDS; i++) out[i] = s_viz[i];
}

/* EQ stubs */
static float s_eq_gains[EQ_BANDS] = {0};
void  audio_eq_set_gain(int b, float db) { if (b >= 0 && b < EQ_BANDS) s_eq_gains[b] = db; }
float audio_eq_get_gain(int b)           { return (b >= 0 && b < EQ_BANDS) ? s_eq_gains[b] : 0.f; }
void  audio_eq_apply_preset(const EQPreset *p) { if (p) for (int i=0;i<EQ_BANDS;i++) s_eq_gains[i]=p->gain[i]; }

EQPreset eq_preset_flat        = {{0,0,0,0,0,0,0,0},       "Flat"};
EQPreset eq_preset_bass_boost  = {{6,5,4,1,0,0,0,0},       "Bass Boost"};
EQPreset eq_preset_vocal       = {{-2,-1,2,4,4,3,1,0},     "Vocal"};
EQPreset eq_preset_rock        = {{4,3,0,-1,0,2,4,5},      "Rock"};
EQPreset eq_preset_classical   = {{3,2,0,0,0,0,2,3},       "Classical"};
EQPreset eq_preset_electronic  = {{5,4,0,-1,1,3,4,5},      "Electronic"};
