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
#include "mp4_aac.h"          /* AAC12 : peek metas explorateur */

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
    if (fread(hdr, 1, 10, f) != 10 || memcmp(hdr, "ID3", 3) != 0) {
        fclose(f);
        return NULL;
    }

    u32 tag_size = ((hdr[6] & 0x7F) << 21) | ((hdr[7] & 0x7F) << 14) | ((hdr[8] & 0x7F) << 7) | (hdr[9] & 0x7F);
    if (tag_size == 0 || tag_size > 10 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    u8 *tag_buf = (u8*)malloc(tag_size);
    if (!tag_buf) { fclose(f); return NULL; }

    size_t read_bytes = fread(tag_buf, 1, tag_size, f);
    fclose(f);

    if (read_bytes < tag_size) {
        free(tag_buf);
        return NULL;
    }

    u8 *img_start = NULL;
    u32 img_size = 0;

    for (u32 j = 0; j < read_bytes - 4; j++) {
        /* Magic JPEG: FF D8 FF */
        if (tag_buf[j] == 0xFF && tag_buf[j+1] == 0xD8 && tag_buf[j+2] == 0xFF) {
            img_start = tag_buf + j;
            img_size = (u32)(read_bytes - j);
            break;
        }
        /* Magic PNG: 89 50 4E 47 */
        if (tag_buf[j] == 0x89 && tag_buf[j+1] == 'P' && tag_buf[j+2] == 'N' && tag_buf[j+3] == 'G') {
            img_start = tag_buf + j;
            img_size = (u32)(read_bytes - j);
            break;
        }
    }

    if (img_start && img_size > 0) {
        u8 *ret_buf = (u8*)malloc(img_size);
        if (ret_buf) {
            memcpy(ret_buf, img_start, img_size);
            *out_size = img_size;
            free(tag_buf);
            return ret_buf;
        }
    }

    free(tag_buf);
    return NULL;
}

