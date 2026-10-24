/*
** audio.c - Moteur audio 3DS avec streaming NDSP
*/
#include "audio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <3ds.h>
#include "settings.h"
#include "soundtouch_wrapper.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_CRC /* CLOT6a : le CRC16 par trame n apporte rien ici (donnees deja en local ; le son ne change pas) et cote CPU */
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.h"
/* ND22 : extracteurs de pochette partages (preview.c) */
extern void *nd19_flac_picture(const char *path, uint32_t *imglen);
extern void *nd19_opus_picture(const char *path, uint32_t *imglen);
extern void *nd21_wav_picture(const char *path, uint32_t *imglen);
#include <mpg123.h>
#include <opusfile.h>
#include <opus/opus.h> /* FIX1bz */
extern OpusDecoder *op_decoder(const OggOpusFile *_of, int _stream); /* FIX1bw */
#include "mp4_aac.h"          /* AAC3 : M4A/AAC natif */

#define NDSP_CHANNEL    0
static bool s_finished_naturally = false;
#define SAMPLE_RATE     44100
#define SAMPLES_PER_BUF 4096   /* Taille plus reactive pour visu + SoundTouch */
#define NUM_BUFS        8      /* 8 buffers = marge suffisante */
#define BYTES_PER_SAMPLE 2
#define CHANNELS        2
#define TARGET_RATE     44100  /* Frequence cible pour le resampling */

/* Buffer de resampling statique (evite malloc dans le thread audio) */
static s16 s_resample_buf[SAMPLES_PER_BUF * 2 * 2]; /* *2 ratio max, *2 stereo */

/* Retourne true si la frequence necessite un resampling vers TARGET_RATE */
static bool needs_resample(u32 rate) { return rate != TARGET_RATE; }
static volatile bool s_rs_native = false; /* EQOP2a */

/* Resampling lineaire stereo s16 : in_rate -> TARGET_RATE
   in_buf   = buffer source (stereo s16)
   in_pairs = paires stereo en entree
   out_buf  = buffer destination
   Retourne le nombre de paires en sortie */
/* Position fractionnaire persistante entre les buffers
   DOIT être remise à 0 lors d un nouveau chargement ou seek */
static u32 s_resample_pos_fp = 0;

/* Appelé par audio_stop() et do_seek() pour reset la position */
/* AAC10 : carry + phase persistante pour le resampling (zero perte) */
#define RS_CARRY_MAX 16
static s16 s_rs_carry[RS_CARRY_MAX * 2];
static int s_rs_carry_n = 0;
static u32 s_rs_fp_frac = 0;
static s16 s_rs_tmp[(SAMPLES_PER_BUF * 2 + 8) * 2];
static void resample_reset(void) { s_resample_pos_fp = 0; s_rs_carry_n = 0; s_rs_fp_frac = 0; }

/* AAC10 : combien de paires ENTRER decoder pour obtenir exactement
   max_frames paires en sortie (compte le carry deja disponible) */
static int rs_need(int max_frames, u32 in_rate)
{
    u32 ratio_fp = ((u32)in_rate << 16) / (u32)TARGET_RATE;
    u64 want_fp  = (u64)max_frames * ratio_fp + s_rs_fp_frac;
    int have = s_rs_carry_n, need;
    if (want_fp <= ((u64)have << 16)) return 4;
    need = (int)((want_fp - ((u64)have << 16) + 65535) >> 16) + 2;
    if (need > SAMPLES_PER_BUF * 2) need = SAMPLES_PER_BUF * 2;
    if (need < 4) need = 4;
    return need;
}

/* AAC10 : resampling cubique Catmull-Rom + carry inter-buffers.
   L'ancien (lineaire) : (1) jetait ~3 samples par buffer (fp & 0xFFFF
   perdait le reste entier) = micro-clics ~43x/s + derive 0.3% ;
   (2) adoucissait les aigus (-2.9 dB a 15 kHz = son "pas clair").
   Cubique mesure sur hote : -1.24 dB a 15 kHz, derive nulle, zero clic. */
static int do_resample(const s16 *in_buf, int in_pairs,
                        u32 in_rate, s16 *out_buf)
{
    u32 ratio_fp;
    int tot, out_pairs, i, idx_c, carry_n;
    u64 fp;

    if (!needs_resample(in_rate)) {
        s_rs_carry_n = 0;
        memcpy(out_buf, in_buf, in_pairs * 2 * sizeof(s16));
        return in_pairs;
    }
    ratio_fp = ((u32)in_rate << 16) / (u32)TARGET_RATE;

    tot = s_rs_carry_n + in_pairs;
    if (tot > SAMPLES_PER_BUF * 2 + 8) tot = SAMPLES_PER_BUF * 2 + 8;
    if (s_rs_carry_n > 0)
        memcpy(s_rs_tmp, s_rs_carry, (size_t)s_rs_carry_n * 4);
    if (tot > s_rs_carry_n)
        memcpy(s_rs_tmp + (size_t)s_rs_carry_n * 2, in_buf,
               (size_t)(tot - s_rs_carry_n) * 4);

    fp = s_rs_fp_frac;
    out_pairs = (int)((((u64)tot << 16) - fp) / ratio_fp);
    if (out_pairs < 1) {
        if (tot <= RS_CARRY_MAX) {
            memcpy(s_rs_carry, s_rs_tmp, (size_t)tot * 4);
            s_rs_carry_n = tot;
        }
        return 0;
    }
    if (out_pairs > SAMPLES_PER_BUF) out_pairs = SAMPLES_PER_BUF; /* CLOT8d : garde qui protegere le chunk 4096 */

    for (i = 0; i < out_pairs; i++) {
        int idx  = (int)(fp >> 16);
        int frac = (int)(fp & 0xFFFF);
        int i0, i2, i3, t, t2, t3, c;
        if (idx > tot - 2) idx = tot - 2;
        if (idx < 0) idx = 0;
        i0 = idx - 1; if (i0 < 0) i0 = 0;
        i2 = idx + 1; if (i2 > tot - 1) i2 = tot - 1;
        i3 = idx + 2; if (i3 > tot - 1) i3 = tot - 1;
        t  = frac >> 1;
        t2 = (int)(((long long)t * t) >> 15);
        t3 = (int)(((long long)t2 * t) >> 15);
        for (c = 0; c < 2; c++) {
            int p0 = s_rs_tmp[i0*2+c],  p1 = s_rs_tmp[idx*2+c];
            int p2 = s_rs_tmp[i2*2+c],  p3 = s_rs_tmp[i3*2+c];
            int a  = (p2 - p0) >> 1;
            int b  = (2*p0 - 5*p1 + 4*p2 - p3) >> 1;
            int cc = (-p0 + 3*p1 - 3*p2 + p3) >> 1;
            long long q = (long long)p1 + (((long long)a * t) >> 15)
                        + (((long long)b * t2) >> 15)
                        + (((long long)cc * t3) >> 15);
            out_buf[i*2+c] = (s16)(q > 32767 ? 32767 : (q < -32768 ? -32768 : q));
        }
        fp += ratio_fp;
    }
    s_rs_fp_frac = (u32)(fp & 0xFFFF);
    idx_c   = (int)(fp >> 16);
    carry_n = tot - idx_c;
    if (carry_n < 0) carry_n = 0;
    if (carry_n > RS_CARRY_MAX) carry_n = RS_CARRY_MAX;
    if (carry_n > 0)
        memcpy(s_rs_carry, s_rs_tmp + (size_t)(tot - carry_n) * 2,
               (size_t)carry_n * 4);
    s_rs_carry_n = carry_n;
    return out_pairs;
}

/* Variante legere (interpolation lineaire) de do_resample(), utilisee
   uniquement pour nourrir SoundTouch (voir audio_st_source ci-dessous).
   Le WSOLA re-etire deja completement le signal pour le vitesse/pitch : la
   perte de finesse d'une interpolation lineaire a cet endroit est inaudible,
   alors qu'elle retire une partie du cout mesure (perfdiag) qui faisait
   depasser le budget temps reel sur FLAC/OPUS + EQ + SoundTouch simultanes
   sur Old 3DS. Le chemin de lecture normale (sans effet) garde le cubique
   ci-dessus, inchange. Partage le meme report (s_rs_carry/s_rs_fp_frac) que
   do_resample() : les deux traitent la meme position/donnees, seule la
   formule d'interpolation differe, donc rien n'empeche de passer de l'un a
   l'autre entre deux appels (bascule identity<->SoundTouch). */
static int do_resample_linear(const s16 *in_buf, int in_pairs,
                               u32 in_rate, s16 *out_buf)
{
    u32 ratio_fp;
    int tot, out_pairs, i, idx_c, carry_n;
    u64 fp;

    if (!needs_resample(in_rate)) {
        s_rs_carry_n = 0;
        memcpy(out_buf, in_buf, in_pairs * 2 * sizeof(s16));
        return in_pairs;
    }
    ratio_fp = ((u32)in_rate << 16) / (u32)TARGET_RATE;

    tot = s_rs_carry_n + in_pairs;
    if (tot > SAMPLES_PER_BUF * 2 + 8) tot = SAMPLES_PER_BUF * 2 + 8;
    if (s_rs_carry_n > 0)
        memcpy(s_rs_tmp, s_rs_carry, (size_t)s_rs_carry_n * 4);
    if (tot > s_rs_carry_n)
        memcpy(s_rs_tmp + (size_t)s_rs_carry_n * 2, in_buf,
               (size_t)(tot - s_rs_carry_n) * 4);

    fp = s_rs_fp_frac;
    out_pairs = (int)((((u64)tot << 16) - fp) / ratio_fp);
    if (out_pairs < 1) {
        if (tot <= RS_CARRY_MAX) {
            memcpy(s_rs_carry, s_rs_tmp, (size_t)tot * 4);
            s_rs_carry_n = tot;
        }
        return 0;
    }
    if (out_pairs > SAMPLES_PER_BUF) out_pairs = SAMPLES_PER_BUF;

    for (i = 0; i < out_pairs; i++) {
        int idx  = (int)(fp >> 16);
        int frac = (int)(fp & 0xFFFF);
        int i1, c;
        if (idx > tot - 2) idx = tot - 2;
        if (idx < 0) idx = 0;
        i1 = idx + 1; if (i1 > tot - 1) i1 = tot - 1;
        for (c = 0; c < 2; c++) {
            int p0 = s_rs_tmp[idx*2+c], p1 = s_rs_tmp[i1*2+c];
            long long q = p0 + (((long long)(p1 - p0) * frac) >> 16);
            out_buf[i*2+c] = (s16)(q > 32767 ? 32767 : (q < -32768 ? -32768 : q));
        }
        fp += ratio_fp;
    }
    s_rs_fp_frac = (u32)(fp & 0xFFFF);
    idx_c   = (int)(fp >> 16);
    carry_n = tot - idx_c;
    if (carry_n < 0) carry_n = 0;
    if (carry_n > RS_CARRY_MAX) carry_n = RS_CARRY_MAX;
    if (carry_n > 0)
        memcpy(s_rs_carry, s_rs_tmp + (size_t)(tot - carry_n) * 2,
               (size_t)carry_n * 4);
    s_rs_carry_n = carry_n;
    return out_pairs;
}

typedef enum {
    FMT_UNKNOWN, FMT_OGG, FMT_WAV, FMT_FLAC, FMT_MP3, FMT_OPUS, FMT_M4A   /* AAC3 */
} AudioFormat;

static AudioState        s_state       = AUDIO_STOPPED;
static float             s_volume      = 0.8f;
static float             s_speed       = 1.0f;
static AudioFormat       s_fmt         = FMT_UNKNOWN;
static AudioMetadata     s_meta;
static float             s_viz[EQ_BANDS]     = {0};
static volatile bool     s_viz_forced_off = false; /* V0.96 : force off (mode eco) */



/* V0.97 : Crossfade - buffer PCM precache de la piste suivante */
#define XFADE_CHANNEL    1  /* canal NDSP separe */
#define XFADE_MAX_SEC    10  /* V0.97 : marge pour couvrir chargement piste 2 */
#define XFADE_MAX_SAMPLES (XFADE_MAX_SEC * SAMPLE_RATE)
static s16      *s_xfade_buf = NULL;
static int       s_xfade_total_samples = 0;
static volatile int s_xfade_play_pos = 0;  /* position lecture dans le buffer */
static char      s_xfade_path[512] = {0};
static bool      s_xfade_active = false;
static ndspWaveBuf s_xfade_wbuf;
static LightLock s_xfade_lock;
static bool      s_xfade_lock_init = false;
static Thread    s_xfade_precache_thread = NULL;
static char      s_xfade_precache_pending[512] = {0};
static int       s_xfade_precache_pending_dur = 5;
static volatile bool s_xfade_precache_running = false;

