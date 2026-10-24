/* mp4_aac.c - ND17 v2 (AAC5) : decodeur MP4/M4A/AAC pour 3DSoundShell
 *
 * API : include/mp4_aac.h (header d'origine de l'utilisateur).
 * Demux : minimp4.h (domaine public, lieff/minimp4)  -> frames AAC brutes
 * Decode : libhelix-aac (RealNetworks Helix, virgule fixe, optimise ARM)
 * Sortie : PCM s16 stereo entrelace (NDSP_FORMAT_STEREO_PCM16)
 *
 * v2 : cache sequentiel 64 Ko (1 acces SD par 64 Ko au lieu de 43/s) +
 *      decode direct dans le buffer pending + mono->stereo.
 * v6 (AAC10) : (1) tags iTunes moov/udta/meta/ilst -> title/artist/
 *      album/covr (pochette) ; (2) duree recalculee depuis sample_count
 *      (stsz) au lieu du mdhd (mdhd menteur sur fichiers mal muxes =
 *      position qui depasse la duree affichee).
 * v5 (AAC8) : estimation de la DUREE des .aac ADTS par scan des
 *      en-tetes de frames. Sans duree (0), la logique xfade/fin-de-
 *      piste d'audio.c declenche la fin des la 1re seconde = freeze.
 * v4 (AAC7) : fallback ADTS - les .aac bruts ne sont PAS des MP4 ;
 *      MP4D_open echoue -> mode ADTS (helix detecte les sync words lui-
 *      meme). Sans ca : .aac = open NULL = freeze de l'app.
 * v3 (AAC6) : FIX CRITIQUE - helix AACGetLastFrameInfo renvoie
 *      outputSamps = TOTAL int16 (nChans * nsampOut * sbr), PAS le nombre
 *      de paires par canal. v1/v2 le traitaient comme des paires -> la
 *      moitie de chaque frame etait du PCM perime = glitches/lag audio.
 *      Desormais : paires = outputSamps / nChans.
 */
#include "mp4_aac.h"

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"
#include "aacdec.h"

#define AAC_OTI_AAC      0x40   /* MPEG-4 AAC (object_type_indication) */
#define AAC_HANDLER_SOUN 0x736F756Eu  /* 'soun' */
#define AAC_FRAME_CAP    16384  /* taille max d'une frame AAC codee */
#define AAC_PCM_CAP      4096   /* s16 : 2048 echantillons stereo max */
#define RD_CACHE         65536  /* cache sequentiel pour le mdat */

/* ---- etat interne (une seule lecture AAC a la fois, comme audio.c) ---- */
static MP4D_demux_t  s_mp4;
static int           s_mp4_open   = 0;
static unsigned      s_track      = 0;
static HAACDecoder   s_haac       = NULL;
static unsigned      s_cur        = 0;     /* index de frame courante */
static unsigned      s_frame_samps = 1024; /* echantillons par frame (1024/2048 SBR) */
static unsigned      s_adts_bpf   = 0;     /* AAC11 : octets/frame ADTS moyens (seek) */
static uint8_t       s_frame_buf[AAC_FRAME_CAP];  /* frame AAC codee */
static int16_t       s_pend[AAC_PCM_CAP];  /* PCM de la frame courante */
static int           s_pend_n = 0, s_pend_i = 0;

/* ---- v4 : mode ADTS (.aac brut, pas de conteneur MP4) ---- */
static int           s_adts     = 0;
static long          s_adts_pos = 0;   /* prochain octet a decoder */

/* ---- cache sequentiel (lecture des frames AAC depuis la SD) ---- */
static uint8_t       s_cache[RD_CACHE];
static long          s_cache_off  = -1;   /* offset fichier du debut du cache */
static int           s_cache_len  = 0;    /* octets valides dans le cache */
static FILE         *s_cache_file = NULL;

static void cache_reset(void)
{
    s_cache_off  = -1;
    s_cache_len  = 0;
    s_cache_file = NULL;
}

