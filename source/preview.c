// preview.c - Chargement asynchrone des metadonnees
#include "preview.h"
#include "stb_image.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <strings.h>
#include <math.h>

/* IMPORTANT : NE PAS definir les MACROS _IMPLEMENTATION ici !
   Elles sont deja definies dans audio.c → sinon duplication au link */
/* stb_vorbis inclut son implementation par defaut
   → il FAUT dire explicitement de ne charger QUE le header */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.h"
#undef STB_VORBIS_HEADER_ONLY

/* dr_flac et dr_wav : NE PAS definir _IMPLEMENTATION ici
   (deja fait dans audio.c) */
#include "dr_flac.h"
#include "dr_wav.h"
#include <mpg123.h>
#include <opusfile.h>

static PreviewInfo   s_cache[PREVIEW_CACHE_SIZE];
static LightLock     s_cache_lock;
static bool          s_lock_init = false;

static char          s_pending_path[512] = {0};
static LightLock     s_pending_lock;
static LightEvent    s_pending_event;
static volatile bool s_thread_run = false;
static volatile bool s_active       = true;  /* V0.95: pause worker si player actif */
static Thread        s_thread = NULL;

typedef struct {
    int    slot;
    u8    *rgba;
    int    w, h;
    bool   ready;
} CoverUploadReq;
static CoverUploadReq s_upload_req[PREVIEW_CACHE_SIZE];
static LightLock      s_upload_lock;

static u32 npow2(u32 v)
{
    v--; v|=v>>1; v|=v>>2; v|=v>>4; v|=v>>8; v|=v>>16;
    return v+1;
}

static u32 morton(u32 x, u32 y)
{
    u32 r = 0;
    for (u32 i = 0; i < 4; i++)
        r |= ((y>>i&1)<<(2*i+1)) | ((x>>i&1)<<(2*i));
    return r;
}

static bool upload_cover_gpu(PreviewInfo *p, const u8 *rgba, int w, int h)
{
    if (p->cover_uploaded) {
        C3D_TexDelete(&p->cover_tex);
        p->cover_uploaded = false;
    }
    u32 tw = npow2((u32)w);
    u32 th = npow2((u32)h);
    if (!C3D_TexInit(&p->cover_tex, (u16)tw, (u16)th, GPU_RGBA8))
        return false;
    u8 *dst = (u8*)p->cover_tex.data;
    memset(dst, 0, tw * th * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u32 tile = ((u32)y/8)*(tw/8) + ((u32)x/8);
            u32 mi   = morton((u32)x&7, (u32)y&7);
            u32 di   = (tile*64 + mi) * 4;
            const u8 *s = rgba + (y*w + x) * 4;
            dst[di+0] = s[3];
            dst[di+1] = s[2];
            dst[di+2] = s[1];
            dst[di+3] = s[0];
        }
    }
    C3D_TexSetFilter(&p->cover_tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&p->cover_tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
    p->cover_subtex.width  = (u16)w;
    p->cover_subtex.height = (u16)h;
    p->cover_subtex.left   = 0.f;
    p->cover_subtex.top    = 1.f;
    p->cover_subtex.right  = (float)w / (float)tw;
    p->cover_subtex.bottom = 1.f - (float)h / (float)th;
    p->cover_uploaded = true;
    p->cover_w = w;
    p->cover_h = h;
    return true;
}