typedef enum {
    PF_UNKNOWN, PF_OGG, PF_WAV, PF_FLAC, PF_MP3, PF_OPUS, PF_M4A
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
    if (0==strcasecmp(e,"m4a") || 0==strcasecmp(e,"mp4") || 0==strcasecmp(e,"aac"))  return PF_M4A;   /* AAC15 */
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


extern int mp4_aac_peek(const char *path, char *title, char *artist, char *album, int cap, u8 **cover, u32 *cover_size, u32 *rate, u32 *channels, u32 *duration_sec);   /* AAC12c */
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
        case PF_M4A: {
            /* AAC13 : MP4/m4a/aac via mp4_aac_peek (reentrant) */
            uint32_t rr = 0, cc = 0, dd = 0;
            int okp = mp4_aac_peek(path, p->title, p->artist, p->album, 256,
                                   NULL, NULL, &rr, &cc, &dd);
            if (!okp) { p->valid = false; p->loaded = true; return; }
            p->sample_rate = (int)rr;
            p->channels    = (int)cc;
            p->duration_sec = (int)dd;
            if (p->duration_sec > 0)
                p->bitrate_kbps = (int)((p->file_size * 8) / (p->duration_sec * 1000));
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

/* ================================================================
   ND19 : pochettes FLAC/OGG/OPUS (explorateur + lecteur).
   FLAC : scan manuel des blocs metadata, type 6 = PICTURE.
   OGG/OPUS : commentaire METADATA_BLOCK_PICTURE en base64.
   ================================================================ */
static int nd19_b64val(char c)
{ if (c>='A'&&c<='Z') return c-'A'; if (c>='a'&&c<='z') return c-'a'+26;
  if (c>='0'&&c<='9') return c-'0'+52; if (c=='+') return 62; if (c=='/') return 63; return -1; }

static void *nd19_b64_decode(const char *s, uint32_t slen, uint32_t *outlen)
{
    uint32_t cap = slen/4*3 + 4, pos = 0, i;
    int acc = 0, nbits = 0;
    uint8_t *out = (uint8_t *)malloc(cap);
    if (!out) return NULL;
    for (i = 0; i < slen; i++) {
        int v = nd19_b64val(s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | v; nbits += 6;
        if (nbits >= 8) { nbits -= 8; out[pos++] = (uint8_t)((acc >> nbits) & 0xFF); }
    }
    *outlen = pos;
    return out;
}

static void *nd19_picture_body(const uint8_t *p, uint32_t len, uint32_t *imglen)
{
    uint32_t pos, ml, dl, dl2;
    if (len < 36 || !imglen) return NULL;
    ml = ((uint32_t)p[4]<<24)|((uint32_t)p[5]<<16)|((uint32_t)p[6]<<8)|p[7];
    pos = 8;
    if (pos + ml + 4 > len) return NULL;
    pos += ml;
    dl = ((uint32_t)p[pos]<<24)|((uint32_t)p[pos+1]<<16)|((uint32_t)p[pos+2]<<8)|p[pos+3];
    pos += 4;
    if (pos + dl + 20 > len) return NULL;
    pos += dl;
    pos += 16;
    dl2 = ((uint32_t)p[pos]<<24)|((uint32_t)p[pos+1]<<16)|((uint32_t)p[pos+2]<<8)|p[pos+3];
    pos += 4;
    if (pos + dl2 > len) return NULL;
    *imglen = dl2;
    return (void *)(p + pos);
}

static stb_vorbis_comment nd19_sc;
static stb_vorbis *nd19_lv = NULL;
static int nd19_ci = 0;
static char *nd19_ogg_next(stb_vorbis *v)
{
    if (v != nd19_lv) { nd19_lv = v; nd19_sc = stb_vorbis_get_comment(v); nd19_ci = 0; }
    if (nd19_ci >= nd19_sc.comment_list_length) { nd19_lv = NULL; return NULL; }
    return nd19_sc.comment_list[nd19_ci++];
}
void *nd19_flac_picture(const char *path, uint32_t *imglen)
{
    FILE *f = fopen(path, "rb");
    uint8_t hdr[4];
    uint32_t guard = 0;
    void *img = NULL;
    if (!f) return NULL;
    if (fread(hdr, 1, 4, f) != 4 || memcmp(hdr, "fLaC", 4) != 0) { fclose(f); return NULL; }
    while (guard++ < 128) {
        uint8_t bh[4];
        uint32_t blen, btype;
        if (fread(bh, 1, 4, f) != 4) break;
        btype = bh[0] & 0x7F;
        blen = ((uint32_t)bh[1]<<16)|((uint32_t)bh[2]<<8)|bh[3];
        if (blen == 0 || blen > 16u*1024u*1024u) break;
        if (btype == 6) {
            uint8_t *body = (uint8_t *)malloc(blen);
            if (!body) break;
            if (fread(body, 1, blen, f) == blen) {
                uint32_t il = 0;
                void *pp = nd19_picture_body(body, blen, &il);
                if (pp && il > 0 && il < 8u*1024u*1024u) {
                    img = malloc(il);
                    if (img) { memcpy(img, pp, il); *imglen = il; }
                }
            }
            free(body);
            if (img) { nd19_lv = NULL; break; }
        } else {
            if (fseek(f, (long)blen, SEEK_CUR)) break;
        }
        if (bh[0] & 0x80) break;
    }
    fclose(f);
    return img;
}

static void *nd19_ogg_picture(const char *path, uint32_t *imglen)
{
    int err = 0;
    void *img = NULL;
    stb_vorbis *v = stb_vorbis_open_filename(path, &err, NULL);
    if (!v) return NULL;
    for (;;) {
        char *c = NULL;
        c = nd19_ogg_next(v); if (!c) break;
        if (strncasecmp(c, "METADATA_BLOCK_PICTURE=", 23) == 0) {
            uint32_t bl = 0, il = 0;
            void *blk = nd19_b64_decode(c + 23, (uint32_t)strlen(c + 23), &bl);
            if (blk) {
                void *pp = nd19_picture_body((const uint8_t *)blk, bl, &il);
                if (pp && il > 0 && il < 8u*1024u*1024u) {
                    img = malloc(il);
                    if (img) { memcpy(img, pp, il); *imglen = il; }
                }
                free(blk);
            }
            if (img) { nd19_lv = NULL; break; }
        }
    }
    stb_vorbis_close(v);
    return img;
}

void *nd19_opus_picture(const char *path, uint32_t *imglen)
{
    int err = 0;
    void *img = NULL;
    OggOpusFile *of = op_open_file(path, &err);
    if (!of) return NULL;
    {
        const OpusTags *tags = op_tags(of, -1);
        if (tags) {
            const char *vv = opus_tags_query(tags, "METADATA_BLOCK_PICTURE", 0);
            if (vv) {
                uint32_t bl = 0, il = 0;
                void *blk = nd19_b64_decode(vv, (uint32_t)strlen(vv), &bl);
                if (blk) {
                    void *pp = nd19_picture_body((const uint8_t *)blk, bl, &il);
                    if (pp && il > 0 && il < 8u*1024u*1024u) {
                        img = malloc(il);
                        if (img) { memcpy(img, pp, il); *imglen = il; }
                    }
                    free(blk);
                }
            }
        }
    }
    op_free(of);
    return img;
}

/* ================================================================
   ND21 : pochette WAV = chunk "ID3 " dans le RIFF (comme VLC).
   Parse ID3v2.3/2.4 -> frame APIC -> donnees image brutes.
   ================================================================ */
static void *nd21_id3_apic(const uint8_t *d, uint32_t len, uint32_t *imglen)
{
    uint32_t pos, tsize;
    int ver;
    if (len < 10 || d[0] != 'I' || d[1] != 'D' || d[2] != '3') return NULL;
    if (d[5] & 0x80) return NULL;              /* unsync global : non supporte */
    ver = d[3];
    tsize = ((uint32_t)(d[6]&0x7F)<<21)|((uint32_t)(d[7]&0x7F)<<14)|
            ((uint32_t)(d[8]&0x7F)<<7)|(uint32_t)(d[9]&0x7F);
    if (10 + tsize > len) tsize = len - 10;
    pos = 10;
    while (pos + 10 <= 10 + tsize) {
        uint32_t fsz, o, fl;
        const uint8_t *f;
        uint8_t enc;
        if (d[pos] == 0) break;
        if (ver == 4)
            fsz = ((uint32_t)(d[pos+4]&0x7F)<<21)|((uint32_t)(d[pos+5]&0x7F)<<14)|
                  ((uint32_t)(d[pos+6]&0x7F)<<7)|(uint32_t)(d[pos+7]&0x7F);
        else
            fsz = ((uint32_t)d[pos+4]<<24)|((uint32_t)d[pos+5]<<16)|
                  ((uint32_t)d[pos+6]<<8)|d[pos+7];
        if (fsz == 0 || pos + 10 + fsz > 10 + tsize) break;
        if (d[pos]=='A' && d[pos+1]=='P' && d[pos+2]=='I' && d[pos+3]=='C') {
            f = d + pos + 10; fl = fsz;
            enc = f[0]; o = 1;
            while (o < fl && f[o]) o++;        /* mime nul-termine */
            o++;
            if (o + 1 >= fl) return NULL;
            o++;                                /* picture type */
            if (enc == 1 || enc == 2) {         /* UTF-16 : desc double-nul */
                while (o + 1 < fl && (f[o] != 0 || f[o+1] != 0)) o += 2;
                o += 2;
            } else {
                while (o < fl && f[o]) o++;
                o++;
            }
            if (o >= fl) return NULL;
            *imglen = fl - o;
            return (void *)(f + o);
        }
        pos += 10 + fsz;
    }
    return NULL;
}

void *nd21_wav_picture(const char *path, uint32_t *imglen)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[12];
    uint32_t guard = 0;
    void *img = NULL;
    if (!f) return NULL;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fclose(f); return NULL;
    }
    while (guard++ < 256) {
        uint8_t cb[8];
        uint32_t csz;
        if (fread(cb, 1, 8, f) != 8) break;
        csz = (uint32_t)cb[4] | ((uint32_t)cb[5]<<8) | ((uint32_t)cb[6]<<16) | ((uint32_t)cb[7]<<24);
        if (csz > 64u*1024u*1024u) break;
        if (memcmp(cb, "ID3 ", 4) == 0 || memcmp(cb, "id3 ", 4) == 0) {   /* ND22a */
            uint8_t *buf = (uint8_t *)malloc(csz);
            if (!buf) break;
            if (fread(buf, 1, csz, f) == csz) {
                uint32_t il = 0;
                void *pp = nd21_id3_apic(buf, csz, &il);
                if (pp && il > 0 && il < 8u*1024u*1024u) {
                    img = malloc(il);
                    if (img) { memcpy(img, pp, il); *imglen = il; }
                }
            }
            free(buf);
            if (img) break;
        } else {
            if (fseek(f, (long)((csz + 1u) & ~1u), SEEK_CUR)) break;  /* pad pair */
        }
        if (feof(f)) break;
    }
    fclose(f);
    return img;
}

static void process_cover_for_slot(int slot, const char *path)
{
    char fmt_ext[8];
    PreviewFormat fmt = detect_format(path, fmt_ext);
    u32 cover_size = 0;
    u8 *cover_data = NULL;
    if (fmt == PF_MP3) {
        cover_data = extract_id3v2_cover_data(path, &cover_size);
    } else if (fmt == PF_M4A) {
        /* AAC12 : pochette embarquee covr (MP4) ; ADTS = aucune */
        mp4_aac_peek(path, NULL, NULL, NULL, 0, &cover_data, &cover_size,
                     NULL, NULL, NULL);
    } else if (fmt == PF_FLAC) {
        /* ND19 : bloc PICTURE FLAC */
        cover_data = (u8 *)nd19_flac_picture(path, &cover_size);
    } else if (fmt == PF_OGG) {
        /* ND19 : METADATA_BLOCK_PICTURE base64 (vorbis) */
        cover_data = (u8 *)nd19_ogg_picture(path, &cover_size);
    } else if (fmt == PF_OPUS) {
        /* ND19 : METADATA_BLOCK_PICTURE base64 (opus) */
        cover_data = (u8 *)nd19_opus_picture(path, &cover_size);
    } else if (fmt == PF_WAV) {
        /* ND21 : chunk ID3 dans RIFF (comme VLC) */
        cover_data = (u8 *)nd21_wav_picture(path, &cover_size);
    } else return;
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
    s_thread = threadCreate(worker_thread, NULL, 128*1024, 0x3F, -1, false);
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