/* lit n octets a l'offset off via le cache ; retourne 0 = succes */
static int read_cached(FILE *f, long off, uint8_t *dst, int n)
{
    if (n <= 0) return 0;
    if (f == s_cache_file && s_cache_off >= 0 &&
        off >= s_cache_off && off + n <= s_cache_off + s_cache_len) {
        memcpy(dst, s_cache + (off - s_cache_off), (size_t)n);  /* dans le cache */
        return 0;
    }
    if (n > RD_CACHE) {                    /* frame enorme : lecture directe */
        cache_reset();
        if (fseek(f, off, SEEK_SET) != 0) return -1;
        return (fread(dst, 1, (size_t)n, f) == (size_t)n) ? 0 : -1;
    }
    if (fseek(f, off, SEEK_SET) != 0) return -1;
    s_cache_off  = off;
    s_cache_len  = (int)fread(s_cache, 1, RD_CACHE, f);
    s_cache_file = f;
    if (n <= s_cache_len) { memcpy(dst, s_cache, (size_t)n); return 0; }
    cache_reset();                         /* a cheval sur la fin : direct */
    if (fseek(f, off, SEEK_SET) != 0) return -1;
    return (fread(dst, 1, (size_t)n, f) == (size_t)n) ? 0 : -1;
}

/* lit jusqu'a n octets a l'offset off ; retourne octets lus (0 = EOF, -1 = err) */
static int read_cached_partial(FILE *f, long off, uint8_t *dst, int n)
{
    if (n <= 0) return 0;
    if (f == s_cache_file && s_cache_off >= 0 &&
        off >= s_cache_off && off + n <= s_cache_off + s_cache_len) {
        memcpy(dst, s_cache + (off - s_cache_off), (size_t)n);
        return n;
    }
    if (fseek(f, off, SEEK_SET) != 0) return -1;
    s_cache_off  = off;
    s_cache_len  = (int)fread(s_cache, 1, RD_CACHE, f);
    s_cache_file = f;
    if (s_cache_len <= 0) { s_cache_len = 0; return 0; }
    if (n > s_cache_len) n = s_cache_len;
    memcpy(dst, s_cache, (size_t)n);
    return n;
}

/* ---- v6 : parsing des tags iTunes (moov/udta/meta/ilst) ---- */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int rd_at(FILE *f, long off, void *dst, int n)
{
    if (n <= 0) return -1;
    if (fseek(f, off, SEEK_SET) != 0) return -1;
    return ((int)fread(dst, 1, (size_t)n, f) == n) ? 0 : -1;
}

/* cherche la box 'want' parmi les enfants de [start,end[ ;
   retourne 0 si trouvee : *coff = debut contenu, *csz = taille contenu */
static int find_box(FILE *f, long start, long end, const char *want,
                    long *coff, long *csz)
{
    long pos = start;
    while (pos + 8 <= end) {
        uint8_t hdr[8];
        long long sz;
        long hoff;
        if (rd_at(f, pos, hdr, 8) != 0) return -1;
        sz  = (long long)be32(hdr);
        hoff = pos + 8;
        if (sz == 1) {                       /* taille 64 bits */
            uint8_t ext[8];
            if (rd_at(f, pos + 8, ext, 8) != 0) return -1;
            sz   = ((long long)be32(ext) << 32) | (long long)be32(ext + 4);
            hoff = pos + 16;
        } else if (sz == 0) {                /* jusqu'a la fin du parent */
            sz = (long long)(end - pos);
        }
        if (sz < 8 || pos + (long)sz > end) return -1;
        if (!memcmp(hdr + 4, want, 4)) {
            *coff = hoff;
            *csz  = (long)(pos + (long)sz - hoff);
            return 0;
        }
        pos += (long)sz;
    }
    return -1;
}