/* V0.97 : Buffer circulaire pour l oscilloscope (256 samples mono) */
static float             s_oscillo_buf[256] = {0};
static volatile int      s_oscillo_idx = 0;
static bool              s_is_new3ds = false;
static volatile bool     s_fft_needed        = false; /* V0.95 opti Old 3DS */
static float             s_viz_fft[EQ_BANDS] = {0};

static ndspWaveBuf       s_wbufs[NUM_BUFS];
/* VIZ17 : snapshot du signal AVANT volume applicatif, par creneau.
   La viz suit l EQ et le tempo, plus le volume : 0% = ca bouge encore,
   300% = les sommets ne sont plus ecremes. Sync gardees : la viz
   lit le creneau EN COURS de lecture, comme avant. */
static s16 s_viz17_buf[NUM_BUFS][SAMPLES_PER_BUF * CHANNELS];
static int s_viz17_n[NUM_BUFS];
static s16              *s_pcm_buf    = NULL;
static int               s_buf_idx    = 0;

static Thread            s_thread     = NULL;
static volatile bool     s_thread_run = false;
static volatile bool     s_seek_requested = false;
static volatile u64      s_seek_target    = 0;
static void do_seek(void); /* forward declaration */

static u64               s_samples_played = 0;
static u32               s_sample_rate    = SAMPLE_RATE;

static stb_vorbis       *s_vorbis    = NULL;
static stb_vorbis_info   s_vinfo;
static drflac           *s_flac      = NULL;
/* Lectures FLAC tamponnees : sur 3DS chaque fread part en IPC vers le
   sysmodule FS, donc le cout est domine par le NOMBRE de lectures, pas par le
   volume. Avec le tampon stdio par defaut (~1 Ko) un FLAC (~1000 kbps) declenche
   des dizaines d allers-retours par creneau audio. Mesure perfdiag : ~75 ms par
   creneau passees dans le decodeur FLAC, contre ~22 ms pour tout le WSOLA.
   On ouvre donc le fichier nous-memes avec un gros tampon, via l API a
   callbacks de dr_flac (drflac_open_file ne laisse pas acceder a son FILE*). */
#define FLAC_IOBUF_SIZE (64 * 1024)
static FILE  *s_flac_fp = NULL;
static char   s_flac_iobuf[FLAC_IOBUF_SIZE];

static size_t flac_on_read(void *ud, void *out, size_t n)
{ return fread(out, 1, n, (FILE*)ud); }

static drflac_bool32 flac_on_seek(void *ud, int offset, drflac_seek_origin origin)
{
    int whence = SEEK_SET;
    if      (origin == DRFLAC_SEEK_CUR) whence = SEEK_CUR;
    else if (origin == DRFLAC_SEEK_END) whence = SEEK_END;
    return fseek((FILE*)ud, offset, whence) == 0;
}

static drflac_bool32 flac_on_tell(void *ud, drflac_int64 *cursor)
{ *cursor = (drflac_int64)ftell((FILE*)ud); return DRFLAC_TRUE; }

/* Ferme le flux FLAC ET le FILE* associe : avec l API a callbacks, dr_flac ne
   possede pas le fichier et ne le fermera pas lui-meme. */
static void flac_close_stream(void)
{
    if (s_flac)    { drflac_close(s_flac); s_flac = NULL; }
    if (s_flac_fp) { fclose(s_flac_fp);    s_flac_fp = NULL; }
}

/* Meme traitement pour le WAV (plus gros debit de tous : 1411 kbps) et pour
   l OGG, tous deux ouverts jusqu ici avec le tampon stdio par defaut. */
#define WAV_IOBUF_SIZE (64 * 1024)
static FILE *s_wav_fp = NULL;
static char  s_wav_iobuf[WAV_IOBUF_SIZE];

static size_t wav_on_read(void *ud, void *out, size_t n)
{ return fread(out, 1, n, (FILE*)ud); }

static drwav_bool32 wav_on_seek(void *ud, int offset, drwav_seek_origin origin)
{
    int whence = SEEK_SET;
    if      (origin == DRWAV_SEEK_CUR) whence = SEEK_CUR;
    else if (origin == DRWAV_SEEK_END) whence = SEEK_END;
    return fseek((FILE*)ud, offset, whence) == 0;
}

static drwav_bool32 wav_on_tell(void *ud, drwav_int64 *cursor)
{ *cursor = (drwav_int64)ftell((FILE*)ud); return DRWAV_TRUE; }

#define OGG_IOBUF_SIZE (64 * 1024)
static char s_ogg_iobuf[OGG_IOBUF_SIZE];

/* mpg123 fait ses propres lectures (il ne passe pas par dr_*) et lit par tres
   petits morceaux (en-tete de trame, puis corps), donc il cumulait le meme
   surcout d IPC. On lui fournit notre propre FILE* tamponne. */
#define MP3_IOBUF_SIZE (64 * 1024)
static FILE *s_mp3_fp = NULL;
static char  s_mp3_iobuf[MP3_IOBUF_SIZE];

/* Streaming progressif : le fichier grandit encore pendant qu on le lit. */
static volatile bool s_stream_growing = false;
void audio_stream_begin(void) { s_stream_growing = true; }
void audio_stream_end(void)   { s_stream_growing = false; }
bool audio_stream_active(void){ return s_stream_growing; }

/* Source reseau (Navidrome) en cours : EQ et SoundTouch (vitesse/pitch) sont
   forces neutres, quels que soient les reglages de l utilisateur. Ce budget a
   ete mesure au plus juste pour un flux nu (voir la session de profilage) ;
   ajouter le cout du WSOLA + de l EQ par-dessus repasserait au-dessus du
   budget temps reel, exactement comme pour la bibliotheque locale avant
   correction. Sert aussi a garder les deux lecteurs (local / Navidrome)
   clairement separes : jamais d effet applique a un flux reseau. */
static volatile bool s_no_dsp = false;
void audio_set_network_source(bool on) { s_no_dsp = on; }
bool audio_is_network_source(void)     { return s_no_dsp; }
#define STREAM_WAIT_STEP_NS  20000000LL /* 20 ms entre deux essais */
#define STREAM_WAIT_MAX_MS   8000       /* abandon si plus rien pendant 8 s */

static mpg123_ssize_t mp3_r_read(void *h, void *buf, size_t count)
{
    FILE *f = (FILE*)h;
    size_t got = fread(buf, 1, count, f);
    if (got == count || !s_stream_growing) return (mpg123_ssize_t)got;

    /* On a touche la fin d un fichier encore en telechargement : attendre la
       suite plutot que de signaler une fin de piste. clearerr() est
       indispensable, sinon le drapeau EOF de stdio fait echouer tous les
       fread suivants meme une fois les octets arrives. */
    char  *p    = (char*)buf + got;
    size_t need = count - got;
    int    idle = 0;
    while (need > 0 && s_stream_growing && idle < STREAM_WAIT_MAX_MS) {
        clearerr(f);
        svcSleepThread(STREAM_WAIT_STEP_NS);
        idle += 20;
        size_t n = fread(p, 1, need, f);
        if (n > 0) { p += n; need -= n; got += n; idle = 0; }
    }
    return (mpg123_ssize_t)got;
}

static off_t mp3_r_lseek(void *h, off_t offset, int whence)
{
    if (fseek((FILE*)h, (long)offset, whence) != 0) return (off_t)-1;
    return (off_t)ftell((FILE*)h);
}