static u8 *extract_id3v2_cover_data(const char *path, u32 *out_size)
{
    *out_size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    u8 hdr[10];
    if (fread(hdr, 1, 10, f) != 10 || memcmp(hdr, "ID3", 3)) {
        fclose(f); return NULL;
    }
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
                u8 *img = malloc(isz);
                if (img) {
                    memcpy(img, data + off, isz);
                    *out_size = isz;
                    free(data);
                    fclose(f);
                    return img;
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
    return NULL;
}

typedef enum {
    PF_UNKNOWN, PF_OGG, PF_WAV, PF_FLAC, PF_MP3, PF_OPUS
} PreviewFormat;

static PreviewFormat detect_format(const char *path, char *out_ext)
{
    const char *e = strrchr(path, '.');
    if (!e) { out_ext[0] = 0; return PF_UNKNOWN; }
    e++;
    int i;
    for (i = 0; i < 7 && e[i]; i++) {
        char c = e[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out_ext[i] = c;
    }
    out_ext[i] = 0;

    if (!strcasecmp(e,"ogg") || !strcasecmp(e,"oga"))  return PF_OGG;
    if (!strcasecmp(e,"flac"))                          return PF_FLAC;
    if (!strcasecmp(e,"wav"))                           return PF_WAV;
    if (!strcasecmp(e,"mp3") || !strcasecmp(e,"mp2"))  return PF_MP3;
    if (!strcasecmp(e,"opus"))                          return PF_OPUS;
    return PF_UNKNOWN;
}

static int cache_find(const char *path)
{
    for (int i = 0; i < PREVIEW_CACHE_SIZE; i++) {
        if (s_cache[i].loaded && !strcmp(s_cache[i].path, path))
            return i;
    }
    return -1;
}

static int cache_lru_slot(void)
{
    int lru = 0;
    u64 min_time = (u64)-1;
    for (int i = 0; i < PREVIEW_CACHE_SIZE; i++) {
        if (!s_cache[i].loaded) return i;
        if (s_cache[i].last_used < min_time) {
            min_time = s_cache[i].last_used;
            lru = i;
        }
    }
    return lru;
}


static void load_metadata(PreviewInfo *p, const char *path)
{
    memset(p, 0, sizeof(PreviewInfo));
    strncpy(p->path, path, 511);
    p->last_used = osGetTime();

    const char *fn = strrchr(path, '/');
    fn = fn ? fn + 1 : path;
    strncpy(p->title, fn, 255);
    strncpy(p->artist, "Artiste inconnu", 255);
    strncpy(p->album,  "Album inconnu",   255);

    struct stat st;
    if (stat(path, &st) == 0) p->file_size = (u64)st.st_size;

    PreviewFormat fmt = detect_format(path, p->format);

    switch (fmt) {
        case PF_OGG: {
            int err = 0;
            stb_vorbis *v = stb_vorbis_open_filename(path, &err, NULL);
            if (!v) { p->valid = false; p->loaded = true; return; }
            stb_vorbis_info info = stb_vorbis_get_info(v);
            p->sample_rate = info.sample_rate;
            p->channels    = info.channels;
            int total = stb_vorbis_stream_length_in_samples(v);
            if (info.sample_rate > 0)
                p->duration_sec = total / info.sample_rate;
            if (p->duration_sec > 0)
                p->bitrate_kbps = (int)((p->file_size * 8) / (p->duration_sec * 1000));

            stb_vorbis_comment c = stb_vorbis_get_comment(v);
            for (int i = 0; i < c.comment_list_length; i++) {
                char *kv = c.comment_list[i];
                if (!strncasecmp(kv,"TITLE=",6))  snprintf(p->title,256,"%s",kv+6);
                if (!strncasecmp(kv,"ARTIST=",7)) snprintf(p->artist,256,"%s",kv+7);
                if (!strncasecmp(kv,"ALBUM=",6))  snprintf(p->album,256,"%s",kv+6);
                if (!strncasecmp(kv,"DATE=",5))   p->year = atoi(kv+5);
                if (!strncasecmp(kv,"GENRE=",6))  snprintf(p->genre,64,"%s",kv+6);
            }
            stb_vorbis_close(v);
            break;
        }
        case PF_WAV: {
            drwav w;
            if (!drwav_init_file(&w, path, NULL)) {
                p->valid = false; p->loaded = true; return;
            }
            p->sample_rate  = w.sampleRate;
            p->channels     = w.channels;
            p->duration_sec = (int)(w.totalPCMFrameCount / w.sampleRate);
            if (p->duration_sec > 0)
                p->bitrate_kbps = (int)((p->file_size * 8) / (p->duration_sec * 1000));
            drwav_uninit(&w);
            break;
        }
        case PF_FLAC: {
            drflac *fl = drflac_open_file(path, NULL);
            if (!fl) { p->valid = false; p->loaded = true; return; }
            p->sample_rate  = fl->sampleRate;
            p->channels     = fl->channels;
            if (fl->sampleRate > 0)
                p->duration_sec = (int)(fl->totalPCMFrameCount / fl->sampleRate);
            if (p->duration_sec > 0)
                p->bitrate_kbps = (int)((p->file_size * 8) / (p->duration_sec * 1000));
            drflac_close(fl);
            break;
        }
        case PF_MP3: {
            int err = 0;
            mpg123_handle *mh = mpg123_new(NULL, &err);
            if (!mh) { p->valid = false; p->loaded = true; return; }
            if (mpg123_open(mh, path) != MPG123_OK) {
                mpg123_delete(mh);
                p->valid = false; p->loaded = true; return;
            }
            long rate; int ch, enc;
            mpg123_getformat(mh, &rate, &ch, &enc);
            p->sample_rate = rate;
            p->channels    = ch;
            off_t len = mpg123_length(mh);
            if (len > 0 && rate > 0) p->duration_sec = (int)(len / rate);
            if (p->duration_sec > 0)
                p->bitrate_kbps = (int)((p->file_size * 8) / (p->duration_sec * 1000));

            mpg123_id3v1 *v1; mpg123_id3v2 *v2;
            if (mpg123_id3(mh, &v1, &v2) == MPG123_OK) {
                if (v2) {
                    if (v2->title)  snprintf(p->title,256,"%s",v2->title->p);
                    if (v2->artist) snprintf(p->artist,256,"%s",v2->artist->p);
                    if (v2->album)  snprintf(p->album,256,"%s",v2->album->p);
                    if (v2->genre)  snprintf(p->genre,64,"%s",v2->genre->p);
                    if (v2->year)   p->year = atoi(v2->year->p);
                } else if (v1) {
                    snprintf(p->title,256,"%s",v1->title);
                    snprintf(p->artist,256,"%s",v1->artist);
                    snprintf(p->album,256,"%s",v1->album);
                }
            }
            mpg123_close(mh);
            mpg123_delete(mh);
            break;
        }
        case PF_OPUS: {
            int err = 0;
            OggOpusFile *op = op_open_file(path, &err);
            if (!op) { p->valid = false; p->loaded = true; return; }
            p->sample_rate = 48000;
            p->channels    = 2;
            ogg_int64_t len = op_pcm_total(op, -1);
            if (len > 0) p->duration_sec = (int)(len / 48000);
            if (p->duration_sec > 0)
                p->bitrate_kbps = (int)((p->file_size * 8) / (p->duration_sec * 1000));
            const OpusTags *tags = op_tags(op, -1);
            if (tags) {
                const char *t = opus_tags_query(tags, "TITLE",  0);
                const char *a = opus_tags_query(tags, "ARTIST", 0);
                const char *b = opus_tags_query(tags, "ALBUM",  0);
                if (t) snprintf(p->title,256,"%s",t);
                if (a) snprintf(p->artist,256,"%s",a);
                if (b) snprintf(p->album,256,"%s",b);
            }
            op_free(op);
            break;
        }
        default:
            p->valid = false;
            p->loaded = true;
            return;
    }

    p->valid  = true;
    p->loaded = true;
    
}

static void process_cover_for_slot(int slot, const char *path)
{
    char fmt_ext[8];
    PreviewFormat fmt = detect_format(path, fmt_ext);
    if (fmt != PF_MP3) return;

    u32 cover_size = 0;
    u8 *cover_data = extract_id3v2_cover_data(path, &cover_size);
    if (!cover_data || cover_size == 0) {
        if (cover_data) free(cover_data);
        return;
    }

    int w, h, ch;
    u8 *px = stbi_load_from_memory(cover_data, (int)cover_size, &w, &h, &ch, 4);
    free(cover_data);
    if (!px) return;

    int tw = w, th_i = h;
    u8 *scaled = NULL;
    if (w > 80 || h > 80) {
        tw = 80; th_i = 80;
        scaled = malloc(80 * 80 * 4);
        if (scaled) {
            for (int dy = 0; dy < 80; dy++) {
                for (int dx = 0; dx < 80; dx++) {
                    int sx = dx * w / 80;
                    int sy = dy * h / 80;
                    if (sx >= w) sx = w-1;
                    if (sy >= h) sy = h-1;
                    memcpy(scaled + (dy*80+dx)*4,
                           px    + (sy*w+sx)*4, 4);
                }
            }
        }
    }

    LightLock_Lock(&s_upload_lock);
    if (s_upload_req[slot].rgba) free(s_upload_req[slot].rgba);
    s_upload_req[slot].slot  = slot;
    s_upload_req[slot].rgba  = scaled ? scaled : px;
    s_upload_req[slot].w     = tw;
    s_upload_req[slot].h     = th_i;
    s_upload_req[slot].ready = true;
    if (scaled) stbi_image_free(px);
    LightLock_Unlock(&s_upload_lock);

    LightLock_Lock(&s_cache_lock);
    s_cache[slot].has_cover = true;
    LightLock_Unlock(&s_cache_lock);
}

static void worker_thread(void *arg)
{
    (void)arg;
    while (s_thread_run) {
        /* V0.95 : si worker desactive (player actif), on dort et on ignore les demandes
           → aucun CPU consomme, l audio a toute la puissance */
        if (!s_active) {
            svcSleepThread(200000000LL); /* 200ms */
            continue;
        }

        /* Verifier si une demande est en attente AVANT de wait
           (evite de perdre un event signale pendant qu on traitait) */
        char path[512] = {0};
        LightLock_Lock(&s_pending_lock);
        if (s_pending_path[0]) {
            strncpy(path, s_pending_path, 511);
            path[511] = 0;
            s_pending_path[0] = 0;
        }
        LightLock_Unlock(&s_pending_lock);

        /* Si rien en attente → wait sur event (max 500ms) */
        if (!path[0]) {
            LightEvent_WaitTimeout(&s_pending_event, 500000000LL);
            if (!s_thread_run) break;
            continue;
        }
        if (!s_thread_run) break;

        
        LightLock_Lock(&s_cache_lock);
        int idx = cache_find(path);
        if (idx >= 0) {
            s_cache[idx].last_used = osGetTime();
            LightLock_Unlock(&s_cache_lock);
            
            continue;
        }
        int slot = cache_lru_slot();
        if (s_cache[slot].cover_uploaded) {
            C3D_TexDelete(&s_cache[slot].cover_tex);
            s_cache[slot].cover_uploaded = false;
        }
        memset(&s_cache[slot], 0, sizeof(PreviewInfo));
        strncpy(s_cache[slot].path, path, 511);
        LightLock_Unlock(&s_cache_lock);

        PreviewInfo tmp;
        load_metadata(&tmp, path);

        LightLock_Lock(&s_cache_lock);
        C3D_Tex saved_tex = s_cache[slot].cover_tex;
        Tex3DS_SubTexture saved_sub = s_cache[slot].cover_subtex;
        bool saved_uploaded = s_cache[slot].cover_uploaded;
        memcpy(&s_cache[slot], &tmp, sizeof(PreviewInfo));
        s_cache[slot].cover_tex      = saved_tex;
        s_cache[slot].cover_subtex   = saved_sub;
        s_cache[slot].cover_uploaded = saved_uploaded;
        LightLock_Unlock(&s_cache_lock);

        process_cover_for_slot(slot, path);

        svcSleepThread(50000000LL); /* 50ms pour ne pas gener l audio */
    }
}

void preview_init(void)
{
    memset(s_cache, 0, sizeof(s_cache));
    memset(s_upload_req, 0, sizeof(s_upload_req));
    if (!s_lock_init) {
        LightLock_Init(&s_cache_lock);
        LightLock_Init(&s_pending_lock);
        LightLock_Init(&s_upload_lock);
        LightEvent_Init(&s_pending_event, RESET_ONESHOT);
        s_lock_init = true;
    }
    s_thread_run = true;
    s_thread = threadCreate(worker_thread, NULL, 64*1024, 0x3F, -1, false);
}

void preview_exit(void)
{
    s_thread_run = false;
    LightEvent_Signal(&s_pending_event);
    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    for (int i = 0; i < PREVIEW_CACHE_SIZE; i++) {
        if (s_cache[i].cover_uploaded) {
            C3D_TexDelete(&s_cache[i].cover_tex);
            s_cache[i].cover_uploaded = false;
        }
        if (s_upload_req[i].rgba) {
            free(s_upload_req[i].rgba);
            s_upload_req[i].rgba = NULL;
        }
    }
}

void preview_request(const char *path)
{
    if (!path || !path[0]) return;
    
    /* Si le fichier est deja en cache et charge → rien a faire */
    LightLock_Lock(&s_cache_lock);
    int idx = cache_find(path);
    if (idx >= 0) {
        s_cache[idx].last_used = osGetTime();
        LightLock_Unlock(&s_cache_lock);
        return;
    }
    LightLock_Unlock(&s_cache_lock);

    /* Sinon on met la demande dans la queue */
    LightLock_Lock(&s_pending_lock);
    strncpy(s_pending_path, path, 511);
    s_pending_path[511] = 0;
    LightLock_Unlock(&s_pending_lock);
    LightEvent_Signal(&s_pending_event);
}

const PreviewInfo *preview_get(const char *path)
{
    if (!path || !path[0]) return NULL;
    LightLock_Lock(&s_cache_lock);
    int idx = cache_find(path);
    if (idx >= 0) {
        s_cache[idx].last_used = osGetTime();
        LightLock_Unlock(&s_cache_lock);
        return &s_cache[idx];
    }
    LightLock_Unlock(&s_cache_lock);
    return NULL;
}

void preview_set_active(bool active)
{
    s_active = active;
}

void preview_update_gpu(void)
{
    LightLock_Lock(&s_upload_lock);
    for (int i = 0; i < PREVIEW_CACHE_SIZE; i++) {
        if (s_upload_req[i].ready && s_upload_req[i].rgba) {
            int slot = s_upload_req[i].slot;
            if (slot >= 0 && slot < PREVIEW_CACHE_SIZE) {
                upload_cover_gpu(&s_cache[slot],
                    s_upload_req[i].rgba,
                    s_upload_req[i].w,
                    s_upload_req[i].h);
            }
            free(s_upload_req[i].rgba);
            s_upload_req[i].rgba  = NULL;
            s_upload_req[i].ready = false;
        }
    }
    LightLock_Unlock(&s_upload_lock);
}