static void parse_tags(MP4AACDecoder *dec, long fsz)
{
    static const uint8_t K_NAM[4] = {0xA9,'n','a','m'};   /* titre  */
    static const uint8_t K_ART[4] = {0xA9,'A','R','T'};   /* artiste */
    static const uint8_t K_ALB[4] = {0xA9,'a','l','b'};   /* album  */
    long moov_o, moov_s, udta_o, udta_s, meta_o, meta_s, ilst_o, ilst_s;
    long pos, end;
    FILE *f = dec->file;

    if (find_box(f, 0, fsz, "moov", &moov_o, &moov_s)) return;
    if (find_box(f, moov_o, moov_o + moov_s, "udta", &udta_o, &udta_s)) return;
    if (find_box(f, udta_o, udta_o + udta_s, "meta", &meta_o, &meta_s)) return;
    meta_o += 4; meta_s -= 4;              /* meta = fullbox (version+flags) */
    if (meta_s <= 0) return;
    if (find_box(f, meta_o, meta_o + meta_s, "ilst", &ilst_o, &ilst_s)) return;

    pos = ilst_o; end = ilst_o + ilst_s;
    while (pos + 8 <= end) {
        uint8_t ihdr[8];
        long isz, doff, dsz, plen, poff;
        uint8_t dh[8];
        if (rd_at(f, pos, ihdr, 8) != 0) break;
        isz = (long)be32(ihdr);
        if (isz < 8 || pos + isz > end) break;
        if (find_box(f, pos + 8, pos + isz, "data", &doff, &dsz) == 0 && dsz > 8) {
            if (rd_at(f, doff, dh, 8) == 0) {   /* 4 octets flags + 4 locale */
                plen = dsz - 8;
                poff = doff + 8;
                if (plen > 0 && plen < 2 * 1024 * 1024) {
                    if (!memcmp(ihdr + 4, K_NAM, 4) ||
                        !memcmp(ihdr + 4, K_ART, 4) ||
                        !memcmp(ihdr + 4, K_ALB, 4)) {
                        char tmp[256];
                        int n = (plen > 255) ? 255 : (int)plen;
                        if (rd_at(f, poff, tmp, n) == 0) {
                            tmp[n] = 0;
                            if (!memcmp(ihdr + 4, K_NAM, 4))
                                memcpy(dec->title,  tmp, (size_t)n + 1);
                            else if (!memcmp(ihdr + 4, K_ART, 4))
                                memcpy(dec->artist, tmp, (size_t)n + 1);
                            else
                                memcpy(dec->album,  tmp, (size_t)n + 1);
                        }
                    } else if (!memcmp(ihdr + 4, "covr", 4) && !dec->cover_data) {
                        dec->cover_data = (uint8_t *)malloc((size_t)plen);
                        if (dec->cover_data) {
                            if (rd_at(f, poff, dec->cover_data, (int)plen) == 0)
                                dec->cover_size = (uint32_t)plen;
                            else { free(dec->cover_data); dec->cover_data = NULL; }
                        }
                    }
                }
            }
        }
        pos += isz;
    }
}

/* ---- callback lecture fichier pour minimp4 (0 = succes) ---- */
static int rd_cb(int64_t offset, void *buffer, size_t size, void *token)
{
    FILE *f = (FILE *)token;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return -1;
    return (fread(buffer, 1, size, f) == size) ? 0 : -1;
}

/* ---- v4 : decode une frame ADTS (.aac brut) -> s_pend ---- */
static int decode_one_adts(MP4AACDecoder *dec)
{
    uint8_t *ip;
    int left, avail, ret, tries = 0;
    AACFrameInfo fi;

    avail = read_cached_partial(dec->file, s_adts_pos, s_frame_buf, AAC_FRAME_CAP);
    if (avail < 8) return -1;                 /* EOF */

    ip = s_frame_buf; left = avail;
    ret = AACDecode(s_haac, &ip, &left, s_pend);
    while (ret != 0 && tries < 8) {           /* frame pourrie : resync */
        int consumed = avail - left;
        if (consumed > 0) s_adts_pos += consumed;
        else s_adts_pos += (avail > 67 ? avail - 64 : 1); /* saute le bloc */
        avail = read_cached_partial(dec->file, s_adts_pos, s_frame_buf, AAC_FRAME_CAP);
        if (avail < 8) return -1;
        ip = s_frame_buf; left = avail;
        ret = AACDecode(s_haac, &ip, &left, s_pend);
        tries++;
    }
    if (ret != 0) return -1;
    s_adts_pos += (int)(ip - s_frame_buf);

    AACGetLastFrameInfo(s_haac, &fi);
    if (fi.nChans > 0)
        dec->channels = (uint32_t)fi.nChans;
    if (fi.nChans > 0 && fi.outputSamps > 0) {
        int pairs = fi.outputSamps / fi.nChans;
        if (pairs > 0 && pairs <= AAC_PCM_CAP / 2) {
            s_pend_n = pairs;
            s_pend_i = 0;
            s_frame_samps = (unsigned)pairs;
        }
    }
    if (fi.sampRateOut > 0 && (uint32_t)fi.sampRateOut != dec->sample_rate)
        dec->sample_rate = (uint32_t)fi.sampRateOut;
    return 0;
}