static void mp3_r_cleanup(void *h) { (void)h; } /* on ferme nous-memes */
static drwav             s_wav;
static bool              s_wav_open  = false;
static mpg123_handle    *s_mpg       = NULL;
static OggOpusFile      *s_opus      = NULL;
static MP4AACDecoder    *s_aac       = NULL;   /* AAC3 */

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
            /* S7: scan magic bytes JPEG/PNG (robuste) */
            u32 img_start = 0;
            for (u32 i = 0; i + 3 < fsz; i++) {
                if (data[i] == 0xFF && data[i+1] == 0xD8 && data[i+2] == 0xFF) {
                    img_start = i; break;
                }
                if (data[i] == 0x89 && data[i+1] == 0x50 && data[i+2] == 0x4E && data[i+3] == 0x47) {
                    img_start = i; break;
                }
            }
            if (img_start > 0 && img_start < fsz) {
                u32 isz = fsz - img_start;
                s_meta.cover_data = malloc(isz);
                if (s_meta.cover_data) {
                    memcpy(s_meta.cover_data, data + img_start, isz);
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
    if (!strcasecmp(e,"wav"))  return FMT_WAV;
    /* AIFF non supporte par dr_wav sur 3DS → ignorer */
    /* AAC non supporte par mpg123 sans plugin → ignorer */
    if (!strcasecmp(e,"mp3") || !strcasecmp(e,"mp2")) return FMT_MP3;
    if (!strcasecmp(e,"opus"))                          return FMT_OPUS;
    /* AAC3 : M4A/MP4/AAC natif (helix + minimp4) */
    if (!strcasecmp(e,"m4a") || !strcasecmp(e,"mp4") ||
        !strcasecmp(e,"aac"))                             return FMT_M4A;
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
    mix[0] = mix[1] = vol > 1.f ? 1.f : vol;   /* VOLBOOST : mix NDSP jamais >1 */
    ndspChnSetMix(NDSP_CHANNEL, mix);
}

/* ── Decode one chunk ───────────────────────────────────────── */
/* ND21 : report de fin de frame vorbis (anti-lag OGG) */
#define OGG_CARRY_CAP 8192
static s16 s_ogg_carry[OGG_CARRY_CAP*2];
static int s_ogg_carry_n = 0;
/* CLOT8f : le creneau fait 4096 paires, la trame MP3 1152 -> 4096 = 3 x 1152 + 640, le reste doit etre reporte */
#define MP3_CARRY_CAP (2304*2)
static s16 s_mp3_carry[MP3_CARRY_CAP];
static int s_mp3_carry_n = 0, s_mp3_carry_off = 0;
static int s_ogg_carry_off = 0;
static int decode_chunk(s16 *out, int max_pairs)
{
    /* VIZ5 : tranche max 2048 paires (46ms) par appel. Avant : WAV/FLAC/
       OGG/M4A remplissaient 93-186ms en 1 seul appel -> la viz etait
       rafraichie ~5x/sec sur ces formats (19x/sec sur MP3 dont les
       frames font 1152). Son strictement identique, juste plus regulier.
       MP3 et OPUS : aucun changement de comportement. */
    /* CLOT8a : plafond VIZ5 retire - le MP3 remplissait deja son creneau en une passe (CLOT5a), FLAC/OGG/WAV/M4A le font aussi */
    switch (s_fmt) {
        case FMT_OGG: {
            if (!s_vorbis) return 0;
            int got = 0;
            /* ND21 : servir d'abord le report du buffer precedent */
            while (got < max_pairs && s_ogg_carry_off < s_ogg_carry_n) {
                out[got*2]   = s_ogg_carry[s_ogg_carry_off*2];
                out[got*2+1] = s_ogg_carry[s_ogg_carry_off*2+1];
                s_ogg_carry_off++; got++;
            }
            while (got < max_pairs) {
                float **pcm;
                int n = stb_vorbis_get_frame_float(s_vorbis, NULL, &pcm);
                int ch, i;
                if (n == 0) return got;
                ch = s_vinfo.channels;
                for (i = 0; i < n && got < max_pairs; i++, got++) {
                    float l = pcm[0][i];
                    float r = (ch > 1) ? pcm[1][i] : l;
                    l = l >  1.f ?  1.f : (l < -1.f ? -1.f : l);
                    r = r >  1.f ?  1.f : (r < -1.f ? -1.f : r);
                    out[got*2]   = (s16)(l * 32767.f);
                    out[got*2+1] = (s16)(r * 32767.f);
                }
                if (i < n) {
                    /* ND21 : fin de frame -> report au lieu de jeter */
                    int rem = n - i, j;
                    if (rem > OGG_CARRY_CAP) rem = OGG_CARRY_CAP;
                    for (j = 0; j < rem; j++) {
                        float l = pcm[0][i+j];
                        float r = (ch > 1) ? pcm[1][i+j] : l;
                        l = l >  1.f ?  1.f : (l < -1.f ? -1.f : l);
                        r = r >  1.f ?  1.f : (r < -1.f ? -1.f : r);
                        s_ogg_carry[j*2]   = (s16)(l * 32767.f);
                        s_ogg_carry[j*2+1] = (s16)(r * 32767.f);
                    }
                    s_ogg_carry_n = rem; s_ogg_carry_off = 0;
                    return got;
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
            int total = 0; /* CLOT8f : remplir le creneau par trames entieres, jamais de trame coupee */
            while (total < max_pairs) {
            while (total < max_pairs && s_mp3_carry_off < s_mp3_carry_n) {
                out[total*2]   = s_mp3_carry[s_mp3_carry_off*2];
                out[total*2+1] = s_mp3_carry[s_mp3_carry_off*2+1];
                s_mp3_carry_off++; total++;
            }
            if (s_mp3_carry_off >= s_mp3_carry_n) { s_mp3_carry_n = 0; s_mp3_carry_off = 0; }
            if (total >= max_pairs) break;
            int err = mpg123_decode_frame(s_mpg, &num, &audio, &bytes);
            if (err == MPG123_DONE || bytes == 0) break;
            int pairs = (int)(bytes / (2 * CHANNELS));
            if (pairs > max_pairs - total) pairs = max_pairs - total;
            memcpy(out + total * CHANNELS, audio, pairs * CHANNELS * 2);
            total += pairs;
            int rem = (int)(bytes / (2 * CHANNELS)) - pairs;
            if (rem > 0) {
                if (rem > MP3_CARRY_CAP / 2) rem = MP3_CARRY_CAP / 2;
                memcpy(s_mp3_carry, audio + pairs * CHANNELS * 2, (size_t)rem * CHANNELS * 2);
                s_mp3_carry_n = rem; s_mp3_carry_off = 0;
            }
            }
            return total;
        }
        case FMT_OPUS: {
            if (!s_opus) return 0;
            int n = op_read_stereo(s_opus, out, max_pairs * 2);
            return n <= 0 ? 0 : n;
        }
        case FMT_M4A: {
            if (!s_aac) return 0;
            return mp4_aac_read_pcm16(s_aac, out, max_pairs);
        }
        default: return 0;
    }
}

/* ── EQ Biquad ──────────────────────────────────────────────── */
static float s_eq_gains[EQ_BANDS] = {0};

static const float EQ_FREQS[EQ_BANDS] = {
    32.f, 64.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 8000.f
};

typedef struct {
    float b0, b1, b2, a1, a2;
    float x1L, x2L, y1L, y2L;
    float x1R, x2R, y1R, y2R;
} BiquadFilter;

static BiquadFilter s_filters[EQ_BANDS];
static float        s_last_gains[EQ_BANDS] = {0};

static void biquad_compute(BiquadFilter *f, float freq, float gain_db, float sr)
{
    if (sr <= 0.f) sr = 44100.f;
    float A  = powf(10.f, gain_db / 40.f);
    float w0 = 2.f * M_PI * freq / sr;
    float Q;
    if      (freq <   100.f) Q = 0.8f;
    else if (freq <   300.f) Q = 1.0f;
    else if (freq <  1000.f) Q = 1.2f;
    else if (freq <  4000.f) Q = 1.5f;
    else                     Q = 2.0f;
    float alpha = sinf(w0) / (2.f * Q);
    float b0 =  1.f + alpha * A;
    float b1 = -2.f * cosf(w0);
    float b2 =  1.f - alpha * A;
    float a0 =  1.f + alpha / A;
    float a1 = -2.f * cosf(w0);
    float a2 =  1.f - alpha / A;
    f->b0 = b0/a0; f->b1 = b1/a0; f->b2 = b2/a0;
    f->a1 = a1/a0; f->a2 = a2/a0;
}

static void eq_update_filters(float sr)
{
    for (int b = 0; b < EQ_BANDS; b++) {
        if (s_eq_gains[b] != s_last_gains[b]) {
            biquad_compute(&s_filters[b], EQ_FREQS[b], s_eq_gains[b], sr);
            s_filters[b].x1L = s_filters[b].x2L = 0.f;
            s_filters[b].y1L = s_filters[b].y2L = 0.f;
            s_filters[b].x1R = s_filters[b].x2R = 0.f;
            s_filters[b].y1R = s_filters[b].y2R = 0.f;
            s_last_gains[b] = s_eq_gains[b];
        }
    }
}

/* Cache l etat EQ actif pour eviter de checker les 8 bandes a chaque appel */
static volatile bool s_eq_active_cache = false;
/* FIX2b : EQ par bande - coefficients et etat gardes dans les registres,
   une passe complete du buffer par bande active. Meme suite d operations
   que apply_eq (ordre des bandes puis des echantillons conserve, memes
   arretes de saturation) : rendu BIT-A-BIT identique, juste moins de
   va-et-vient memoire par echantillon (le FPU du device na que 16
   registres et 5 bandes x 13 flottants ne tiennent pas dedans). */
static void eq_bandpass(s16 *buf, int pairs)
{
    if (!s_eq_active_cache) return;
    eq_update_filters((float)s_sample_rate);
    int order[EQ_BANDS]; int nb = 0;
    for (int b = 0; b < EQ_BANDS; b++)
        if (s_eq_gains[b] != 0.f) order[nb++] = b;
    if (!nb) return;
    for (int o = 0; o < nb; o++) {
        BiquadFilter *f = &s_filters[order[o]];
        float cb0 = f->b0, cb1 = f->b1, cb2 = f->b2, ca1 = f->a1, ca2 = f->a2;
        float x1L = f->x1L, x2L = f->x2L, y1L = f->y1L, y2L = f->y2L;
        float x1R = f->x1R, x2R = f->x2R, y1R = f->y1R, y2R = f->y2R;
        for (int i = 0; i < pairs; i++) {
            float xL = (float)buf[i*2];
            float yL = cb0*xL + cb1*x1L + cb2*x2L - ca1*y1L - ca2*y2L;
            /* Anti-denormal : sur silence/fondu, yL decroit en douceur vers 0 et
               finit par entrer en plage "denormal" (< ~1.2e-38). Le VFP de
               l'ARM11 (3DS) n'a pas de flush-to-zero materiel : chaque operation
               sur un flottant denormal part alors sur un chemin logiciel lent,
               ce qui double+ le cout de l'EQ apres quelques secondes de lecture
               (mesure : ~11.7ms -> ~20.6ms/buffer). Ecraser a 0 sous ce seuil
               est inaudible (bien sous le bruit de fond du s16) et coupe court. */
            if (yL > -1e-15f && yL < 1e-15f) yL = 0.f;
            x2L = x1L; x1L = xL; y2L = y1L; y1L = yL;
            buf[i*2] = (s16)(yL > 32767.f ? 32767.f : (yL < -32768.f ? -32768.f : yL));
            float xR = (float)buf[i*2+1];
            float yR = cb0*xR + cb1*x1R + cb2*x2R - ca1*y1R - ca2*y2R;
            if (yR > -1e-15f && yR < 1e-15f) yR = 0.f;
            x2R = x1R; x1R = xR; y2R = y1R; y1R = yR;
            buf[i*2+1] = (s16)(yR > 32767.f ? 32767.f : (yR < -32768.f ? -32768.f : yR));
        }
        f->x1L = x1L; f->x2L = x2L; f->y1L = y1L; f->y2L = y2L;
        f->x1R = x1R; f->x2R = x2R; f->y1R = y1R; f->y2R = y2R;
    }
}

static void apply_eq(s16 *buf, int pairs)
{
    eq_bandpass(buf, pairs); return; /* FIX2a */
    /* EQOP1b : passe unique par echantillon (localite cache L1) - le resultat
       est BIT-A-BIT identique a la double boucle d avant (memes operations,
       meme ordre de filtres par echantillon), juste 1 balayage du buffer au
       lieu de 6. Bandes a plat toujours ignorees. */
    if (!s_eq_active_cache) return;
    eq_update_filters((float)s_sample_rate);
    int order[EQ_BANDS]; int nb = 0;
    for (int b = 0; b < EQ_BANDS; b++)
        if (s_eq_gains[b] != 0.f) order[nb++] = b;
    if (!nb) return;
    for (int i = 0; i < pairs; i++) {
        for (int o = 0; o < nb; o++) {
            BiquadFilter *f = &s_filters[order[o]];
            float xL = (float)buf[i*2];
            float yL = f->b0*xL + f->b1*f->x1L + f->b2*f->x2L
                                 - f->a1*f->y1L - f->a2*f->y2L;
            f->x2L = f->x1L; f->x1L = xL;
            f->y2L = f->y1L; f->y1L = yL;
            buf[i*2] = (s16)(yL > 32767.f ? 32767.f : (yL < -32768.f ? -32768.f : yL));
            float xR = (float)buf[i*2+1];
            float yR = f->b0*xR + f->b1*f->x1R + f->b2*f->x2R
                                 - f->a1*f->y1R - f->a2*f->y2R;
            f->x2R = f->x1R; f->x1R = xR;
            f->y2R = f->y1R; f->y1R = yR;
            buf[i*2+1] = (s16)(yR > 32767.f ? 32767.f : (yR < -32768.f ? -32768.f : yR));
        }
    }
}

/* ── FFT Visualiseur ────────────────────────────────────────── */
#define FFT_SIZE 256

static float s_fft_re[FFT_SIZE];
static float s_fft_im[FFT_SIZE];
static float s_hann[FFT_SIZE];
static bool  s_hann_init = false;

static void fft_init_hann(void)
{
    for (int i = 0; i < FFT_SIZE; i++)
        s_hann[i] = 0.5f * (1.f - cosf(2.f * M_PI * i / (FFT_SIZE - 1)));
    s_hann_init = true;
}

static void fft_compute(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.f * M_PI / len;
        float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.f, ci = 0.f;
            for (int j = 0; j < len/2; j++) {
                float ur = re[i+j], ui = im[i+j];
                float vr = re[i+j+len/2]*cr - im[i+j+len/2]*ci;
                float vi = re[i+j+len/2]*ci + im[i+j+len/2]*cr;
                re[i+j]       = ur + vr; im[i+j]       = ui + vi;
                re[i+j+len/2] = ur - vr; im[i+j+len/2] = ui - vi;
                float ncr = cr*wr - ci*wi;
                ci = cr*wi + ci*wr; cr = ncr;
            }
        }
    }
}

/* VIZ7 : compteur diag, a supprimer quand le bug est compris */
volatile int g_viz7_updates = 0;

/* VIZ11 : mesure de cadence viz (a retirer a la cloture) */
volatile int g_viz11_maxgap = 0;
int audio_viz11_gap(void) { int v11 = g_viz11_maxgap; g_viz11_maxgap = 0; return v11; }
/* EQOP1a : statiques du meteur lisseur (table des alphas + constante log) */
static float s_v11_tbl[33][4];
static bool  s_v11_tbl_ready = false;
static const float s_log301_inv = 1.f / 5.707110f; /* 1/ln(301) replie a la compilation */
static void update_viz(const s16 *buf, int pairs)
{
    g_viz7_updates++; /* VIZ7 */
    /* VIZ11 : lissage viz sur temps reel + mesure du pire ecart */
    /* EQOP1c : alphas de lissage par table - plus de powf ni de double par tick */
    static u64 s_v11_prevt = 0;
    u64 v11_t = svcGetSystemTick();
    int v11_idx = 0;
    if (s_v11_prevt) {
        long long v11_gap_ms = (long long)((v11_t - s_v11_prevt) / 268112ULL);
        if ((int)v11_gap_ms > g_viz11_maxgap) g_viz11_maxgap = (int)v11_gap_ms;
        v11_idx = (int)v11_gap_ms;
        if (v11_idx < 1) v11_idx = 1;
        if (v11_idx > 32) v11_idx = 32;
    }
    s_v11_prevt = v11_t;
    if (!s_v11_tbl_ready) {
        for (int i = 0; i <= 32; i++) {
            float k = (float)i * 0.04f;
            if (k < 0.05f) k = 0.05f;
            if (k > 12.f) k = 12.f;
            s_v11_tbl[i][0] = 1.f - powf(0.4f, k);
            s_v11_tbl[i][1] = 1.f - powf(0.96f, k);
            s_v11_tbl[i][2] = 1.f - powf(0.6f, k);
            s_v11_tbl[i][3] = 1.f - powf(0.94f, k);
        }
        if (v11_idx == 0) {
            s_v11_tbl[0][0] = 1.f - powf(0.4f, 1.0f);
            s_v11_tbl[0][1] = 1.f - powf(0.96f, 1.0f);
            s_v11_tbl[0][2] = 1.f - powf(0.6f, 1.0f);
            s_v11_tbl[0][3] = 1.f - powf(0.94f, 1.0f);
        }
        s_v11_tbl_ready = true;
    }
    float v11_fr = s_v11_tbl[v11_idx][0];
    float v11_ff = s_v11_tbl[v11_idx][1];
    float v11_rr = s_v11_tbl[v11_idx][2];
    float v11_rf = s_v11_tbl[v11_idx][3];
    if (!s_hann_init) fft_init_hann();
    int n = pairs < FFT_SIZE ? pairs : FFT_SIZE;


    /* FFT pour VIZ_EQ - OPTI: skip si pas demandee (economie ~15% CPU Old 3DS) */
    if (!s_fft_needed) goto skip_fft;

    for (int i = 0; i < n; i++) {
        float l = buf[i*2]   / 32767.f;
        float r = buf[i*2+1] / 32767.f;
        s_fft_re[i] = ((l + r) * 0.5f) * s_hann[i];
        s_fft_im[i] = 0.f;
    }
    for (int i = n; i < FFT_SIZE; i++) { s_fft_re[i] = 0.f; s_fft_im[i] = 0.f; }
    fft_compute(s_fft_re, s_fft_im, FFT_SIZE);

    /* Bandes: limites en Hz recalculees correctement
       bin = freq * FFT_SIZE / sample_rate
       sr=44100, FFT=512: 1bin = 86Hz
       Limites: 0 45 90 180 375 750 3500 8000 11025 Hz */
    static const int BSTART[EQ_BANDS] = { 0,  0,  1,  1,  2,  4, 20, 46};
    static const int BEND  [EQ_BANDS] = { 0,  1,  1,  2,  4, 20, 46, 63};

    /* Boost par bande pour equilibrer (les aigus ont plus de bins = energie diluee) */
    static const float BOOST[EQ_BANDS] = {1.3f, 1.2f, 1.5f, 1.6f, 1.5f, 4.f, 6.6f, 9.3f};

    for (int b = 0; b < EQ_BANDS; b++) {
        int bs = BSTART[b], be = BEND[b];
        if (be >= FFT_SIZE/2) be = FFT_SIZE/2 - 1;
        if (be <= bs) be = bs + 1;
        float energy = 0.f; int count = 0;
        for (int k = bs; k <= be; k++) {
            float mag = s_fft_re[k]*s_fft_re[k] + s_fft_im[k]*s_fft_im[k];
            energy += sqrtf(mag); count++;
        }
        if (count > 0) energy /= count;
        energy /= (FFT_SIZE * 0.5f);
        energy *= BOOST[b];
        if (energy > 0.f)
            energy = logf(1.f + energy * 300.f)  * s_log301_inv; /* EQOP1d */
        if (energy > 1.f) energy = 1.f;
        float spd = (energy > s_viz_fft[b]) ? v11_fr : v11_ff; /* VIZ11 */
        s_viz_fft[b] += (energy - s_viz_fft[b]) * spd;
    }

    skip_fft:
    /* RMS pour Barres/Onde/Cercle */
    int bs2 = n / EQ_BANDS;
    if (bs2 < 1) bs2 = 1;
    for (int b = 0; b < EQ_BANDS; b++) {
        float rms = 0.f;
        int s2 = b * bs2, e2 = s2 + bs2;
        if (e2 > n) e2 = n;
        for (int i = s2; i < e2; i++) {
            float l = buf[i*2] / 32767.f, r = buf[i*2+1] / 32767.f;
            float m = (l + r) * 0.5f; rms += m * m;
        }
        rms = sqrtf(rms / (e2 - s2));
        float spd = (rms > s_viz[b]) ? v11_rr : v11_rf; /* VIZ11 */
        s_viz[b] += (rms - s_viz[b]) * spd;
    }
}

/* ── Audio thread ───────────────────────────────────────────── */
int g_nd24_wanted_core = -2; /* CLOT10d : coeur demande a la creation */
int g_nd24_real_core = -2;   /* CLOT10e : coeur ou le thread a vraiment atterri */
static LightLock s_audio_lock;
static bool      s_lock_ready = false;

static void audio_lock_init(void)
{
    if (!s_lock_ready) { LightLock_Init(&s_audio_lock); s_lock_ready = true; }
}

static int audio_st_source(int16_t *buf, int max_frames, void *ctx)
{
    (void)ctx;
    int got = 0;
    if (max_frames <= 0) return 0;

    LightLock_Lock(&s_audio_lock);
    if (needs_resample(s_sample_rate)) {
        int   need  = rs_need(max_frames, s_sample_rate);
        if (need > 4096) need = 4096; /* CLOT8b */
        if (need > SAMPLES_PER_BUF * 2) need = SAMPLES_PER_BUF * 2;
        int raw = decode_chunk(s_resample_buf, need);
        if (raw > 0) {
            /* Interpolation lineaire ici (pas cubique) : ce flux alimente
               SoundTouch, voir do_resample_linear() pour le pourquoi. */
            got = do_resample_linear(s_resample_buf, raw, s_sample_rate, buf);
            s_samples_played += (u64)raw;
        }
    } else {
        got = decode_chunk(buf, max_frames);
        if (got > 0) s_samples_played += (u64)got;
    }
    LightLock_Unlock(&s_audio_lock);
    return got;
}

static bool s_nd24_viz_pass_ok = true; /* CLOT9a : le rendu 3D du haut tourne deux fois par image (gauche puis droit) */
void audio_viz_frame_begin(void) { s_nd24_viz_pass_ok = true; } /* CLOT9b : rappele une fois par image par draw_frame */
int g_nd24_viz_frames = 0; /* CLOT9c : nb de vrais travaux de viz (diagnostic) */
static void audio_thread(void *arg)
{
    (void)arg;
    /* Compteur pour calculer la viz 1 buffer sur 3 seulement
       Le visualiseur est rafraichi ~20 fois/sec ce qui est largement suffisant
       et evite de surcharger le CPU avec la FFT a chaque buffer */
    g_nd24_real_core = (int)svcGetProcessorID(); /* CLOT10f */
    while (s_thread_run) {
        if (s_state != AUDIO_PLAYING) { svcSleepThread(16000000LL); continue; }
        ndspWaveBuf *wb = &s_wbufs[s_buf_idx];
        if (wb->status == NDSP_WBUF_QUEUED || wb->status == NDSP_WBUF_PLAYING) {
            /* Sleep court pour 48000Hz (resampling plus lourd)
               Sleep plus long pour 44100Hz (natif, moins de CPU) */
            if (needs_resample(s_sample_rate) && s_rs_native == false) /* EQOP2h */
                svcSleepThread(3000000LL);  /* CLOT3d : 3ms = rythme V0.98 */
            else
                svcSleepThread(8000000LL);  /* 8ms pour 44100Hz natif */
            continue;
        }
        if (s_seek_requested) do_seek();
        int pairs = 0;
        s16 *pcm = s_pcm_buf + s_buf_idx * SAMPLES_PER_BUF * CHANNELS;
        /* V0.98 : Synchroniser les settings avec SoundTouch */
        {
            static float last_pro_speed = -1.f;
            static int   last_pitch_st  = -999;
            float cur_speed = (g_settings.pro_speed > 0.01f) ? g_settings.pro_speed : 1.0f;
            int   cur_pitch = g_settings.pitch_semitones;
            if (cur_speed != last_pro_speed) {
                st_set_speed(cur_speed);
                last_pro_speed = cur_speed;
            }
            if (cur_pitch != last_pitch_st) {
                st_set_pitch(powf(2.f, (float)cur_pitch / 12.f));
                last_pitch_st = cur_pitch;
            }
        }

        /* V0.98 : Bypass complet si identity pour economiser CPU/batterie.
           Si on entre/sort du mode SoundTouch en live, on flush une fois
           pour eviter les restes de pipeline. */
        {
            static bool last_identity = true;
            /* Source reseau : identity FORCEE (jamais de WSOLA), quels que
               soient g_settings.pro_speed/pitch_semitones. */
            bool cur_identity = s_no_dsp ? true : st_is_identity();

            if (cur_identity != last_identity) {
                st_flush();
                last_identity = cur_identity;
                if (s_rs_native) {
                    float sp = (g_settings.pro_speed > 0.01f) ? g_settings.pro_speed : 1.0f;
                    float pr = (s_is_new3ds && g_settings.pitch_semitones != 0) ? powf(2.f, (float)g_settings.pitch_semitones / 12.f) : 1.0f;
                    ndspChnSetRate(NDSP_CHANNEL, (cur_identity ? 48000.f : (float)TARGET_RATE) * sp * pr); /* EQOP2e */
                }
            }

            if (cur_identity) {
                LightLock_Lock(&s_audio_lock);
                if (needs_resample(s_sample_rate) && s_rs_native == false) { /* EQOP2f */
                    int   need  = rs_need(SAMPLES_PER_BUF, s_sample_rate);
                    if (need > SAMPLES_PER_BUF * 2) need = SAMPLES_PER_BUF * 2;
                    int raw = decode_chunk(s_resample_buf, need);
                    if (raw <= 0) {
                        LightLock_Unlock(&s_audio_lock);
                        s_state = AUDIO_STOPPED; continue;
                    }
                    pairs = do_resample(s_resample_buf, raw, s_sample_rate, pcm);
                    s_samples_played += (u64)raw;
                } else {
                    pairs = decode_chunk(pcm, SAMPLES_PER_BUF);
                    if (pairs > 0) s_samples_played += (u64)pairs;
                }
                LightLock_Unlock(&s_audio_lock);
            } else {
                pairs = st_fill_buffer(pcm, SAMPLES_PER_BUF);
            }
        }

        if (pairs <= 0) {
            /* Vidange materielle NDSP : attend que les derniers tampons deja
               en file (au plus NUM_BUFS=8, ~93ms chacun a 44100Hz = ~750ms
               au pire) finissent de jouer, pour ne pas couper la toute fin
               du morceau quand l auto-avance charge le suivant. Le budget
               precedent (40 x 50ms = 2s, jusqu a 2,7x le pire cas reel)
               bloquait le THREAD AUDIO lui-meme pendant tout ce temps a
               chaque fin de piste naturelle : plus aucun traitement audio
               possible, et le shutdown (s_thread_run) n etait revu qu une
               fois la vidange finie. Reduit au pire cas reel + marge, et
               rendu interruptible par l arret de l app. */
            int drain = 0;
            while (s_thread_run && drain < 16 && ndspChnIsPlaying(NDSP_CHANNEL)) {
                svcSleepThread(50000000ULL);
                drain++;
            }
            s_finished_naturally = true; /* FIN RÉELLE DU FICHIER AUDIO LOCAL ! */
            s_state = AUDIO_STOPPED;
            continue;
        }

        if (!s_no_dsp) apply_eq(pcm, pairs);
        /* VIZ16a/VIZ14a : le remplissage oscillo et le throttling viz ont ete
           deplaces dans audio_viz_tick() (appele par le thread UI) */
        memcpy(s_viz17_buf[s_buf_idx], pcm, pairs * CHANNELS * BYTES_PER_SAMPLE); /* VIZ17a */
        s_viz17_n[s_buf_idx] = pairs;
        if (s_volume != 1.0f) {
            /* VOLBOOST4 : gain reel = x4 par +100% au-dela de 100%
               (150% => x3, 200% => x5, 300% => x9) : saturation
               franche voulue, clip s16 propre en bout de chaine */
            float ge = s_volume <= 1.f ? s_volume
                                       : 1.f + (s_volume - 1.f) * 4.f;
            for (int i = 0; i < pairs * CHANNELS; i++) {
                float sv = pcm[i] * ge;
                pcm[i] = (s16)(sv > 32767.f ? 32767.f : (sv < -32768.f ? -32768.f : sv));
            }
        }
        DSP_FlushDataCache(pcm, pairs * CHANNELS * BYTES_PER_SAMPLE);
        /* Reset complet du wavebuf pour eviter valeurs residuelles */
        memset(wb, 0, sizeof(ndspWaveBuf));
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
    if (R_FAILED(rc)) { if (rc != (Result)0xD880A7FA) return rc; }
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
    /* V0.98 : Init SoundTouch pour pitch/speed separe */
    st_init(SAMPLE_RATE, CHANNELS);
    {
        bool isNew = false;
        APT_CheckNew3DS(&isNew);
        s_is_new3ds = isNew;
    }
    st_set_lowcpu(!s_is_new3ds); /* Old 3DS : WSOLA en mode economique */
    st_set_source(audio_st_source, NULL);

    /* Coeurs : Old 3DS = 2 coeurs ARM11 (0 = application, 1 = sysmodules),
       New 3DS = 4 (le coeur 2 est libre pour l application).
       Mesure perfdiag : avec EQ + SoundTouch le thread audio mange 87-97% du
       coeur 0, donc l interface (qui rend sur ce meme coeur) n a plus rien et
       tombe a quelques images/seconde. On place donc le thread audio sur le
       coeur 1, apres avoir demande sa quote de temps au systeme : l interface
       garde le coeur 0 pour elle. Le coeur 1 sert aussi aux sysmodules (FS /
       carte SD), d ou une quote et pas la totalite. */
    int nd24_proc = s_is_new3ds ? 2 : 1; /* s_is_new3ds : detecte juste au-dessus, pas de dependance a l ordre d init de main.c */
    APT_SetAppCpuTimeLimit(80);
    s_thread = threadCreate(audio_thread, NULL, 32*1024, 0x19, nd24_proc, false);
    if (!s_thread) { /* CLOT10c : noyau refractaire -> retour strict V0.98 (coeur de l exheader) */
        nd24_proc = -2;
        APT_SetAppCpuTimeLimit(30);
        s_thread = threadCreate(audio_thread, NULL, 32*1024, 0x19, -2, false);
    }
    if (!s_thread) { /* CLOT10d2 : filet INCONDITIONNEL (lecon FIX3 : jamais de thread manquant) */
        nd24_proc = -2;
        s_thread = threadCreate(audio_thread, NULL, 32*1024, 0x19, -2, false);
    }
    g_nd24_wanted_core = nd24_proc; /* -2 = coeur 0 (comme avant), 2 = coeur libere */
    if (!s_thread) {
        s_thread_run = false; free_cover();
        linearFree(s_pcm_buf); s_pcm_buf = NULL;
        mpg123_exit(); ndspExit(); return (Result)-2;
    }
    return 0;
}

void audio_exit(void)
{
    s_thread_run = false; s_state = AUDIO_STOPPED;
    svcSleepThread(50000000ULL);
    ndspChnWaveBufClear(NDSP_CHANNEL);
    if (s_thread) { threadJoin(s_thread, U64_MAX); threadFree(s_thread); s_thread = NULL; }
    if (s_vorbis)   { stb_vorbis_close(s_vorbis); s_vorbis   = NULL; s_ogg_carry_n = 0; s_ogg_carry_off = 0; }
    flac_close_stream();
    if (s_wav_open) { drwav_uninit(&s_wav);         s_wav_open = false; }
    if (s_wav_fp)   { fclose(s_wav_fp);             s_wav_fp   = NULL; }
    if (s_mpg)      { mpg123_close(s_mpg); mpg123_delete(s_mpg); s_mpg = NULL; }
    if (s_mp3_fp)   { fclose(s_mp3_fp);             s_mp3_fp   = NULL; }
    if (s_opus)     { op_free(s_opus);              s_opus     = NULL; }
    if (s_aac)      { mp4_aac_close(s_aac);         s_aac      = NULL; }  /* AAC3 */
    free_cover(); mpg123_exit();
    /* V0.97 : cleanup crossfade */
    audio_xfade_stop();

    /* V0.98 : cleanup SoundTouch */
    st_destroy();

    if (s_pcm_buf) { linearFree(s_pcm_buf); s_pcm_buf = NULL; }
    ndspExit();
}

Result audio_load(const char *path)
{
    audio_lock_init();
    
    LightLock_Lock(&s_audio_lock);
    audio_stop();
    st_flush();
    
    free_cover(); /* liberer la pochette precedente avant de vider s_meta (elle etait perdue sinon : memset annulait le pointeur avant que free_cover() le voie) */
    memset(&s_meta, 0, sizeof(s_meta));
    meta_from_filename(path);
    s_fmt = detect_fmt(path);
    if (s_fmt == FMT_UNKNOWN) { LightLock_Unlock(&s_audio_lock); return (Result)-1; }
    {
        const char *_e = strrchr(path, '.');
        if (_e) {
            strncpy(s_meta.format, _e+1, 7); s_meta.format[7] = 0;
            for (int i = 0; s_meta.format[i]; i++)
                if (s_meta.format[i] >= 'a' && s_meta.format[i] <= 'z')
                    s_meta.format[i] -= 32;
        } else strcpy(s_meta.format, "???");
    }
    s_samples_played = 0; s_buf_idx = 0; s_mp3_carry_n = 0; s_mp3_carry_off = 0; /* CLOT8f2 */
    s_rs_native = false; /* EQOP2b */
    memset(s_oscillo_buf, 0, sizeof(s_oscillo_buf));
    s_oscillo_idx = 0;
    ndspChnReset(NDSP_CHANNEL);
    ndspChnSetInterp(NDSP_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(NDSP_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    set_mix(s_volume);
    switch (s_fmt) {
        case FMT_OGG: {
            int err = 0;
            /* FILE* ouvert ici (et non stb_vorbis_open_filename) uniquement
               pour lui donner un gros tampon de lecture, cf FLAC ci-dessus.
               close_on_free = 1 : stb_vorbis fermera le fichier lui-meme. */
            FILE *ogg_fp = fopen(path, "rb");
            if (ogg_fp) setvbuf(ogg_fp, s_ogg_iobuf, _IOFBF, OGG_IOBUF_SIZE);
            s_vorbis = ogg_fp ? stb_vorbis_open_file(ogg_fp, 1, &err, NULL) : NULL;
            s_ogg_carry_n = 0; s_ogg_carry_off = 0;
            if (!s_vorbis) {
                /* Ne PAS fclose(ogg_fp) ici : close_on_free=1 signifie que
                   stb_vorbis_open_file a deja pris possession du FILE* et l a
                   ferme lui-meme des que le parsing echoue (vorbis_deinit).
                   Le refermer ici serait un double fclose() sur un fichier
                   deja rendu - un fichier .ogg corrompu pouvait ainsi
                   corrompre la table FILE* de la libc. */
                LightLock_Unlock(&s_audio_lock);
                return (Result)-1;
            }
            s_vinfo = stb_vorbis_get_info(s_vorbis);
            ndspChnSetRate(NDSP_CHANNEL, needs_resample(s_vinfo.sample_rate) ? (float)TARGET_RATE : (float)s_vinfo.sample_rate);
            s_sample_rate = s_vinfo.sample_rate;

            int total = stb_vorbis_stream_length_in_samples(s_vorbis);
            if (s_vinfo.sample_rate > 0) s_meta.duration_sec = total / s_vinfo.sample_rate;
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
            {   /* ND22 : pochette lecteur WAV (chunk ID3 dans RIFF) */
                uint32_t nd22_il = 0;
                void *nd22_im = nd21_wav_picture(path, &nd22_il);
                if (nd22_im && nd22_il > 0) {
                    s_meta.cover_data = nd22_im;
                    s_meta.cover_size = nd22_il;
                    s_meta.has_cover  = true;
                } else if (nd22_im) free(nd22_im);
            }
            s_wav_fp = fopen(path, "rb");
            if (s_wav_fp) setvbuf(s_wav_fp, s_wav_iobuf, _IOFBF, WAV_IOBUF_SIZE);
            if (!s_wav_fp ||
                !drwav_init(&s_wav, wav_on_read, wav_on_seek, wav_on_tell, s_wav_fp, NULL)) {
                if (s_wav_fp) { fclose(s_wav_fp); s_wav_fp = NULL; }
                LightLock_Unlock(&s_audio_lock);
                return (Result)-1;
            }
            s_wav_open = true; s_sample_rate = s_wav.sampleRate;
            ndspChnSetRate(NDSP_CHANNEL, needs_resample(s_wav.sampleRate) ? (float)TARGET_RATE : (float)s_wav.sampleRate);
            s_meta.duration_sec = (int)(s_wav.totalPCMFrameCount / s_wav.sampleRate);
            break;
        }
        case FMT_FLAC: {
            {   /* ND22 : pochette lecteur FLAC (bloc PICTURE) */
                uint32_t nd22_il = 0;
                void *nd22_im = nd19_flac_picture(path, &nd22_il);
                if (nd22_im && nd22_il > 0) {
                    s_meta.cover_data = nd22_im;
                    s_meta.cover_size = nd22_il;
                    s_meta.has_cover  = true;
                } else if (nd22_im) free(nd22_im);
            }
            s_flac_fp = fopen(path, "rb");
            if (s_flac_fp) {
                setvbuf(s_flac_fp, s_flac_iobuf, _IOFBF, FLAC_IOBUF_SIZE);
                s_flac = drflac_open(flac_on_read, flac_on_seek, flac_on_tell, s_flac_fp, NULL);
            }
            if (!s_flac) {
                if (s_flac_fp) { fclose(s_flac_fp); s_flac_fp = NULL; }
                LightLock_Unlock(&s_audio_lock);
                return (Result)-1;
            }
            s_sample_rate = s_flac->sampleRate;
            if (s_flac->sampleRate == 48000) { /* CLOT5b : FLAC 48k natif (idem EQOP2c/d pour OPUS) */
                ndspChnSetRate(NDSP_CHANNEL, 48000.f);
                s_rs_native = true;
            } else {
                ndspChnSetRate(NDSP_CHANNEL, needs_resample(s_flac->sampleRate) ? (float)TARGET_RATE : (float)s_flac->sampleRate);
            }
            if (s_flac->sampleRate > 0)
                s_meta.duration_sec = (int)(s_flac->totalPCMFrameCount / s_flac->sampleRate);
            break;
        }
        case FMT_MP3: {
            int err = 0;
            s_mpg = mpg123_new(NULL, &err);
            if (!s_mpg) { LightLock_Unlock(&s_audio_lock); return (Result)-1; }
            mpg123_param(s_mpg, MPG123_FORCE_RATE, SAMPLE_RATE, 0);
            mpg123_param(s_mpg, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO | MPG123_FUZZY, 0);
            s_mp3_fp = fopen(path, "rb");
            if (s_mp3_fp) setvbuf(s_mp3_fp, s_mp3_iobuf, _IOFBF, MP3_IOBUF_SIZE);
            if (!s_mp3_fp ||
                mpg123_replace_reader_handle(s_mpg, mp3_r_read, mp3_r_lseek, mp3_r_cleanup) != MPG123_OK ||
                mpg123_open_handle(s_mpg, s_mp3_fp) != MPG123_OK) {
                mpg123_delete(s_mpg); s_mpg = NULL;
                if (s_mp3_fp) { fclose(s_mp3_fp); s_mp3_fp = NULL; }
                LightLock_Unlock(&s_audio_lock);
                return (Result)-1;
            }
            long rate; int ch, enc;
            mpg123_getformat(s_mpg, &rate, &ch, &enc);
            mpg123_format_none(s_mpg);
            mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);

            ndspChnSetRate(NDSP_CHANNEL, needs_resample((u32)rate) ? (float)TARGET_RATE : (float)rate);
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
            {   /* ND22 : pochette lecteur OPUS (METADATA_BLOCK_PICTURE) */
                uint32_t nd22_il = 0;
                void *nd22_im = nd19_opus_picture(path, &nd22_il);
                if (nd22_im && nd22_il > 0) {
                    s_meta.cover_data = nd22_im;
                    s_meta.cover_size = nd22_il;
                    s_meta.has_cover  = true;
                } else if (nd22_im) free(nd22_im);
            }
            int err = 0;
            s_opus = op_open_file(path, &err);
            if (!s_opus) { LightLock_Unlock(&s_audio_lock); return (Result)-1; }
            ndspChnSetRate(NDSP_CHANNEL, 48000.f); /* EQOP2c : OPUS natif 48k */
            s_sample_rate = 48000;
            s_rs_native = true; /* EQOP2d : OPUS en natif */
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
        case FMT_M4A: {           /* AAC3 : MP4/M4A/AAC natif */
            s_aac = mp4_aac_open(path);
            if (!s_aac) { LightLock_Unlock(&s_audio_lock); return (Result)-1; }
            s_sample_rate = s_aac->sample_rate;
            ndspChnSetRate(NDSP_CHANNEL,
                needs_resample(s_aac->sample_rate) ? (float)TARGET_RATE
                                                     : (float)s_aac->sample_rate);
            if (s_aac->duration_sec > 0)
                s_meta.duration_sec = (int)s_aac->duration_sec;
            /* AAC10 : meta + pochette depuis le conteneur MP4 */
            if (s_aac->title[0])  snprintf(s_meta.title,  sizeof(s_meta.title),  "%s", s_aac->title);
            if (s_aac->artist[0]) snprintf(s_meta.artist, sizeof(s_meta.artist), "%s", s_aac->artist);
            if (s_aac->album[0])  snprintf(s_meta.album,  sizeof(s_meta.album),  "%s", s_aac->album);
            if (s_aac->cover_data && s_aac->cover_size > 0) {
                s_meta.cover_data = malloc(s_aac->cover_size);
                if (s_meta.cover_data) {
                    memcpy(s_meta.cover_data, s_aac->cover_data, s_aac->cover_size);
                    s_meta.cover_size = s_aac->cover_size;
                    s_meta.has_cover  = true;
                }
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

/* MOTEUR DE FONDU MATÉRIEL DIRECT NDSP 60 FPS AVEC INVERSION SANS A-COUP */
static volatile bool  s_fade_in_active  = false;
static volatile bool  s_fade_out_active = false;
static u64            s_fade_start_time = 0;
static float          s_current_fade_factor = 1.0f;
static int            s_pause_delay_frames = 0;
static volatile bool  s_pending_unpause   = false;

static void set_ndsp_fade_mix(float factor) {
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    s_current_fade_factor = factor;

    float vol = s_volume > 1.f ? 1.f : s_volume;
    float mix[12] = {0};
    mix[0] = vol * factor;
    mix[1] = vol * factor;
    ndspChnSetMix(NDSP_CHANNEL, mix);
}

void audio_update_fade(void)
{
    if (s_pending_unpause) { s_pending_unpause = false; ndspChnSetPaused(NDSP_CHANNEL, false); }
    if (!s_fade_in_active && !s_fade_out_active) return;

    /* S7: delai avant pause pour vider le buffer DSP a volume 0 */
    if (s_pause_delay_frames > 0) {
        s_pause_delay_frames--;
        if (s_pause_delay_frames == 0) {
            ndspChnSetPaused(NDSP_CHANNEL, true);
            s_state = AUDIO_PAUSED;
            extern Settings g_settings;
            if (g_settings.white_noise_keepalive) {
                void audio_start_white_noise(void);
                audio_start_white_noise();
            }
        }
        return;
    }

    extern Settings g_settings;
    float fade_dur = g_settings.pause_fade_sec;
    if (fade_dur <= 0.05f) {
        s_fade_in_active = false;
        s_fade_out_active = false;
        set_ndsp_fade_mix(1.0f);
        return;
    }

    u64 now = osGetTime();
    float elapsed = (float)(now - s_fade_start_time) / 1000.f;
    float progress = elapsed / fade_dur;

    if (s_fade_in_active) {
        if (progress >= 1.0f) {
            set_ndsp_fade_mix(1.0f);
            s_fade_in_active = false;
        } else {
            set_ndsp_fade_mix(progress);
        }
    } else if (s_fade_out_active) {
        if (progress >= 1.0f) {
            set_ndsp_fade_mix(0.0f);
            s_fade_out_active = false;
            s_pause_delay_frames = 3; /* ~50ms buffer drain */
        } else {
            set_ndsp_fade_mix(1.0f - progress);
        }
    }
}

void audio_play(void)
{
    s_finished_naturally = false;
    s_pause_delay_frames = 0; /* S7: annuler le delai de pause residuel */
    if (s_fmt == FMT_UNKNOWN) return;
    extern int audio_get_position(void);
    extern void audio_seek(int seconds);
    /* Si la musique n'est pas une reprise de pause et qu'on est tout au debut, on garantit 0:00 ! */
    if (s_state == AUDIO_STOPPED && audio_get_position() <= 1) {
        audio_seek(0);
    }

    void audio_stop_white_noise(void);
    audio_stop_white_noise();

    extern Settings g_settings;
    float fade_dur = g_settings.pause_fade_sec;

    /* S7: etat precedent pour savoir si reprise ou nouveau */
    AudioState prev_state = s_state;
    s_state = AUDIO_PLAYING;

    if (fade_dur > 0.05f && prev_state == AUDIO_PAUSED) {
        /* REPRISE DE PAUSE : fade-in depuis le facteur actuel */
        float start_factor = s_fade_out_active ? s_current_fade_factor : 0.0f;
        s_fade_out_active = false;
        s_fade_in_active  = true;
        u64 offset_ms = (u64)(start_factor * fade_dur * 1000.f);
        s_fade_start_time = osGetTime() - offset_ms;
        set_ndsp_fade_mix(start_factor);
        s_pending_unpause = true;
    } else {
        /* NOUVEAU MORCEAU ou fade OFF : demarrage immediat a 100% */
        s_fade_in_active  = false;
        s_fade_out_active = false;
        s_pause_delay_frames = 0;
        s_pending_unpause = true;
        set_ndsp_fade_mix(1.0f);
    }
}

void audio_pause(void)
{
    s_finished_naturally = false;
    extern Settings g_settings;
    float fade_dur = g_settings.pause_fade_sec;

    if (fade_dur > 0.05f) {
        /* On recupere le point de depart pour un fondu de sortie inverse en douceur */
        float start_factor = s_fade_in_active ? s_current_fade_factor : 1.0f;

        s_fade_in_active  = false;
        s_fade_out_active = true;
        s_state = AUDIO_PAUSED; /* Marque l'intention de pause (le get_state compensera l'UI) */

        u64 offset_ms = (u64)((1.0f - start_factor) * fade_dur * 1000.f);
        s_fade_start_time = osGetTime() - offset_ms;

        set_ndsp_fade_mix(start_factor);
    } else {
        s_fade_in_active  = false;
        s_fade_out_active = false;
        s_state = AUDIO_PAUSED;
        ndspChnSetPaused(NDSP_CHANNEL, true);
        if (g_settings.white_noise_keepalive) {
            void audio_start_white_noise(void);
            audio_start_white_noise();
        }
    }
}

void audio_stop(void)
{
    s_finished_naturally = false;
    /* Active automatiquement le son blanc a l'arret de la musique */
    extern Settings g_settings;
    void audio_start_white_noise(void);
    if (g_settings.white_noise_keepalive) {
        audio_start_white_noise();
    }
    /* Toute demande d arret libere aussi un decodeur en train d attendre la
       suite d un flux : sans ca, le thread audio garderait s_audio_lock
       pendant son attente et bloquerait l appelant. */
    s_stream_growing = false;
    s_state = AUDIO_STOPPED;
    ndspChnSetPaused(NDSP_CHANNEL, true);
    ndspChnReset(NDSP_CHANNEL);
    ndspChnWaveBufClear(NDSP_CHANNEL);
    
    svcSleepThread(16000000LL);
    
    if (s_vorbis)   { stb_vorbis_close(s_vorbis); s_vorbis   = NULL; s_ogg_carry_n = 0; s_ogg_carry_off = 0; }
    flac_close_stream();
    if (s_wav_open) { drwav_uninit(&s_wav);         s_wav_open = false; }
    if (s_wav_fp)   { fclose(s_wav_fp);             s_wav_fp   = NULL; }
    if (s_mpg)      { mpg123_close(s_mpg); mpg123_delete(s_mpg); s_mpg = NULL; }
    if (s_mp3_fp)   { fclose(s_mp3_fp);             s_mp3_fp   = NULL; }
    if (s_opus)     { op_free(s_opus);              s_opus     = NULL; }
    /* Le decodeur AAC etait le seul a ne pas etre libere ici : chaque piste
       M4A/MP4/AAC chargee abandonnait un descripteur de fichier (+ son tampon
       de 32 Ko), la structure du decodeur, sa pochette, le decodeur Helix et
       les tables d index MP4. Au bout de quelques dizaines de changements de
       piste, la 3DS n avait plus de descripteurs libres et l application se
       figeait ; seul un redemarrage remettait le compteur a zero. */
    if (s_aac)      { mp4_aac_close(s_aac);         s_aac      = NULL; }
    s_samples_played = 0; s_fmt = FMT_UNKNOWN; s_mp3_carry_n = 0; s_mp3_carry_off = 0; /* CLOT8f3 */
    resample_reset(); /* Reset position resampling */
    s_sample_rate = SAMPLE_RATE;
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
    int dur = s_meta.duration_sec;
    if (dur > 0 && seconds > dur) seconds = dur;
    
    u64 target = (u64)seconds * s_sample_rate;
    s_seek_target    = target;
    s_samples_played = target;
    st_flush();
    resample_reset();
    s_seek_requested = true;

    /* Si en PAUSE ou STOP, executer le seek immediatement sans attendre le thread audio */
    if (s_state != AUDIO_PLAYING) {
        do_seek();
    }
}

static void do_seek(void)
{
    /* Vider immediatement la file d attente materielle DSP (elimine la latence de 185ms) */
    ndspChnWaveBufClear(NDSP_CHANNEL);
    s_seek_requested = false;
    u64 target = s_seek_target;

    /* Seek dans le fichier source */
    switch (s_fmt) {
        case FMT_OGG:
            if (s_vorbis)
                stb_vorbis_seek(s_vorbis, (unsigned int)target);
            break;
        case FMT_WAV:
            if (s_wav_open)
                drwav_seek_to_pcm_frame(&s_wav, target);
            break;
        case FMT_FLAC:
            if (s_flac)
                drflac_seek_to_pcm_frame(s_flac, target);
            break;
        case FMT_MP3:
            if (s_mpg) {
                off_t res = mpg123_seek(s_mpg, (off_t)target, SEEK_SET);
                if (res < 0) {
                    mpg123_seek(s_mpg, 0, SEEK_SET);
                    target = 0;
                }
            }
            break;
        case FMT_OPUS:
            if (s_opus)
                op_pcm_seek(s_opus, (ogg_int64_t)target);
            break;
        case FMT_M4A:          /* AAC11/AAC4 : seek M4A via index minimp4 */
            if (s_aac)
                mp4_aac_seek_sample(s_aac, target);
            break;
        default:
            break;
    }

    /* Mettre a jour la position affichee */
    s_samples_played = target;

    /* Reset position resampling pour eviter clic au resume */
    resample_reset();

    /* V0.98 : Reset SoundTouch au seek */
    st_flush();
    memset(s_oscillo_buf, 0, sizeof(s_oscillo_buf));
    s_oscillo_idx = 0;

    /* PAS de ndspChnWaveBufClear ici !
       Le NDSP finit naturellement son buffer actuel
       Le prochain decode_chunk lira depuis la nouvelle position
       = pas de saut, pas de silence */
}

u32   audio_get_sample_rate(void)  { return s_sample_rate; }
int   audio_get_position(void)     { return s_sample_rate ? (int)(s_samples_played / s_sample_rate) : 0; }
int   audio_get_duration(void)     { return s_meta.duration_sec; }
void  audio_set_duration_hint(int seconds)
{ if (seconds > 0) s_meta.duration_sec = seconds; }
float audio_get_position_pct(void) { return s_meta.duration_sec > 0 ? (float)audio_get_position() / s_meta.duration_sec : 0.f; }
void  audio_set_volume(float v)    { s_volume = v < 0.f ? 0.f : (v > 3.f ? 3.f : v); set_mix(s_volume); }  /* VOLBOOST3 */
float audio_get_volume(void)       { return s_volume; }

void audio_set_speed(float speed)
{
    if (speed < 0.5f) speed = 0.5f;
    if (speed > 2.0f) speed = 2.0f;
    s_speed = speed;

    /* V0.97 : combiner avec le pitch pro si actif (New 3DS) */
    extern bool g_is_new3ds;
    float pitch_ratio = 1.0f;
    if (g_is_new3ds && g_settings.pitch_semitones != 0) {
        pitch_ratio = powf(2.f, (float)g_settings.pitch_semitones / 12.f);
    }

    /* NDSP rate = TARGET × speed_fun × pitch_pro
       Le WSOLA de audio_thread compensera pour maintenir la vitesse voulue */
    float new_rate = (float)(s_rs_native ? 48000 : TARGET_RATE) * speed * pitch_ratio; /* EQOP2g */
    ndspChnSetRate(NDSP_CHANNEL, new_rate);
}
AudioState audio_get_state(void) {
    /* Maintien des barres EQ et de la LED pendant le fade-out ! */
    if (s_state == AUDIO_PAUSED && s_fade_out_active) {
        return AUDIO_PLAYING;
    }
    return s_state;
}
bool audio_is_finished(void) { return s_finished_naturally; }
const AudioMetadata *audio_get_metadata(void) { return &s_meta; }



void audio_get_visualizer_fft(float out[EQ_BANDS])
{
    s_fft_needed = true; /* Signale au thread audio que FFT est necessaire */
    for (int i = 0; i < EQ_BANDS; i++) out[i] = s_viz_fft[i];
}

void audio_get_visualizer(float out[EQ_BANDS])
{
    s_fft_needed = false; /* Pas besoin de FFT si viz classique */
    for (int i = 0; i < EQ_BANDS; i++) out[i] = s_viz[i];
}

void  audio_eq_set_gain(int b, float db) {
    if (b >= 0 && b < EQ_BANDS) s_eq_gains[b] = db;
    /* Mettre a jour le cache eq_active */
    bool active = false;
    for (int i = 0; i < EQ_BANDS; i++)
        if (s_eq_gains[i] != 0.f) { active = true; break; }
    s_eq_active_cache = active;
}
float audio_eq_get_gain(int b)           { return (b >= 0 && b < EQ_BANDS) ? s_eq_gains[b] : 0.f; }
void  audio_eq_apply_preset(const EQPreset *p) {
    if (p) for (int i=0;i<EQ_BANDS;i++) s_eq_gains[i]=p->gain[i];
    /* Mettre a jour le cache */
    bool active = false;
    for (int i = 0; i < EQ_BANDS; i++)
        if (s_eq_gains[i] != 0.f) { active = true; break; }
    s_eq_active_cache = active;
}

EQPreset eq_preset_flat        = {{0,0,0,0,0,0,0,0},       "Flat"};
EQPreset eq_preset_bass_boost  = {{6,5,4,1,0,0,0,0},       "Bass Boost"};
EQPreset eq_preset_vocal       = {{-2,-1,2,4,4,3,1,0},     "Vocal"};
EQPreset eq_preset_rock        = {{4,3,0,-1,0,2,4,5},      "Rock"};
EQPreset eq_preset_classical   = {{3,2,0,0,0,0,2,3},       "Classical"};
EQPreset eq_preset_electronic  = {{5,4,0,-1,1,3,4,5},      "Electronic"};
EQPreset eq_preset_custom      = {{0,0,0,0,0,0,0,0},       "Custom"};

/* V0.96 : desactiver temporairement le calcul viz (mode eco) */
void audio_set_viz_active(bool active)
{
    s_viz_forced_off = !active;
}

/* V0.97 : Copie les samples oscilloscope vers out[] */
int audio_get_oscillo(float out[256])
{
    int n = s_oscillo_idx;
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) out[i] = s_oscillo_buf[i];
    return n;
}

/* ═══════════════════════════════════════════════════════════════
   V0.97 : CROSSFADE - precache PCM + lecture canal separe
   ═══════════════════════════════════════════════════════════════ */

static void xfade_lock_init(void)
{
    if (!s_xfade_lock_init) {
        LightLock_Init(&s_xfade_lock);
        s_xfade_lock_init = true;
    }
}

static void xfade_precache_worker(void *arg)
{
    (void)arg;
    s_xfade_precache_running = true;
    /* Appel bloquant du precache (thread separe = pas de freeze main) */
    audio_xfade_precache(s_xfade_precache_pending, s_xfade_precache_pending_dur);
    s_xfade_precache_running = false;
}

/* V0.97 : Precache asynchrone - lance dans un thread priorite basse */
void audio_xfade_precache_async(const char *path, int duration_sec)
{
    if (!path || s_xfade_precache_running) return;
    strncpy(s_xfade_precache_pending, path, 511);
    s_xfade_precache_pending[511] = 0;
    s_xfade_precache_pending_dur = duration_sec;

    /* Le thread est cree DETACHE (dernier argument), donc libctru libere son
       handle tout seul a la sortie : le joindre puis le liberer ici revenait a
       toucher une structure deja rendue. On se contente de lacher la
       reference ; le garde s_xfade_precache_running ci-dessus empeche deja
       d en lancer deux a la fois. */
    s_xfade_precache_thread = NULL;

    /* Nouveau thread priorite basse (n interfere pas avec audio) */
    s_xfade_precache_thread = threadCreate(
        xfade_precache_worker, NULL, 64*1024, 0x3F, -1, true);
}

bool audio_xfade_is_precached(const char *path)
{
    if (!path || !s_xfade_buf || s_xfade_total_samples == 0) return false;
    return strcmp(s_xfade_path, path) == 0;
}

bool audio_xfade_is_active(void)
{
    return s_xfade_active;
}

bool audio_xfade_precache(const char *path, int duration_sec)
{
    xfade_lock_init();
    if (!path || !path[0]) return false;
    if (duration_sec < 2) duration_sec = 2;
    if (duration_sec > XFADE_MAX_SEC) duration_sec = XFADE_MAX_SEC;

    /* Deja precache pour ce path ? */
    if (audio_xfade_is_precached(path)) return true;

    LightLock_Lock(&s_xfade_lock);

    /* Nettoyer l ancien buffer */
    if (s_xfade_buf) {
        linearFree(s_xfade_buf);
        s_xfade_buf = NULL;
    }
    s_xfade_total_samples = 0;
    s_xfade_path[0] = 0;

    /* Allouer nouveau buffer (max duration_sec * SAMPLE_RATE stereo) */
    int max_samples = duration_sec * SAMPLE_RATE;
    size_t buf_size = max_samples * CHANNELS * sizeof(s16);
    s_xfade_buf = (s16*)linearAlloc(buf_size);
    if (!s_xfade_buf) {
        LightLock_Unlock(&s_xfade_lock);
        return false;
    }
    memset(s_xfade_buf, 0, buf_size);

    LightLock_Unlock(&s_xfade_lock);

    /* Ouvrir le fichier avec un decodeur temporaire */
    AudioFormat fmt = detect_fmt(path);
    int decoded_pairs = 0;

    /* On utilise des variables locales temporaires pour ne pas
       toucher a l etat du decodeur principal */
    switch (fmt) {
        case FMT_OGG: {
            int err = 0;
            stb_vorbis *v = stb_vorbis_open_filename(path, &err, NULL);
            if (!v) break;
            stb_vorbis_info info = stb_vorbis_get_info(v);
            /* Decoder directement en s16 */
            while (decoded_pairs < max_samples) {
                int remaining = max_samples - decoded_pairs;
                s16 *out = s_xfade_buf + decoded_pairs * CHANNELS;
                int got = stb_vorbis_get_samples_short_interleaved(
                    v, 2, out, remaining * 2);
                if (got <= 0) break;
                decoded_pairs += got;
                (void)info;
            }
            stb_vorbis_close(v);
            break;
        }
        case FMT_WAV: {
            drwav w;
            if (!drwav_init_file(&w, path, NULL)) break;
            drwav_uint64 n = drwav_read_pcm_frames_s16(
                &w, max_samples, s_xfade_buf);
            decoded_pairs = (int)n;
            drwav_uninit(&w);
            break;
        }
        case FMT_FLAC: {
            drflac *fl = drflac_open_file(path, NULL);
            if (!fl) break;
            drflac_uint64 n = drflac_read_pcm_frames_s16(
                fl, max_samples, s_xfade_buf);
            decoded_pairs = (int)n;
            drflac_close(fl);
            break;
        }
        case FMT_MP3: {
            int err = 0;
            mpg123_handle *mh = mpg123_new(NULL, &err);
            if (!mh) break;
            mpg123_param(mh, MPG123_FORCE_RATE, SAMPLE_RATE, 0);
            mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO, 0);
            if (mpg123_open(mh, path) == MPG123_OK) {
                long rate; int ch, enc;
                mpg123_getformat(mh, &rate, &ch, &enc);
                mpg123_format_none(mh);
                mpg123_format(mh, rate, 2, MPG123_ENC_SIGNED_16);
                size_t done = 0;
                size_t max_bytes = max_samples * CHANNELS * sizeof(s16);
                mpg123_read(mh, (unsigned char*)s_xfade_buf, max_bytes, &done);
                decoded_pairs = (int)(done / (CHANNELS * sizeof(s16)));
                mpg123_close(mh);
            }
            mpg123_delete(mh);
            break;
        }
        case FMT_OPUS: {
            int err = 0;
            OggOpusFile *op = op_open_file(path, &err);
            if (!op) break;
            /* op_read_stereo lit en s16 stereo entrelace */
            int total = 0;
            while (total < max_samples) {
                int n = op_read_stereo(op,
                    s_xfade_buf + total * CHANNELS,
                    (max_samples - total) * 2);
                if (n <= 0) break;
                total += n;
            }
            decoded_pairs = total;
            op_free(op);
            break;
        }
        default:
            break;
    }

    LightLock_Lock(&s_xfade_lock);
    s_xfade_total_samples = decoded_pairs;
    if (decoded_pairs > 0) {
        strncpy(s_xfade_path, path, 511);
        s_xfade_path[511] = 0;
    }
    LightLock_Unlock(&s_xfade_lock);

    return decoded_pairs > 0;
}

void audio_xfade_start(void)
{
    xfade_lock_init();
    LightLock_Lock(&s_xfade_lock);
    if (!s_xfade_buf || s_xfade_total_samples == 0) {
        LightLock_Unlock(&s_xfade_lock);
        return;
    }

    /* Config canal 1 */
    ndspChnReset(XFADE_CHANNEL);
    ndspChnSetInterp(XFADE_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(XFADE_CHANNEL, (float)SAMPLE_RATE);
    ndspChnSetFormat(XFADE_CHANNEL, NDSP_FORMAT_STEREO_PCM16);

    /* Mix initial : 100% volume canal 1 (sera modifie par audio_xfade_set_mix) */
    float mix[12] = {0};
    mix[0] = mix[1] = 0.f;
    ndspChnSetMix(XFADE_CHANNEL, mix);

    /* Envoyer le buffer entier au NDSP */
    memset(&s_xfade_wbuf, 0, sizeof(s_xfade_wbuf));
    s_xfade_wbuf.data_vaddr = s_xfade_buf;
    s_xfade_wbuf.nsamples = s_xfade_total_samples;
    s_xfade_wbuf.looping = false;

    DSP_FlushDataCache(s_xfade_buf,
        s_xfade_total_samples * CHANNELS * sizeof(s16));
    ndspChnWaveBufAdd(XFADE_CHANNEL, &s_xfade_wbuf);

    s_xfade_active = true;
    s_xfade_play_pos = 0;
    LightLock_Unlock(&s_xfade_lock);
}

void audio_xfade_set_mix(float vol_current, float vol_next)
{
    /* Canal 0 (piste actuelle) */
    float mix0[12] = {0};
    mix0[0] = mix0[1] = vol_current * (s_volume > 1.f ? 1.f : s_volume);   /* VOLBOOST */
    ndspChnSetMix(NDSP_CHANNEL, mix0);

    /* Canal 1 (piste suivante) */
    if (s_xfade_active) {
        float mix1[12] = {0};
        mix1[0] = mix1[1] = vol_next * (s_volume > 1.f ? 1.f : s_volume);   /* VOLBOOST */
        ndspChnSetMix(XFADE_CHANNEL, mix1);
    }
}

void audio_xfade_stop(void)
{
    xfade_lock_init();
    LightLock_Lock(&s_xfade_lock);
    if (s_xfade_active) {
        ndspChnWaveBufClear(XFADE_CHANNEL);
        ndspChnReset(XFADE_CHANNEL);
        s_xfade_active = false;
    }
    if (s_xfade_buf) {
        linearFree(s_xfade_buf);
        s_xfade_buf = NULL;
    }
    s_xfade_total_samples = 0;
    s_xfade_path[0] = 0;
    s_xfade_play_pos = 0;
    LightLock_Unlock(&s_xfade_lock);
    /* Restaurer le mix normal du canal 0 */
    set_mix(s_volume);
}

/* VIZ14b : tick de visualisation cote UI (appele a ~60fps par le player).
   Visualise le buffer NDSP en cours de lecture, une fois par changement :
   le rythme vient du DSP lui-meme (~26ms/buffer), jamais des a-coups du
   decodeur, et un decodage couteux ne peut plus geler la viz puisqu il
   tourne dans un autre thread. Le buffer PLAYING n'est jamais reecrit par
   le producteur (il attend sa fin) : lecture sans dechirure. */
static int s_viz14_slot = -1;
void audio_viz_tick(void)
{
    if (s_pcm_buf == NULL) return;
    if (!s_nd24_viz_pass_ok) return; /* CLOT9d */
    s_nd24_viz_pass_ok = false;
    int cur = -1;
    for (int k = 0; k < NUM_BUFS; k++) {
        if (s_wbufs[k].status == NDSP_WBUF_PLAYING) { cur = k; break; }
    }
    s_viz14_slot = cur;
    if (cur < 0) return;
    if (g_settings.viz_enabled == 0 || s_viz_forced_off != 0) return;

    /* Plafond ~30 Hz : largement suffisant a l oeil (c etait ~11 Hz avant), et
       ca borne le cout cote thread UI. Utile sur les formats sans marge CPU
       (OPUS, le plus lourd a decoder), ou analyser a chaque image suffisait a
       faire hoqueter l interface. */
    {
        static u64 s_viz_last_tick = 0;
        u64 now_t = svcGetSystemTick();
        if (s_viz_last_tick && (now_t - s_viz_last_tick) < 268ULL * 30000ULL) return;
        s_viz_last_tick = now_t;
    }

    /* La fenetre analysee est prise a la position REELLEMENT jouee dans le
       creneau courant, et non plus au seul changement de creneau.
       Avant, la viz se rafraichissait a la cadence des buffers audio : un
       creneau de 4096 frames dure 93 ms, soit ~11 images/seconde de viz, alors
       qu OPUS decode par trames natives de ~960 frames (~20 ms) et atteignait
       donc ~50 Hz. D ou une viz fluide en OPUS et saccadee sur tous les autres
       formats, sans que le CPU n y soit pour rien. */
    int vz_pairs = s_viz17_n[cur];
    int vz_off   = (int)ndspChnGetSamplePos(NDSP_CHANNEL);
    if (vz_off < 0)        vz_off = 0;
    if (vz_off > vz_pairs) vz_off = vz_pairs;
    int vz_avail = vz_pairs - vz_off;
    if (vz_avail < 64) { /* fin de creneau : reculer pour garder de quoi analyser */
        vz_off   = vz_pairs > 256 ? vz_pairs - 256 : 0;
        vz_avail = vz_pairs - vz_off;
    }
    if (vz_avail <= 0) return;

    update_viz(s_viz17_buf[cur] + (size_t)vz_off * 2, vz_avail); /* VIZ17b */
    {  /* VIZ16b : oscillo remplit sur le tick de consommation (deplace du thread audio) */
    const s16 *pcm = s_viz17_buf[cur] + (size_t)vz_off * 2; /* VIZ17c */
    int pairs = vz_avail;
    if (g_settings.viz_enabled && !s_viz_forced_off) {
        static int osc_counter = 0;
        bool st_active = !st_is_identity();
        int osc_div = 1;
        int target_pts = 256;
        if (st_active) {
            if (s_is_new3ds) {
                osc_div = 1;      /* New : oscillo a chaque buffer */
                target_pts = 192; /* plus detaille */
            } else {
                osc_div = 2;      /* Old : une fois sur 2 */
                target_pts = 96;  /* plus leger */
            }
        }
        osc_counter++;
        if ((osc_counter % osc_div) == 0) {
            int step = pairs / target_pts;
            if (step < 1) step = 1;
            int oi = 0;
            for (int i = 0; i < pairs && oi < target_pts; i += step, oi++) {
                float l = pcm[i*2]   / 32767.f;
                float r = pcm[i*2+1] / 32767.f;
                s_oscillo_buf[oi] = (l + r) * 0.5f;
            }
            for (int j = oi; j < 256; j++) s_oscillo_buf[j] = 0.f;
            s_oscillo_idx = oi;
        }
    }
    }
}

/* =============================================================================
   GENERATEUR DE SON BLANC ANTI-EXTINCTION D'ENCEINTES BLUETOOTH
   Maintient un flux audio minimal pour empecher les enceintes (Bose, JBL, etc.)
   de s'eteindre pendant les silences ou entre deux morceaux.
   Amplitude : +/- 300 sur 32767 (~1% du volume max) - souffle inaudible
   ============================================================================= */
#define WN_CHANNEL     3
#define WN_BUF_SAMPLES 4096
#define WN_AMPLITUDE   300  /* +/- 300 sur 32767 : inaudible mais suffisant pour JBL Go */

static s16 *s_wn_buf = NULL;
static ndspWaveBuf s_wn_wavebuf;
static bool s_wn_active = false;

#include <math.h>

void audio_update_white_noise_volume(void)
{
    if (!s_wn_active) return;
    u8 slider_val = 63;
    HIDUSER_GetSoundVolume(&slider_val);
    float vol_factor = (float)slider_val / 63.0f;

    extern Settings g_settings;
    float final_vol = vol_factor * g_settings.volume;
    if (final_vol < 0.05f) final_vol = 0.05f; // Baseline minimale efficace

    float mix[12] = {0};
    mix[0] = final_vol; // Gauche
    mix[1] = final_vol; // Droite
    ndspChnSetMix(WN_CHANNEL, mix);
}

void audio_start_white_noise(void)
{
    bool hp = false;
    if (R_SUCCEEDED(DSP_GetHeadphoneStatus(&hp)) && !hp) {
        return; // Pas de Jack -> silence total sur les haut-parleurs internes !
    }

    if (s_wn_active) return;

    if (!s_wn_buf) {
        s_wn_buf = (s16*)linearAlloc(WN_BUF_SAMPLES * 2 * sizeof(s16));
        if (!s_wn_buf) return;

        #ifndef M_PI
        #define M_PI 3.14159265358979323846
        #endif

        u32 seed = 12345;
        for (int i = 0; i < WN_BUF_SAMPLES; i++) {
            // Frequences coherentes avec la taille du buffer (4096) pour un bouclage sans AUCUN clic numerique :
            // sub = f_ech / 4096 * 2  = 21.53 Hz (exactement 2 cycles complets)
            float sub = sinf(2.0f * (float)M_PI * 2.0f * (float)i / 4096.0f);

            // high = f_ech / 4096 * 1672 = 18001.75 Hz (exactement 1672 cycles complets)
            float high = sinf(2.0f * (float)M_PI * 1672.0f * (float)i / 4096.0f);

            // Modulation lente LFO (f_ech / 4096 * 1 = 10.76 Hz) pour berner les DSP intelligents anti-ton statique
            float lfo = 0.7f + 0.3f * sinf(2.0f * (float)M_PI * 1.0f * (float)i / 4096.0f);

            // Leger bruit blanc de fond pour exciter l'ensemble du spectre
            seed = seed * 1103515245 + 12345;
            float noise = (float)(((seed >> 16) & 0x7FFF) - 16384) / 16384.f;

            // Mix de survie Bluetooth ultime : 65% Sub-bass module, 25% Ultra-high, 10% Bruit blanc
            float signal = (0.65f * sub * lfo) + (0.25f * high) + (0.10f * noise);

            // Amplitude cible de 350 (+/- 350 sur 32767) -> Totalement inaudible mais suffisant pour le matériel
            s16 sample_val = (s16)(signal * 350.0f);

            s_wn_buf[i * 2]     = sample_val; // Gauche
            s_wn_buf[i * 2 + 1] = sample_val; // Droite
        }
        GSPGPU_FlushDataCache(s_wn_buf, WN_BUF_SAMPLES * 2 * sizeof(s16));
    }

    ndspChnReset(WN_CHANNEL);
    ndspChnSetInterp(WN_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(WN_CHANNEL, 44100.f);
    ndspChnSetFormat(WN_CHANNEL, NDSP_FORMAT_STEREO_PCM16);

    s_wn_active = true;
    audio_update_white_noise_volume();

    memset(&s_wn_wavebuf, 0, sizeof(s_wn_wavebuf));
    s_wn_wavebuf.data_vaddr = s_wn_buf;
    s_wn_wavebuf.nsamples = WN_BUF_SAMPLES;
    s_wn_wavebuf.looping = true;
    s_wn_wavebuf.status = NDSP_WBUF_FREE;

    DSP_FlushDataCache(s_wn_buf, WN_BUF_SAMPLES * 2 * sizeof(s16));
    ndspChnWaveBufAdd(WN_CHANNEL, &s_wn_wavebuf);
    ndspChnSetPaused(WN_CHANNEL, false);
}

void audio_stop_white_noise(void)
{
    if (!s_wn_active) return;
    ndspChnSetPaused(WN_CHANNEL, true);
    ndspChnWaveBufClear(WN_CHANNEL);
    s_wn_active = false;
}

int audio_is_white_noise_active(void)
{
    return s_wn_active ? 1 : 0;
}

/* Effet sonore de confirmation UI (Ping a 880Hz) */
#include <math.h>
void audio_play_beep(void)
{
    static s16 *s_beep_buf = NULL;
    static ndspWaveBuf s_beep_wavebuf;
    int samples = 8820;

    if (!s_beep_buf) {
        s_beep_buf = (s16*)linearAlloc(samples * 2 * sizeof(s16));
        if (!s_beep_buf) return;

        for (int i = 0; i < samples; i++) {
            float t = (float)i / 44100.0f;
            float env = 1.0f - (t / 0.20f);
            if (env < 0.0f) env = 0.0f;
            s16 val = (s16)(8000.0f * sinf(2.0f * 3.14159265f * 880.0f * t) * env);
            s_beep_buf[i * 2]     = val;
            s_beep_buf[i * 2 + 1] = val;
        }
        GSPGPU_FlushDataCache(s_beep_buf, samples * 2 * sizeof(s16));
    }

    ndspChnReset(9);
    ndspChnSetInterp(9, NDSP_INTERP_LINEAR);
    ndspChnSetRate(9, 44100.f);
    ndspChnSetFormat(9, NDSP_FORMAT_STEREO_PCM16);

    float mix[12] = {0.40f, 0.40f};
    ndspChnSetMix(9, mix);

    memset(&s_beep_wavebuf, 0, sizeof(s_beep_wavebuf));
    s_beep_wavebuf.data_vaddr = s_beep_buf;
    s_beep_wavebuf.nsamples = samples;
    s_beep_wavebuf.looping = false;
    s_beep_wavebuf.status = NDSP_WBUF_FREE;

    DSP_FlushDataCache(s_beep_buf, samples * 2 * sizeof(s16));
    ndspChnWaveBufAdd(9, &s_beep_wavebuf);
    ndspChnSetPaused(9, false);
}

void audio_update_wobble(void)
{
    extern Settings g_settings;
    if (s_state != AUDIO_PLAYING) return;

    static float phase = 0.0f;
    static float saved_speed = -1.0f;

    if (g_settings.distortion > 0) {
        // Sauvegarder la vitesse fun d'origine une seule fois
        if (saved_speed < 0.0f) {
            saved_speed = (g_settings.playback_speed > 0.01f) ? g_settings.playback_speed : 1.0f;
        }

        // Oscillateur de phase à 4.5 Hz (incrémentation par frame à 60 FPS)
        phase += 0.4712389f;
        if (phase > 6.2831853f) phase -= 6.2831853f;
        float sine = sinf(phase);

        // Profondeur : 5% = +/-5%, 100% = +/-90%
        float depth = (float)g_settings.distortion / 100.0f * 0.90f;
        float modulated_speed = saved_speed * (1.0f + sine * depth);

        // Appliquer directement via audio_set_speed qui fait ndspChnSetRate
        // On passe par s_speed directement pour ne pas modifier playback_speed sauvegardé
        float pitch_ratio = 1.0f;
        extern bool g_is_new3ds;
        if (g_is_new3ds && g_settings.pitch_semitones != 0)
            pitch_ratio = powf(2.f, (float)g_settings.pitch_semitones / 12.f);
        float new_rate = (float)(s_rs_native ? 48000 : TARGET_RATE) * modulated_speed * pitch_ratio;
        if (new_rate < 4000.f) new_rate = 4000.f;
        if (new_rate > 96000.f) new_rate = 96000.f;
        ndspChnSetRate(NDSP_CHANNEL, new_rate);
    } else {
        // Restaurer la vitesse d'origine quand la distorsion est coupée
        if (saved_speed >= 0.0f) {
            float pitch_ratio = 1.0f;
            extern bool g_is_new3ds;
            if (g_is_new3ds && g_settings.pitch_semitones != 0)
                pitch_ratio = powf(2.f, (float)g_settings.pitch_semitones / 12.f);
            float new_rate = (float)(s_rs_native ? 48000 : TARGET_RATE) * saved_speed * pitch_ratio;
            ndspChnSetRate(NDSP_CHANNEL, new_rate);
            saved_speed = -1.0f;
            phase = 0.0f;
        }
    }
}