/* ---- decode une frame -> s_pend ; met a jour rate/canaux (SBR) ---- */
static int decode_one(MP4AACDecoder *dec)
{
    MP4D_track_t *tr;

    if (s_adts) return decode_one_adts(dec);   /* v4 */
    unsigned bytes = 0, ts = 0, dur = 0;
    int64_t off;
    uint8_t *ip;
    int left, ret;
    AACFrameInfo fi;

    if (!s_mp4_open || s_track >= s_mp4.track_count) return -1;
    tr = &s_mp4.track[s_track];
    if (s_cur >= tr->sample_count) return -1;

    off = MP4D_frame_offset(&s_mp4, s_track, s_cur, &bytes, &ts, &dur);
    s_cur++;
    if (off < 0 || bytes == 0 || bytes > AAC_FRAME_CAP) return -1;
    if (read_cached(dec->file, (long)off, s_frame_buf, (int)bytes) != 0) return -1;

    ip = s_frame_buf; left = (int)bytes;
    ret = AACDecode(s_haac, &ip, &left, s_pend);
    if (ret != 0) return -1;

    AACGetLastFrameInfo(s_haac, &fi);
    if (fi.nChans > 0)
        dec->channels = (uint32_t)fi.nChans;
    /* v3 : outputSamps = total int16 (nChans*nsampOut*sbr) -> paires/canal */
    if (fi.nChans > 0 && fi.outputSamps > 0) {
        int pairs = fi.outputSamps / fi.nChans;
        if (pairs > 0 && pairs <= AAC_PCM_CAP / 2) {
            s_pend_n = pairs;
            s_pend_i = 0;
            s_frame_samps = (unsigned)pairs;
        }
    }
    /* SBR/HE-AAC : le taux de sortie reel peut etre le double du core */
    if (fi.sampRateOut > 0 && (uint32_t)fi.sampRateOut != dec->sample_rate)
        dec->sample_rate = (uint32_t)fi.sampRateOut;
    return 0;
}

MP4AACDecoder *mp4_aac_open(const char *path)
{
    MP4AACDecoder *dec;
    long fsz = 0;
    unsigned i;
    int found = -1;
    AACFrameInfo fi;

    dec = (MP4AACDecoder *)calloc(1, sizeof(MP4AACDecoder));
    if (!dec) return NULL;
    dec->file = fopen(path, "rb");
    if (!dec->file) { free(dec); return NULL; }
    setvbuf(dec->file, NULL, _IOFBF, 32768);   /* parsing moov plus rapide */
    fseek(dec->file, 0, SEEK_END);
    fsz = ftell(dec->file);
    fseek(dec->file, 0, SEEK_SET);
    if (fsz <= 0) { fclose(dec->file); free(dec); return NULL; }

    /* L etat du decodeur (index MP4 + instance Helix) tient dans des variables
       globales : si une ouverture precedente n a pas ete refermee, l ecraser
       ici perdrait definitivement sa memoire. On la libere donc d abord. */
    if (s_haac)     { AACFreeDecoder(s_haac); s_haac = NULL; }
    if (s_mp4_open) { MP4D_close(&s_mp4);     s_mp4_open = 0; }
    cache_reset();

    memset(&s_mp4, 0, sizeof(s_mp4));
    if (!MP4D_open(&s_mp4, rd_cb, dec->file, (int64_t)fsz)) {
        /* v4 : pas un MP4 -> flux ADTS brut (.aac) ? */
        s_adts     = 1;
        s_adts_pos = 0;
        dec->channels     = 2;
        dec->sample_rate  = 44100;   /* cale par la frame 0 ci-dessous */
        dec->duration_sec = 0;       /* inconnue sans scan complet */
        s_haac = AACInitDecoder();   /* defaut helix = format ADTS */
        if (!s_haac) { fclose(dec->file); free(dec); return NULL; }
        s_cur = 0; s_pend_n = s_pend_i = 0;
        if (decode_one(dec) != 0) {  /* ni MP4 ni ADTS valide */
            AACFreeDecoder(s_haac); s_haac = NULL;
            s_adts = 0;
            fclose(dec->file); free(dec); return NULL;
        }
        /* v5 : estimation duree/total par scan d'en-tetes ADTS (7 octets,
           frame_length 13 bits) sur 64 frames max, sans decoder. Ne touche
           ni s_pend ni s_adts_pos : la lecture reprend exactement ou elle
           en etait apres la frame 0. */
        dec->total_samples = 0;
        dec->duration_sec  = 0;
        {
            long pos = s_adts_pos;          /* juste apres la frame 0 */
            long long tb = 0;
            int nf = 0;
            uint8_t hdr[8];
            while (nf < 64) {
                unsigned flen;
                if (read_cached_partial(dec->file, pos, hdr, 7) < 7) break;
                if (hdr[0] != 0xFF || (hdr[1] & 0xF0) != 0xF0) break;
                flen = ((unsigned)(hdr[3] & 0x03) << 11) |
                       ((unsigned)hdr[4] << 3) | (unsigned)(hdr[5] >> 5);
                if (flen < 7 || flen > AAC_FRAME_CAP) break;
                tb += (long long)flen; nf++; pos += (long)flen;
            }
            if (nf >= 4 && tb > 0 && s_frame_samps > 0 && dec->sample_rate > 0) {
                s_adts_bpf = (unsigned)(tb / nf);
                unsigned long long rest = 0;
                if ((long long)fsz > (long long)pos)
                    rest = ((unsigned long long)(fsz - pos) *
                            (unsigned long long)nf *
                            (unsigned long long)s_frame_samps) / (unsigned long long)tb;
                dec->total_samples = (unsigned long long)(nf + 1) *
                                     (unsigned long long)s_frame_samps + rest;
                dec->duration_sec  = (uint32_t)(dec->total_samples /
                                                dec->sample_rate);
            }
        }
        return dec;
    }
    s_adts = 0;
    s_mp4_open = 1;

    for (i = 0; i < s_mp4.track_count; i++) {
        MP4D_track_t *tr = &s_mp4.track[i];
        if (tr->handler_type == AAC_HANDLER_SOUN &&
            tr->object_type_indication == AAC_OTI_AAC) { found = (int)i; break; }
    }
    if (found < 0) {   /* fallback : premiere piste audio quelle qu'elle soit */
        for (i = 0; i < s_mp4.track_count; i++) {
            if (s_mp4.track[i].handler_type == AAC_HANDLER_SOUN) { found = (int)i; break; }
        }
    }
    if (found < 0) { MP4D_close(&s_mp4); s_mp4_open = 0;
                     fclose(dec->file); free(dec); return NULL; }
    s_track = (unsigned)found;

    dec->channels    = s_mp4.track[s_track].SampleDescription.audio.channelcount;
    dec->sample_rate = s_mp4.track[s_track].SampleDescription.audio.samplerate_hz;
    if (s_mp4.track[s_track].timescale > 0) {
        uint64_t d = ((uint64_t)s_mp4.track[s_track].duration_hi << 32) |
                      s_mp4.track[s_track].duration_lo;
        dec->duration_sec = (uint32_t)(d / s_mp4.track[s_track].timescale);
    }
    if (dec->channels == 0) dec->channels = 2;
    if (dec->sample_rate == 0) dec->sample_rate = 44100;

    s_haac = AACInitDecoder();
    if (!s_haac) { MP4D_close(&s_mp4); s_mp4_open = 0;
                   fclose(dec->file); free(dec); return NULL; }
    memset(&fi, 0, sizeof(fi));
    fi.nChans       = (int)dec->channels;
    fi.sampRateCore = (int)dec->sample_rate;
    fi.sampRateOut  = (int)dec->sample_rate;
    fi.profile      = 2;            /* AAC-LC ; helix detecte SBR tout seul */
    AACSetRawBlockParams(s_haac, 0, &fi);

    s_cur = 0; s_pend_n = s_pend_i = 0;
    /* decode la frame 0 des maintenant : cale rate/canaux reels (SBR)
       et fournit le premier bloc PCM au premier read_pcm16 */
    if (decode_one(dec) != 0) { s_pend_n = s_pend_i = 0; }
    dec->total_samples = (uint64_t)s_mp4.track[s_track].sample_count *
                         (uint64_t)s_frame_samps;
    /* v6 : la duree qui compte = frames reelles decodees (stsz). Le mdhd
       est parfois faux (fichiers mal muxes) -> position > duree affichee. */
    if (dec->total_samples > 0 && dec->sample_rate > 0)
        dec->duration_sec = (uint32_t)(dec->total_samples / dec->sample_rate);
    parse_tags(dec, fsz);   /* v6 : titre/artiste/album/pochette (ilst) */
    return dec;
}

int mp4_aac_read_pcm16(MP4AACDecoder *dec, int16_t *buf, int max_pairs)
{
    int got = 0;
    if (!dec || !buf || max_pairs <= 0) return 0;
    while (got < max_pairs) {
        if (s_pend_i < s_pend_n) {
            int n = s_pend_n - s_pend_i;
            if (n > max_pairs - got) n = max_pairs - got;
            if (dec->channels == 1) {   /* mono -> stereo (NDSP attend 2 canaux) */
                int i;
                for (i = 0; i < n; i++) {
                    buf[(got + i) * 2]     = s_pend[s_pend_i + i];
                    buf[(got + i) * 2 + 1] = s_pend[s_pend_i + i];
                }
            } else {
                memcpy(buf + got * 2, s_pend + s_pend_i * 2, (size_t)n * 4);
            }
            s_pend_i += n;
            got += n;
            continue;
        }
        if (decode_one(dec) != 0) break;   /* fin de piste ou frame KO */
    }
    return got;
}

void mp4_aac_seek_sample(MP4AACDecoder *dec, uint64_t target_sample)
{
    (void)dec;
    (void)target_sample;
    /* AAC11/AAC4 : seek ADTS par estimation (bitrate moyen de l'open) */
    if (s_adts) {
        long fsz2 = 0, off = 0;
        int i, n;
        unsigned char rb[2048];
        if (s_adts_bpf == 0 || s_frame_samps == 0 || !dec->file) return;
        fseek(dec->file, 0, SEEK_END);
        fsz2 = ftell(dec->file);
        off = (long)((target_sample / s_frame_samps) * (uint64_t)s_adts_bpf);
        if (off > fsz2 - 8) off = (fsz2 > 8 ? fsz2 - 8 : 0);
        if (off < 0) off = 0;
        s_adts_pos = off;
        fseek(dec->file, s_adts_pos, SEEK_SET);
        cache_reset();
        s_pend_n = s_pend_i = 0;
        n = (int)fread(rb, 1, sizeof(rb), dec->file);
        for (i = 0; i + 1 < n; i++)
            if (rb[i] == 0xFF && (rb[i+1] & 0xF0) == 0xF0) break;
        if (i + 1 < n) {
            s_adts_pos += i;
            fseek(dec->file, s_adts_pos, SEEK_SET);
            cache_reset();
        }
        return;
    }
    if (!s_mp4_open || s_track >= s_mp4.track_count) return;
    if (s_frame_samps == 0) s_frame_samps = 1024;
    s_cur = (unsigned)(target_sample / s_frame_samps);
    if (s_cur > s_mp4.track[s_track].sample_count)
        s_cur = s_mp4.track[s_track].sample_count;
    s_pend_n = s_pend_i = 0;
    cache_reset();   /* saut : le cache sequentiel ne sert plus */
}

void mp4_aac_close(MP4AACDecoder *dec)
{
    if (s_haac) { AACFreeDecoder(s_haac); s_haac = NULL; }
    if (s_mp4_open) { MP4D_close(&s_mp4); s_mp4_open = 0; }
    s_pend_n = s_pend_i = 0;
    s_cur = 0;
    s_adts = 0; s_adts_pos = 0;
    cache_reset();
    if (dec) {
        if (dec->file) fclose(dec->file);
        if (dec->cover_data) free(dec->cover_data);
        free(dec);
    }
}
/* ================================================================
   AAC16 : lecture legere pour l'explorateur (preview.c).
   REENTRANT (tout en local) : peut tourner dans le thread preview
   pendant que le joueur lit un autre fichier. Ne decode que la
   frame 0 (frame_samps / SBR / rate reel) + tags ilst + covr.
   Retourne 1 si conteneur lisible (MP4 ou ADTS), 0 sinon.
   title/artist/album : ecrits UNIQUEMENT si le tag existe (ne
   casse pas le defaut appelant = nom de fichier).
   cover : malloc, a liberer par l'appelant. Tout ptr = NULL ok.
   ================================================================ */
int mp4_aac_peek(const char *path, char *title, char *artist, char *album, int cap,
                 uint8_t **cover, uint32_t *cover_size,
                 uint32_t *rate, uint32_t *channels, uint32_t *duration_sec)
{
    static const uint8_t K_NAM[4] = {0xA9,'n','a','m'};
    static const uint8_t K_ART[4] = {0xA9,'A','R','T'};
    static const uint8_t K_ALB[4] = {0xA9,'a','l','b'};
    static const unsigned ADTS_RATE[16] = {
        96000,88200,64000,48000,44100,32000,24000,22050,
        16000,12000,11025, 8000, 7350,    0,    0,    0 };
    FILE *f;
    long fsz;
    MP4D_demux_t mi;
    int tr = -1, ok = 0;
    unsigned i;
    uint32_t sr = 0, ch = 0, dur = 0;

    if (cover) *cover = NULL;
    if (cover_size) *cover_size = 0;
    if (rate) *rate = 0;
    if (channels) *channels = 0;
    if (duration_sec) *duration_sec = 0;

    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0) { fclose(f); return 0; }

    memset(&mi, 0, sizeof(mi));
    if (MP4D_open(&mi, rd_cb, f, (int64_t)fsz)) {
        /* ---- conteneur MP4 / m4a / mp4 ---- */
        unsigned fsamps = 1024;
        for (i = 0; i < mi.track_count; i++)
            if (mi.track[i].handler_type == AAC_HANDLER_SOUN &&
                mi.track[i].object_type_indication == AAC_OTI_AAC) { tr = (int)i; break; }
        if (tr < 0) {
            for (i = 0; i < mi.track_count; i++)
                if (mi.track[i].handler_type == AAC_HANDLER_SOUN) { tr = (int)i; break; }
        }
        if (tr >= 0) {
            HAACDecoder haac;
            ch = mi.track[tr].SampleDescription.audio.channelcount;
            sr = mi.track[tr].SampleDescription.audio.samplerate_hz;
            if (ch == 0) ch = 2;
            if (sr == 0) sr = 44100;
            haac = AACInitDecoder();
            if (haac) {
                unsigned bytes = 0, ts = 0, d2 = 0;
                int64_t off = MP4D_frame_offset(&mi, (unsigned)tr, 0,
                                                &bytes, &ts, &d2);
                if (off >= 0 && bytes > 0 && bytes <= AAC_FRAME_CAP) {
                    uint8_t fb[AAC_FRAME_CAP];
                    int16_t pcm[AAC_PCM_CAP];
                    uint8_t *ip = fb;
                    int left = (int)bytes;
                    if (rd_at(f, (long)off, fb, (int)bytes) == 0 &&
                        AACDecode(haac, &ip, &left, pcm) == 0) {
                        AACFrameInfo fi2;
                        AACGetLastFrameInfo(haac, &fi2);
                        if (fi2.nChans > 0) ch = (uint32_t)fi2.nChans;
                        if (fi2.nChans > 0 && fi2.outputSamps > 0) {
                            int pairs = fi2.outputSamps / fi2.nChans;
                            if (pairs > 0) fsamps = (unsigned)pairs;
                        }
                        if (fi2.sampRateOut > 0) sr = (uint32_t)fi2.sampRateOut;
                    }
                }
                AACFreeDecoder(haac);
            }
            if (sr > 0)
                dur = (uint32_t)((unsigned long long)mi.track[tr].sample_count *
                                 fsamps / sr);
            ok = 1;
        }
        /* ---- tags ilst + covr (scan local independant) ---- */
        {
            long moov_o, moov_s, udta_o, udta_s, meta_o, meta_s, ilst_o, ilst_s;
            if (find_box(f, 0, fsz, "moov", &moov_o, &moov_s) == 0 &&
                find_box(f, moov_o, moov_o + moov_s, "udta", &udta_o, &udta_s) == 0 &&
                find_box(f, udta_o, udta_o + udta_s, "meta", &meta_o, &meta_s) == 0) {
                meta_o += 4; meta_s -= 4;
                if (meta_s > 0 &&
                    find_box(f, meta_o, meta_o + meta_s, "ilst", &ilst_o, &ilst_s) == 0) {
                    long pos = ilst_o, end = ilst_o + ilst_s;
                    while (pos + 8 <= end) {
                        uint8_t ihdr[8];
                        long isz, doff, dsz, plen, poff;
                        uint8_t dh[8];
                        char *dst = NULL;
                        if (rd_at(f, pos, ihdr, 8) != 0) break;
                        isz = (long)be32(ihdr);
                        if (isz < 8 || pos + isz > end) break;
                        if (find_box(f, pos + 8, pos + isz, "data", &doff, &dsz) == 0
                            && dsz > 8 && rd_at(f, doff, dh, 8) == 0) {
                            plen = dsz - 8;
                            poff = doff + 8;
                            if (plen > 0 && plen < 2 * 1024 * 1024) {
                                if (!memcmp(ihdr + 4, K_NAM, 4))      dst = title;
                                else if (!memcmp(ihdr + 4, K_ART, 4)) dst = artist;
                                else if (!memcmp(ihdr + 4, K_ALB, 4)) dst = album;
                                if (dst && cap > 0) {
                                    char tmp[256];
                                    int n = (plen > 255) ? 255 : (int)plen;
                                    if (rd_at(f, poff, tmp, n) == 0) {
                                        tmp[n] = 0;
                                        snprintf(dst, (size_t)cap, "%s", tmp);
                                    }
                                }
                                if (cover && !*cover &&
                                    !memcmp(ihdr + 4, "covr", 4)) {
                                    uint8_t *cd = (uint8_t *)malloc((size_t)plen);
                                    if (cd) {
                                        if (rd_at(f, poff, cd, (int)plen) == 0) {
                                            *cover = cd;
                                            if (cover_size)
                                                *cover_size = (uint32_t)plen;
                                        } else free(cd);
                                    }
                                }
                            }
                        }
                        pos += isz;
                    }
                }
            }
        }
        MP4D_close(&mi);
    } else {
        /* ---- AAC16 : ADTS brut (.aac) : pas de tags, duree estimee.
           TOLERANT : scan du premier syncword 0xFFF dans les 8 premiers
           Ko (leading ID3, padding, entete decalee) au lieu de l'offset
           0 exact qui rejetait les fichiers reels. ---- */
        uint8_t hdr[8];
        long pos = 0, sync = -1;
        long long tb = 0;
        int nf = 0, i2, sn;
        static uint8_t sb[8192 + 8];
        fseek(f, 0, SEEK_SET);
        sn = (int)fread(sb, 1, sizeof(sb), f);
        for (i2 = 0; i2 + 1 < sn; i2++) {
            if (sb[i2] == 0xFF && (sb[i2 + 1] & 0xF0) == 0xF0) { sync = i2; break; }
        }
        if (sync >= 0) {
            unsigned idx = (sb[sync + 2] >> 2) & 0x0F;
            ch = (unsigned)(((sb[sync + 2] & 0x01) << 2) | ((sb[sync + 3] >> 6) & 0x03));
            sr = (idx < 16) ? ADTS_RATE[idx] : 0;
            if (ch == 0) ch = 2;
            if (sr == 0) sr = 44100;
            pos = sync;
            while (nf < 64) {
                unsigned flen;
                if (rd_at(f, pos, hdr, 7) != 0) break;
                if (hdr[0] != 0xFF || (hdr[1] & 0xF0) != 0xF0) break;
                flen = ((unsigned)(hdr[3] & 0x03) << 11) |
                       ((unsigned)hdr[4] << 3) | (unsigned)(hdr[5] >> 5);
                if (flen < 7 || flen > AAC_FRAME_CAP) break;
                tb += (long long)flen; nf++; pos += (long)flen;
            }
            if (nf >= 4 && tb > 0 && sr > 0) {
                unsigned long long rest = 0;
                if ((long long)fsz > (long long)pos)
                    rest = ((unsigned long long)(fsz - pos) *
                            (unsigned long long)nf * 1024ull) /
                           (unsigned long long)tb;
                dur = (uint32_t)(((unsigned long long)(nf + 1) * 1024ull + rest) / sr);
            }
            ok = 1;
        }
    }
    fclose(f);
    if (!ok) return 0;
    if (rate) *rate = sr;
    if (channels) *channels = ch;
    if (duration_sec) *duration_sec = dur;
    return 1;
}
