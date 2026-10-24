#include "power_save.h"
/* ============================================================
   navidrome.c - mode Navidrome ("app dans l'app")
   M1 : connexion (ND1..ND5) - M2 : navigation + telechargement (ND6)
   - Intro animee : logo centre, glissade vers le haut, etapes
   - Auth officielle : t = md5(mdp + salt), s = salt (mdp jamais expose)
   - HTTPS d'abord, repli HTTP auto (TLS 3DS vs Cloudflare ECDSA)
   - Etape WiFi = attente ASSOCIATION REELLE (statut >= 2, 15 s max)
   - M2 : menu Albums / Artistes / Morceaux, listes, fil d'ariane,
     A sur un morceau = telechargement SD (3DSoundShell/navidrome/)
     via .part + rename (jamais de fichier moitie ecrit)
   - Couleurs = theme custom de l'app (current_theme), version = APP_VERSION
   Entree : Parametres > Systeme > "Navidrome" (A) - Sortie : B/START
   MD5 embarque (RFC 1321) : aucune dependance externe.
   Aucun identifiant n'est affiche a l'ecran.
   Marqueurs : ND1, ND2, ND3, ND4, ND5, ND6, ND6G, ND7, ND8
   ============================================================ */
/* =========================================================================
   MODIFICATION FACILE DE LA VERSION DE L'APP
   Modifier la valeur ci-dessous pour changer le texte affiche à l'ecran
   ========================================================================= */
#define ND_VERSION " - V1.27"
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include "navidrome.h"

#include <strings.h>

/* Gravure ID3v2.3 APIC dans le MP3 */
static u8 s_tag_buf[16384]; /* Buffer BSS static pour eviter le stack overflow */

static void embed_cover_mp3_exact(const char *mp3_path, const u8 *img_data, size_t img_size)
{
    if (!mp3_path || !mp3_path[0] || !img_data || img_size == 0) return;

    const char *ext = strrchr(mp3_path, '.');
    if (!ext || (strcasecmp(ext, ".mp3") != 0 && strcasecmp(ext, ".MP3") != 0)) return;

    FILE *f_mp3 = fopen(mp3_path, "rb");
    if (!f_mp3) return;

    u8 hdr[10];
    size_t audio_offset = 0;
    if (fread(hdr, 1, 10, f_mp3) == 10 && memcmp(hdr, "ID3", 3) == 0) {
        u32 old_sz = (((u32)(hdr[6] & 0x7F) << 21) |
                      ((u32)(hdr[7] & 0x7F) << 14) |
                      ((u32)(hdr[8] & 0x7F) << 7)  |
                       (u32)(hdr[9] & 0x7F));
        audio_offset = 10 + old_sz;
    }
    fseek(f_mp3, (long)audio_offset, SEEK_SET);

    bool is_png = (img_size > 8 && memcmp(img_data, "\x89PNG", 4) == 0);
    const char *mime = is_png ? "image/png" : "image/jpeg";
    size_t mime_len = strlen(mime) + 1;

    size_t apic_payload_len = 1 + mime_len + 1 + 1 + img_size;
    size_t apic_frame_len   = 10 + apic_payload_len;
    size_t id3_size         = apic_frame_len;

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.id3tmp", mp3_path);
    FILE *f_out = fopen(tmp_path, "wb");
    if (!f_out) { fclose(f_mp3); return; }

    u8 s0 = (u8)((id3_size >> 21) & 0x7F);
    u8 s1 = (u8)((id3_size >> 14) & 0x7F);
    u8 s2 = (u8)((id3_size >> 7)  & 0x7F);
    u8 s3 = (u8)(id3_size        & 0x7F);
    u8 id3_hdr[10] = {'I', 'D', '3', 0x03, 0x00, 0x00, s0, s1, s2, s3};
    fwrite(id3_hdr, 1, 10, f_out);

    u8 apic_hdr[10] = {
        'A', 'P', 'I', 'C',
        (u8)((apic_payload_len >> 24) & 0xFF),
        (u8)((apic_payload_len >> 16) & 0xFF),
        (u8)((apic_payload_len >> 8)  & 0xFF),
        (u8)(apic_payload_len        & 0xFF),
        0x00, 0x00
    };
    fwrite(apic_hdr, 1, 10, f_out);

    u8 enc = 0x00, pic_type = 0x03, null_byte = 0x00;
    fwrite(&enc, 1, 1, f_out);
    fwrite(mime, 1, mime_len, f_out);
    fwrite(&pic_type, 1, 1, f_out);
    fwrite(&null_byte, 1, 1, f_out);
    fwrite(img_data, 1, img_size, f_out);

    /* 3DSS-FIX: Utilisation de s_dl_buf (static) au lieu du stack thread de 16 Ko */
    size_t n;
    while ((n = fread(s_tag_buf, 1, sizeof(s_tag_buf), f_mp3)) > 0) {
        fwrite(s_tag_buf, 1, n, f_out);
    }

    fclose(f_mp3);
    fclose(f_out);

    /* 3DSS-FIX: Suppression + temporisation 20ms avant renommage sur FAT 3DS */
    remove(mp3_path);
    svcSleepThread(20000000LL);
    if (rename(tmp_path, mp3_path) != 0) {
        remove(mp3_path);
        rename(tmp_path, mp3_path);
    }
}


/* Injecteur ID3v2.3 APIC : Grave la pochette directement DANS le fichier MP3 */
static void id3_embed_cover_mp3(const char *mp3_path, const u8 *img_data, size_t img_size)
{
    if (!mp3_path || !mp3_path[0] || !img_data || img_size == 0) return;

    const char *ext = strrchr(mp3_path, '.');
    if (!ext || (strcasecmp(ext, ".mp3") != 0 && strcasecmp(ext, ".MP3") != 0)) return;

    FILE *f_orig = fopen(mp3_path, "rb");
    if (!f_orig) return;

    u8 hdr[10];
    if (fread(hdr, 1, 10, f_orig) == 10) {
        if (memcmp(hdr, "ID3", 3) == 0) {
            fclose(f_orig);
            return;
        }
    }
    fseek(f_orig, 0, SEEK_SET);

    bool is_png = (img_size > 8 && memcmp(img_data, "\x89PNG", 4) == 0);
    const char *mime = is_png ? "image/png" : "image/jpeg";
    size_t mime_len = strlen(mime) + 1;

    size_t apic_payload_sz = 1 + mime_len + 1 + 1 + img_size;
    size_t apic_frame_sz   = 10 + apic_payload_sz;

    u32 tag_sz = (u32)apic_frame_sz;
    u8 synchsafe[4];
    synchsafe[0] = (u8)((tag_sz >> 21) & 0x7F);
    synchsafe[1] = (u8)((tag_sz >> 14) & 0x7F);
    synchsafe[2] = (u8)((tag_sz >> 7)  & 0x7F);
    synchsafe[3] = (u8)(tag_sz        & 0x7F);

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.id3tmp", mp3_path);
    FILE *f_out = fopen(tmp_path, "wb");
    if (!f_out) { fclose(f_orig); return; }

    u8 id3_hdr[10] = {'I', 'D', '3', 0x03, 0x00, 0x00, synchsafe[0], synchsafe[1], synchsafe[2], synchsafe[3]};
    fwrite(id3_hdr, 1, 10, f_out);

    u8 apic_hdr[10] = {'A', 'P', 'I', 'C',
                        (u8)((apic_payload_sz >> 24) & 0xFF),
                        (u8)((apic_payload_sz >> 16) & 0xFF),
                        (u8)((apic_payload_sz >> 8)  & 0xFF),
                        (u8)(apic_payload_sz        & 0xFF),
                        0x00, 0x00};
    fwrite(apic_hdr, 1, 10, f_out);

    u8 enc = 0x00;
    u8 pic_type = 0x03;
    u8 null_byte = 0x00;

    fwrite(&enc, 1, 1, f_out);
    fwrite(mime, 1, mime_len, f_out);
    fwrite(&pic_type, 1, 1, f_out);
    fwrite(&null_byte, 1, 1, f_out);
    fwrite(img_data, 1, img_size, f_out);

    u8 buf[16384];
    size_t bytes_read = 0;
    while ((bytes_read = fread(buf, 1, sizeof(buf), f_orig)) > 0) {
        fwrite(buf, 1, bytes_read, f_out);
    }

    fclose(f_orig);
    fclose(f_out);

    remove(mp3_path);
    rename(tmp_path, mp3_path);
}

#include "main.h"
#include "settings.h"
#include "theme.h"      /* ND4 : Theme + current_theme */
#include "wifi_manager.h"
#include "audio.h"      /* ND10 : lecture directe (audio_load/play/stop) */
#include "player_ui.h"
extern PlayerUI g_player_ui;
static bool s_playing_from_sd = false;

/* ND10 : stb_image est DEJA compile dans le projet (easter eggs PNG) :
   on declare juste les deux symboles, pas besoin du header. */
extern unsigned char *stbi_load_from_memory(unsigned char const *buf, int len,
                                            int *x, int *y, int *ch, int want);
extern void stbi_image_free(void *p);

/* ---------- ND4 : couleurs = theme custom de l'app ---------- */
#define ND_ERRC  RGBA8(224, 80, 80,255)  /* rouge erreur : semantique, reste fixe */

static Theme *nd_th(void)
{
    static Theme s_fb;
    static bool  s_init = false;
    if (current_theme) return current_theme;
    if (!s_init) {   /* securite : secours sombre si current_theme == NULL */
        memset(&s_fb, 0, sizeof s_fb);
        s_fb.bg_primary     = RGBA8( 16, 16, 24,255);
        s_fb.bg_secondary   = RGBA8( 24, 24, 32,255);
        s_fb.bg_header      = RGBA8( 24, 24, 32,255);
        s_fb.bg_selected    = RGBA8( 40, 40, 56,255);
        s_fb.text_primary   = RGBA8(232,232,240,255);
        s_fb.text_secondary = RGBA8(160,160,176,255);
        s_fb.text_disabled  = RGBA8( 96, 96,112,255);
        s_fb.text_accent    = RGBA8( 64,192,224,255);
        s_fb.accent         = RGBA8( 64,192,224,255);
        s_fb.accent2        = RGBA8( 96,208,128,255);
        s_fb.border         = RGBA8( 40, 40, 56,255);
        s_fb.progress_bg    = RGBA8( 30, 30, 40,255);
        s_fb.progress_fill  = RGBA8( 96,208,128,255);
        s_init = true;
    }
    return &s_fb;
}

/* ---------- intro animee (ND2/ND3) ---------- */
#define ND_T_APPEAR   900   /* ms : logo apparait au centre        */
#define ND_T_HOLD    2000   /* ms : fin du maintien (barres animees) */
#define ND_T_SLIDE   2600   /* ms : fin de la glissade vers le haut */
#define ND_T_FADE    3000   /* ms : fin du fondu des etapes        */
#define ND_LOGO_MID  1.45f
#define ND_LOGO_TOP  1.05f
#define ND_CX        (TOP_WIDTH * 0.5f)

/* ---------- etapes de connexion ---------- */
enum { ND_S_IDLE = 0, ND_S_WIFI, ND_S_HTTP, ND_S_HTTPS, ND_S_SERVER,
       ND_S_AUTH, ND_S_OK, ND_S_ERR };   /* ND12 : https puis http visibles */

static const char *const s_step_names[5] =
    { "WiFi", "Module HTTP", "Serveur (https)", "Serveur (http)",
      "Identification" };

static volatile int  s_step    = ND_S_IDLE;
static volatile int  s_fail_at = -1;
static char s_err[96]     = {0};
static char s_srvinfo[64] = {0};
static bool s_wifi_was_off      = false;
static volatile bool s_thread_running = false;
static u64  s_t_start = 0;

int         nd_get_step(void)    { return s_step; }
const char *nd_get_err(void)     { return s_err; }
const char *nd_get_srvinfo(void) { return s_srvinfo; }

/* ---------- M2 (ND6) : navigation ---------- */
enum { ND_V_BROWSE = 0, ND_V_TRACKS, ND_V_ARTALB };
enum { ND_JOB_NONE = 0, ND_JOB_FETCH, ND_JOB_DL, ND_JOB_COVER };

#define ND_MAX_ITEMS 40
#define ND_ROWS       7
#define ND_LCATS      6
#define ND_LX         120.0f
#define ND_DL_DIR   "sdmc:/Music/Navidrome"   /* ND10b : demande user */
#define ND_TMP_DIR  "sdmc:/3DSoundShell/Temp"  /* ND28 : temp lecture, hors bibliotheque */
/* Favoris LOCAUX a CETTE console : enregistrement brut des NdItem favoris,
   distinct des etoiles du serveur (star.view reste appele en plus, pour que
   l administrateur du serveur voie qui aime quoi, mais la liste "Favoris"
   affichee ici ne montre jamais que les favoris de CETTE console). */
#define ND_FAV_PATH "sdmc:/3DSoundShell/nd_favorites.dat"

typedef struct { int kind; char id[32]; char title[100];
                 int sel; int scroll; int pane; } NdView;

static NdView s_stack[4];
static int    s_depth = 0;
static NdItem s_items[ND_MAX_ITEMS];
static volatile int s_item_count = 0;
static char s_resp[786432];
static char s_artists_raw_json[786432]; /* Buffer de cache pour artistes */                  /* reponse API (bss, hors stack) */
static u8   s_dl_buf[16384];          /* ND9 : moins d'allers-retours IPC */

static volatile int s_job         = ND_JOB_NONE;
static httpcContext *s_active_ctx = NULL; /* 3DSS-FIX */

static volatile int s_job_running = 0;
static volatile bool s_ui_fetch_pending = false;
static volatile bool s_sync_fetch_active = false;
static int s_sync_fetch_kind = 0;
static char s_sync_fetch_id[48] = {0};
/* Demande d annulation du telechargement en cours (changer de piste pendant
   qu une autre est encore en train d arriver). Verifiee dans les points
   d attente de nd_dl_attempt ; le job se termine alors vite et proprement au
   lieu de forcer l utilisateur a attendre la fin du transfert precedent. */
static volatile bool s_job_cancel = false;
static char s_job_url[600];
static char s_job_path[256];
static int  s_job_kind = ND_V_BROWSE;
static int  s_job_lcat = 0;
static char s_job_id[40];
static char s_job_cover[40]; /* assez pour un UUID Navidrome (36 car.) complet */
static char s_resp_job[786432];              /* buffer propre au thread job */
static int  s_lcat = 0;                     /* categorie courante (gauche) */
static int  s_rc_sel[ND_LCATS], s_rc_scr[ND_LCATS];
static int  s_fetched_lcat = -1;
static volatile u32 s_wifi_live = 3;   /* ND8 : statut WiFi sonde */
static u64  s_wifi_poll = 0;
static int  s_wifi_lost = 0;           /* ND8 : coupe pendant le mode */
static int  s_fetch_ok  = 1;           /* ND8 : dernier fetch de liste ok */
static int  s_prefer_http = 0;         /* ND9 : https inutile sur ce serveur */
static int  s_httpc_open  = 0;
static int  s_persist     = 0;         /* ND9 : connexion persistante */
static int  s_lsel        = 0;         /* ND9 : selection volet gauche (0..6) */
static int  s_pending_lcat = -1;       /* ND9 : categorie demandee pendant job */
static int  s_show_wifi   = 0;         /* ND9 : affichage statut ACU brut */
static int  s_confirm_disc = 0;        /* ND10b : "etes-vous sur ?" */
static volatile int s_connect_abort = 0;  /* ND10d : B annule l'attente WiFi */

/* ---- ND11 : diagnostic reseau sur l'ecran d'erreur (Y = test) ---- */
static char s_diag1[72];                  /* resultat sonde https */
static char s_diag2[72];                  /* resultat sonde http  */
static char s_diag_resp[2048];            /* corps (statique, pas la pile) */
static int  s_diag_done = 0;
static u64  s_job_t0 = 0;                 /* ND11 : depart du job (secondes) */
static Result s_audio_rc = 0;             /* ND11 : code erreur audio_load */

/* ---- ND10 : pochette (getCoverArt -> stbi -> texture, pattern projet) ---- */
#define ND_COVER_CAP 65536
static u8   s_cover_raw[ND_COVER_CAP];       /* JPEG/PNG brut telecharge */
static volatile int s_cover_raw_len = 0;
static char s_cover_id[40];

/* --- ARCHITECTURE AUDIO & RESEAU SECURISEE (3DSS) --- */
static int s_endless_radio = 0;
static volatile bool s_pending_radio_fetch = false;
static u64 s_abort_timestamp = 0;
static volatile bool s_transition_active = false;

/* Pagination de la liste Navidrome (L/R pour changer de page) */
static int s_page_offset = 0;
static int s_page_size = 30;
static int s_total_items = 0;
static bool s_nd_session_active = false;

bool nd_session_active(void) { return s_nd_session_active; }

static void nd_open_item(const NdItem *it);
static void nd_start_download2(const NdItem *it, int play_now);

static void nd_close_http(httpcContext *ctx)
{
    if (ctx) { httpcCloseContext(ctx); }
    s_active_ctx = NULL;
}                  /* id demande (album/artiste) */
static char s_cover_have[40];                /* id charge dans la texture */
static int  s_cover_inited = 0;              /* texture prete */
static int  s_cover_failed = 0;
static int  s_cover_enabled = 1;             /* levier banc hote */
static C3D_Tex           s_cover_tex;
static Tex3DS_SubTexture s_cover_subtex;
static C2D_Image         s_cover_img;
static int  s_cover_tex_live = 0;
static int  s_cover_deb_sel = -1;            /* anti-rebond volet droit */
static u64  s_cover_deb_t = 0;
/* Thread DEDIE a la pochette, independant de s_job_running : sinon la
   pochette ne pouvait meme pas commencer sa requete tant que le
   telechargement de la musique en cours (potentiellement toute la duree du
   morceau, en streaming progressif) n etait pas termine. */
static volatile int s_cover_job_running = 0;
static char s_cover_job_url[600];
static char s_cover_job_id[40];    /* snapshot de s_cover_id au depart du job */

/* ---- Variables distinctes dédiées à la Preview (Aperçu de liste) ---- */
static char s_prev_id[40] = {0};
static char s_prev_have[40] = {0};
static int  s_prev_inited = 0;
static int  s_prev_failed = 0;
static C3D_Tex           s_prev_tex;
static Tex3DS_SubTexture s_prev_subtex;
static C2D_Image         s_prev_img;
static int  s_prev_tex_live = 0;
static volatile int s_prev_job_running = 0;
static char s_prev_job_url[600];
static char s_prev_job_id[40] = {0};
static u8   s_prev_raw[ND_COVER_CAP];
static volatile int s_prev_raw_len = 0;

static bool nd_fetch_bin(const char *url, u8 *out, int cap, int *outlen,
                         char *err, int errsz);  /* defini plus bas */

static void nd_prev_job_thread(void *arg)
{
    int n = 0;
    char cerr[64] = {0};
    if (nd_fetch_bin(s_prev_job_url, s_prev_raw, ND_COVER_CAP, &n,
                     cerr, (int)sizeof cerr)
        && n > 0 && strcmp(s_prev_job_id, s_prev_id) == 0) {
        s_prev_raw_len = n;
    } else if (strcmp(s_prev_job_id, s_prev_id) == 0) {
        s_prev_failed = 1;
    }
    s_prev_job_running = 0;
}

static void nd_cover_job_thread(void *arg)
{
    (void)arg;
    int n = 0;
    char cerr[64];
    cerr[0] = 0;
    if (nd_fetch_bin(s_cover_job_url, s_cover_raw, ND_COVER_CAP, &n,
                     cerr, (int)sizeof cerr)
        && n > 0 && strcmp(s_cover_job_id, s_cover_id) == 0) {
        s_cover_raw_len = n;
    } else if (strcmp(s_cover_job_id, s_cover_id) == 0) {
        s_cover_failed = 1;
    }
    s_cover_job_running = 0;
}

int nd_api_url(const char *base, const char *view, const char *user,
               const char *pass, char *out, int outsz);  /* defini plus bas */

/* Declenche la requete de pochette IMMEDIATEMENT (sans attendre le prochain
   nd_cover_tick), pour le morceau qu on s apprete a jouer. */
static void nd_cover_request_now(const char *cover_id)
{
    Thread t;
    char vb[80];
    if (!cover_id || !*cover_id) return;
    if (!strcmp(s_cover_id, cover_id) && (s_cover_inited || s_cover_job_running))
        return;   /* deja pret ou deja en vol pour ce meme id */
    snprintf(s_cover_id, sizeof s_cover_id, "%s", cover_id);
    s_cover_raw_len = 0; s_cover_inited = 0; s_cover_failed = 0;
    if (s_cover_job_running) return;   /* un autre id est en vol, il finira ; nd_cover_tick reprendra la main */
    snprintf(vb, sizeof vb, "getCoverArt.view?id=%s&size=160", s_cover_id);
    nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user,
               g_settings.navidrome_pass, s_cover_job_url,
               (int)sizeof s_cover_job_url);
    snprintf(s_cover_job_id, sizeof s_cover_job_id, "%s", s_cover_id);
    s_cover_job_running = 1;
    t = threadCreate(nd_cover_job_thread, NULL, 32 * 1024, 0x30, -1, true);
    if (!t) s_cover_job_running = 0;
}

/* Attente COURTE et BORNEE (0,5s max) que la pochette arrive avant de lancer
   le telechargement de la musique. Le service HTTP de la 3DS ne semble pas
   bien gerer deux requetes vraiment simultanees : sans cette attente, la
   pochette se retrouvait a partager la bande passante avec tout le morceau et
   n apparaissait qu une fois celui-ci entierement recu. En la faisant passer
   EN PREMIER (fichier minuscule), elle arrive en pratique en tres peu de
   temps ; passe le delai, on lance la musique de toute facon (pas de blocage
   indefini si la pochette echoue ou est lente). */
static void nd_cover_wait_brief(void)
{
    int waited = 0;
    while (s_cover_job_running && waited < 500) {
        svcSleepThread(20000000LL);
        waited += 20;
    }
}

/* ---- ND10 : lecture directe (download temporaire -> audio) ---- */
static char s_job_title[100];                /* titre du job DL en cours */
static int  s_play_after = 0;                /* DL : lire apres ecriture */
static int  s_playing_nd = 0;                /* un titre ND joue */
/* Lecture ND demandee mais pas encore demarree (mise en tampon). Sans ce
   drapeau, pendant les secondes de pre-tampon l application croyait qu aucune
   lecture ND n etait en cours et l auto-avance de la playlist LOCALE lancait
   un morceau de la carte SD par-dessus. */
static volatile int s_play_pending = 0;
static char s_play_title[100];
static volatile u32 s_dl_have = 0, s_dl_total = 0;

/* ---- lecteur Navidrome dedie (ecran + commandes tactiles separes de la
   navigation, comme demande : "2 apps separees") ----
   La liste est COPIEE au moment ou la lecture demarre plutot que de garder un
   index dans s_items : s_items est un tampon partage, reecrit des que
   l utilisateur navigue ou relance une recherche pendant qu un titre joue -
   suivre un simple index dedans aurait fini par pointer sur un tout autre
   morceau. */
static bool   s_player_view = false;
static NdItem s_np_list[ND_MAX_ITEMS];
static int    s_np_list_count = 0;
static int    s_np_idx = -1;

/* Piste demandee pendant qu un telechargement est deja en cours : on annule
   celui-ci (s_job_cancel) plutot que de bloquer le bouton, et on la lance des
   que le job annule s est vraiment arrete (verifie une fois par image). */
static NdItem s_pending_play_item;
static volatile bool s_pending_play_valid = false;

#define ND_CACHE_SLOTS 12
typedef struct { char key[48]; NdItem items[ND_MAX_ITEMS]; int count; } NdCacheSlot;
static NdCacheSlot s_cache[ND_CACHE_SLOTS];
static int s_cache_next = 0;
static int s_cache_init = 0;

static NdCacheSlot *nd_cache_get(const char *key)
{
    int i;
    if (!s_cache_init) {
        for (i = 0; i < ND_CACHE_SLOTS; i++) { s_cache[i].count = -1; s_cache[i].key[0] = 0; }
        s_cache_init = 1;
    }
    for (i = 0; i < ND_CACHE_SLOTS; i++)
        if (s_cache[i].count >= 0 && !strcmp(s_cache[i].key, key))
            return &s_cache[i];
    return NULL;
}

static int nd_cache_fill(const char *key)      /* -1 si absent */
{
    NdCacheSlot *c = nd_cache_get(key);
    if (!c) return -1;
    memcpy(s_items, c->items, (size_t)c->count * sizeof s_items[0]);
    s_item_count = c->count;
    return c->count;
}

static void nd_cache_put(const char *key, int count)
{
    NdCacheSlot *c = nd_cache_get(key);
    if (!c) { c = &s_cache[s_cache_next]; s_cache_next = (s_cache_next + 1) % ND_CACHE_SLOTS; }
    snprintf(c->key, sizeof c->key, "%s", key);
    c->count = count;
    memcpy(c->items, s_items, (size_t)count * sizeof s_items[0]);
}

static char s_msg[256];
static bool s_msg_err = false;
static u64  s_msg_until = 0;

/* ============ MD5 embarque (RFC 1321) ============ */
static unsigned nd_rol(unsigned x, int n) { return (x << n) | (x >> (32 - n)); }
static void nd_md5_transform(unsigned s[4], const unsigned char blk[64])
{
    static const int S[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    static const unsigned K[64] = {0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,
        0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,
        0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,
        0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,
        0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,0xfffa3942,0x8771f681,
        0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,
        0x1fa27cf8,0xc4ac5665,0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,
        0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,
        0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    unsigned a = s[0], b = s[1], c = s[2], d = s[3], x[16];
    int i;
    for (i = 0; i < 16; i++)
        x[i] = (unsigned)blk[i*4] | ((unsigned)blk[i*4+1] << 8) |
               ((unsigned)blk[i*4+2] << 16) | ((unsigned)blk[i*4+3] << 24);
    for (i = 0; i < 64; i++) {
        unsigned f, g;
        if (i < 16)      { f = (b & c) | (~b & d);   g = (unsigned)i; }
        else if (i < 32) { f = (d & b) | (~d & c);   g = (5*i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d;            g = (3*i + 5) % 16; }
        else             { f = c ^ (b | ~d);         g = (7*i) % 16; }
        unsigned t = d; d = c; c = b;
        b = b + nd_rol(a + f + K[i] + x[g], S[i]);
        a = t;
    }
    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
}
static void nd_md5_raw(const unsigned char *in, size_t len,
                       unsigned char out[16])
{
    unsigned s[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    unsigned long long bits = (unsigned long long)len * 8ULL;
    size_t i = 0, rem, pad;
    unsigned char blk[128];
    int j;
    for (; i + 64 <= len; i += 64) nd_md5_transform(s, in + i);
    memset(blk, 0, sizeof blk);
    rem = len - i;
    memcpy(blk, in + i, rem);
    blk[rem] = 0x80;
    pad = (rem < 56) ? 64 : 128;
    for (j = 0; j < 8; j++)
        blk[pad - 8 + j] = (unsigned char)(bits >> (8 * j));
    nd_md5_transform(s, blk);
    if (pad == 128) nd_md5_transform(s, blk + 64);
    for (j = 0; j < 4; j++) {
        out[j*4]   = (unsigned char)(s[j] & 0xff);
        out[j*4+1] = (unsigned char)((s[j] >> 8) & 0xff);
        out[j*4+2] = (unsigned char)((s[j] >> 16) & 0xff);
        out[j*4+3] = (unsigned char)((s[j] >> 24) & 0xff);
    }
}

/* ---------- dessin ---------- */
static C2D_TextBuf s_nd_textbuf = NULL;

extern float g_3d_iod;
extern int g_3d_eye;
static int s_nd_flat_draw = 0;
static int s_nd_overlay_draw = 0;
static float nd_calc_3d_off(float base_x);

static float nd_text_ex(const char *t, float x, float y, float sz, u32 col,
                        bool centered, u8 alpha)
{
    C2D_Text txt;
    float w = 0.0f, h = 0.0f;
    if (!t || !t[0]) return 0.0f;
    if (!s_nd_textbuf) s_nd_textbuf = C2D_TextBufNew(4096);
    C2D_TextBufClear(s_nd_textbuf);
    C2D_TextParse(&txt, s_nd_textbuf, t);
    C2D_TextOptimize(&txt);
    C2D_TextGetDimensions(&txt, sz, sz, &w, &h);
    if (centered) x = ND_CX - w * 0.5f;
    if (alpha < 255)
        col = (col & 0x00FFFFFFu) | ((u32)alpha << 24);
    float ox = nd_calc_3d_off(x);
    float depth_z = (s_nd_flat_draw || s_nd_overlay_draw) ? 0.95f : 0.5f;
    C2D_DrawText(&txt, C2D_WithColor, x + ox, y, depth_z, sz, sz, col);
    return w;
}
/* Centrage sur l ecran du BAS (320px) : nd_text_ex()/nd_text_ct() centrent
   toujours sur ND_CX (milieu de l ecran du HAUT, 400px) - correct pour tout
   l ecran du haut de Navidrome, faux pour un bouton dessine sur l ecran du
   bas (deux boutons du lecteur dedie). */
static void nd_text_bot_ct(const char *t, float y, float sz, u32 col)
{
    C2D_Text txt;
    float w = 0.0f, h = 0.0f;
    if (!t || !t[0]) return;
    if (!s_nd_textbuf) s_nd_textbuf = C2D_TextBufNew(4096);
    C2D_TextBufClear(s_nd_textbuf);
    C2D_TextParse(&txt, s_nd_textbuf, t);
    C2D_TextOptimize(&txt);
    C2D_TextGetDimensions(&txt, sz, sz, &w, &h);
    C2D_DrawText(&txt, C2D_WithColor, (float)BOT_WIDTH * 0.5f - w * 0.5f,
                 y, 0.5f, sz, sz, col);
}

/* compat M1 : signature historique (x en premier) */
static void nd_text_ct(float y, float sz, u32 c, const char *t, bool ctr, u8 a)
{ nd_text_ex(t, 0, y, sz, c, ctr, a); }
static void nd_text(float x, float y, float sz, u32 c, const char *t)
{ nd_text_ex(t, x, y, sz, c, false, 255); }
extern float g_3d_iod;
extern int   g_3d_eye;

/* Calcule le décalage 3D en pixels selon l'oeil et le composant */


static float nd_calc_3d_off(float base_x)
{
    if (s_nd_flat_draw || g_3d_iod <= 0.001f) return 0.0f;

    float dir = (g_3d_eye == 0) ? -1.0f : 1.0f;
    if (s_nd_overlay_draw) return dir * (-4.5f) * (g_3d_iod / 6.0f);

    NdView *v = &s_stack[s_depth];
    float depth = 0.0f;

    if (base_x < 120.0f) {
        depth = (v->pane == 0) ? -2.5f : 1.0f;
    } else {
        depth = (v->pane == 1 || v->kind != ND_V_BROWSE) ? -2.5f : 1.0f;
    }

    return dir * depth * (g_3d_iod / 6.0f);
}

static void nd_rect(float x, float y, float w, float h, u32 c)
{
    float ox = nd_calc_3d_off(x);
    float depth_z = (s_nd_flat_draw || s_nd_overlay_draw) ? 0.90f : 0.0f;
    C2D_DrawRectangle(x + ox, y, depth_z, w, h, c, c, c, c);
}

static float nd_ease(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
static void nd_trunc(const char *in, char *out, int n)
{
    int i = 0;
    if (!in) { out[0] = 0; return; }
    while (in[i] && i < n) { out[i] = in[i]; i++; }
    out[i] = 0;
    if (in[i] && n >= 2) out[n - 1] = '>';
}

/* ---------- helpers purs (banches) ---------- */
void nd_md5_hex(const char *in, char out[33])
{
    unsigned char d[16];
    static const char hx[] = "0123456789abcdef";
    int i;
    nd_md5_raw((const unsigned char *)in, strlen(in), d);
    for (i = 0; i < 16; i++) {
        out[i * 2]     = hx[d[i] >> 4];
        out[i * 2 + 1] = hx[d[i] & 15];
    }
    out[32] = 0;
}

void nd_make_salt(char out[17])
{
    static unsigned long long s_ctr = 0;
    unsigned long long t =
        (unsigned long long)osGetTime() + (++s_ctr) * 7919ULL;
    snprintf(out, 17, "%08x%08x",
             (unsigned)(t & 0xFFFFFFFFu), (unsigned)(t >> 32));
}

/* ND6 : URL API generique : base + /rest/ + view + auth token */
int nd_api_url(const char *base, const char *view,
               const char *user, const char *pass, char *out, int outsz)
{
    char salt[17], concat[224], tok[33], b[128];
    int bl;
    nd_make_salt(salt);
    snprintf(concat, sizeof concat, "%s%s", pass ? pass : "", salt);
    nd_md5_hex(concat, tok);
    strncpy(b, (base && base[0]) ? base : "http://localhost", sizeof b - 1);
    b[sizeof b - 1] = 0;
    bl = (int)strlen(b);
    while (bl > 0 && b[bl - 1] == '/') b[--bl] = 0;
    return snprintf(out, (size_t)outsz,
        "%s/rest/%s%cu=%s&s=%s&t=%s&v=1.16.1&c=3DSoundShell",
        b, view ? view : "", strchr(view ? view : "", '?') ? '&' : '?',
        user ? user : "", salt, tok);
}

int nd_build_ping_url(const char *base, const char *user, const char *pass,
                      char *out, int outsz)
{
    return nd_api_url(base, "ping.view?f=json", user, pass, out, outsz);
}

/* ND2 : https:// -> http:// (repli si le TLS de la 3DS echoue) */
bool nd_http_variant(const char *url, char *out, int outsz)
{
    if (!url || strncmp(url, "https://", 8) != 0) return false;
    if (outsz < 8) return false;
    memcpy(out, "http://", 7);
    strncpy(out + 7, url + 8, (size_t)(outsz - 8));
    out[outsz - 1] = 0;
    return true;
}

bool nd_json_str(const char *js, const char *key, char *out, int outsz)
{
    char pat[64];
    const char *p;
    int i = 0;
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p && *p != '"' && i < outsz - 1) {
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    out[i] = 0;
    return true;
}

bool nd_json_int(const char *js, const char *key, int *outv)
{
    char pat[64];
    const char *p;
    char *end = NULL;
    long v;
    snprintf(pat, sizeof pat, "\"%s\":", key);
    p = strstr(js, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    v = strtol(p, &end, 10);
    if (end == p) return false;
    *outv = (int)v;
    return true;
}

/* ND6 : collecte tous les objets de TOUS les tableaux "key":[...]
   (getArtists repond avec plusieurs tableaux "artist" imbriques).
   Extrait id, f_title, f_sub, suffix, duration/albumCount. */
/* ND6 : collecte avec pagination par offset */
int nd_collect_ex(const char *js, const char *key,
                  const char *f_title, const char *f_sub,
                  NdItem *items, int max, int offset)
{
    char pat[40];
    const char *p = js;
    int cnt = 0;
    int skipped = 0;
    if (!js || !key || !items || max <= 0) return 0;
    if (offset < 0) offset = 0;
    snprintf(pat, sizeof pat, "\"%s\":[", key);
    while ((p = strstr(p, pat)) != NULL && cnt < max) {
        p += strlen(pat);
        while (*p && cnt < max) {
            const char *st;
            int depth = 0, inq = 0, len;
            char obj[2048];
            while (*p && *p != '{' && *p != ']') p++;
            if (*p != '{') break;
            st = p;
            while (*p) {
                char c = *p;
                if (inq) {
                    if (c == '\\') p++;
                    else if (c == '"') inq = 0;
                } else {
                    if (c == '"') inq = 1;
                    else if (c == '{') depth++;
                    else if (c == '}') { depth--; if (depth == 0) { p++; break; } }
                }
                p++;
            }
            if (depth != 0) break;
            len = (int)(p - st);
            if (len <= 0 || len >= (int)sizeof obj) continue;
            memcpy(obj, st, (size_t)len);
            obj[len] = 0;

            char tmp_id[32];
            if (!nd_json_str(obj, "id", tmp_id, (int)sizeof tmp_id))
                continue;

            if (skipped < offset) {
                skipped++;
                continue;
            }

            memset(&items[cnt], 0, sizeof items[cnt]);
            snprintf(items[cnt].id, sizeof items[cnt].id, "%s", tmp_id);

            if (f_title)
                nd_json_str(obj, f_title, items[cnt].title,
                            (int)sizeof items[cnt].title);
            if (f_sub)
                nd_json_str(obj, f_sub, items[cnt].sub,
                            (int)sizeof items[cnt].sub);
            nd_json_str(obj, "suffix", items[cnt].sfx,
                        (int)sizeof items[cnt].sfx);
            if (!nd_json_str(obj, "coverArt", items[cnt].cover,
                             (int)sizeof items[cnt].cover))
                snprintf(items[cnt].cover, sizeof items[cnt].cover,
                         "%s", items[cnt].id);
            {
                char sdate[8];
                items[cnt].starred = nd_json_str(obj, "starred", sdate,
                                                 (int)sizeof sdate);
            }
            items[cnt].num = -1;
            if (!nd_json_int(obj, "duration", &items[cnt].num))
                if (!nd_json_int(obj, "albumCount", &items[cnt].num))
                    items[cnt].num = -1;
            cnt++;
        }
    }
    return cnt;
}

int nd_collect(const char *js, const char *key,
               const char *f_title, const char *f_sub,
               NdItem *items, int max)
{
    return nd_collect_ex(js, key, f_title, f_sub, items, max, 0);
}


/* ND6 : nom de fichier SD sur (pas de / : * ? etc.) */
void nd_sanitize(const char *title, const char *sfx, char *out, int outsz)
{
    int i = 0, j;
    if (outsz < 16) return;
    if (title) {
        for (j = 0; title[j] && i < outsz - 12; j++) {
            char c = title[j];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                c == '_' || c == '.' || c == '(' || c == ')')
                out[i++] = c;
            else
                out[i++] = '_';
        }
    }
    while (i > 0 && out[i - 1] == ' ') i--;
    if (i == 0) { memcpy(out, "track", 5); i = 5; }
    if (!sfx || !sfx[0]) sfx = "mp3";
    out[i++] = '.';
    for (j = 0; sfx[j] && i < outsz - 1; j++) out[i++] = sfx[j];
    out[i] = 0;
}

/* ---------- HTTP GET (pattern fetch_version d'update.c) ---------- */
static bool nd_http_get(const char *url, char *out, int outsz, u32 *status_out)
{
    httpcContext ctx;
    u32 status = 0;
    Result src = (Result)-1;
    int total = 0, i;
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(rc)) {
        /* NE PAS pointer s_active_ctx sur ctx avant d avoir confirme
           l ouverture : ctx est une variable locale de PILE. Si
           httpcOpenContext echoue (un hoquet WiFi/DNS, pas rare), la
           fonction rendait la main avec s_active_ctx pointant sur une pile
           de thread qui n existe plus - tout code UI qui dereference
           ensuite s_active_ctx->servhandle (nd_abort_active_download,
           nd_disconnect) lit/ecrit dans de la memoire liberee. */
        snprintf(s_err, sizeof s_err, "OpenCtx %08lX", (unsigned long)rc);
        return false;
    }
    s_active_ctx = &ctx;
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "3DSoundShell");

    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) {
        snprintf(s_err, sizeof s_err, "BeginReq %08lX", (unsigned long)rc);
        nd_close_http(&ctx);
        return false;
    }

    for (i = 0; i < 100; i++) {              /* ~10 s max */
        src = httpcGetResponseStatusCode(&ctx, &status);
        if (!R_FAILED(src)) break;
        svcSleepThread(100000000LL);
    }
    if (R_FAILED(src)) {
        snprintf(s_err, sizeof s_err, "Pas de reponse serveur");
        nd_close_http(&ctx);
        return false;
    }
    if (status_out) *status_out = status;

    if (status == 301 || status == 302 || status == 303) {
        char loc[512] = {0};
        httpcGetResponseHeader(&ctx, "Location", loc, sizeof loc);
        nd_close_http(&ctx);
        if (!loc[0]) {
            snprintf(s_err, sizeof s_err, "Redirection vide");
            return false;
        }
        return nd_http_get(loc, out, outsz, status_out);
    }
    if (status != 200) {
        snprintf(s_err, sizeof s_err, "HTTP %lu", (unsigned long)status);
        nd_close_http(&ctx);
        return false;
    }

    for (;;) {
        int chunk;
        u32 read = 0;
        Result dr;
        if (total >= outsz - 1) break;
        chunk = outsz - 1 - total;
        if (chunk > 2048) chunk = 2048;
        dr = httpcDownloadData(&ctx, (u8 *)(out + total), (u32)chunk, &read);
        total += (int)read;
        if (s_dl_total > 0 && s_dl_have >= s_dl_total) break;
        if (R_SUCCEEDED(dr)) break;
        if (dr != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) break;
    }
    out[total] = 0;
    nd_close_http(&ctx);
    return total > 0;
}

/* ND2 : HTTPS d'abord, repli HTTP automatique.
   ND8 : la session httpc est ouverte UNE fois pour tout le mode
   (nd_enter) et fermee en sortant (nd_leave) : plus aucune course
   entre threads sur la poignee httpc (cause du OpenCtx D8E007F7). */
static void nd_msg(const char *m, int ms, bool is_err);
static void nd_net_ok(void);
static void nd_net_fail(void);

static bool nd_fetch(const char *url, char *out, int outsz,
                     u32 *status_out)
{
    char alt[600];
    bool ok;
    if (s_prefer_http && nd_http_variant(url, alt, (int)sizeof alt)) {
        ok = nd_http_get(alt, out, outsz, status_out);
    } else {
        ok = nd_http_get(url, out, outsz, status_out);
        if (!ok && nd_http_variant(url, alt, (int)sizeof alt)) {
            s_prefer_http = 1;  /* ND9 : ce serveur passe par http */
            s_err[0] = 0;
            ok = nd_http_get(alt, out, outsz, status_out);
        }
    }
    /* ND10 : WiFi [X] REEL : base sur le resultat des requetes,
       pas seulement sur le statut ACU (qui reste a 3 quand c'est
       Internet qui est coupe, pas la radio WiFi). */
    if (ok) nd_net_ok(); /* Silencieux pour eviter l'alerte reseau lors d'une lecture locale */
    return ok;
}

/* ---------- favori (star/unstar), lecteur dedie uniquement ---------- */
/* Stockage local : un fichier plat d enregistrements NdItem bruts. Petit
   (quelques dizaines de favoris au plus), pas besoin de plus qu une lecture
   sequentielle + reecriture complete pour retirer un element. */
static bool nd_fav_is_local(const char *id)
{
    FILE *f = fopen(ND_FAV_PATH, "rb");
    NdItem it;
    bool found = false;
    if (!f) return false;
    while (!found && fread(&it, sizeof it, 1, f) == 1)
        if (!strcmp(it.id, id)) found = true;
    fclose(f);
    return found;
}

static void nd_fav_add(const NdItem *item)
{
    FILE *f;
    if (nd_fav_is_local(item->id)) return;
    mkdir("sdmc:/3DSoundShell", 0777);
    f = fopen(ND_FAV_PATH, "ab");
    if (!f) return;
    fwrite(item, sizeof *item, 1, f);
    fclose(f);
}

static void nd_fav_remove(const char *id)
{
    FILE *in = fopen(ND_FAV_PATH, "rb");
    FILE *out;
    NdItem it;
    if (!in) return;
    out = fopen(ND_FAV_PATH ".tmp", "wb");
    if (!out) { fclose(in); return; }
    while (fread(&it, sizeof it, 1, in) == 1)
        if (strcmp(it.id, id)) fwrite(&it, sizeof it, 1, out);
    fclose(in);
    fclose(out);
    remove(ND_FAV_PATH);
    rename(ND_FAV_PATH ".tmp", ND_FAV_PATH);
}

/* Charge directement les favoris locaux dans s_items, sans le moindre acces
   reseau : c est ce qui alimente la categorie "Favoris" du volet gauche, et
   garantit qu elle est a jour a chaque ouverture (un simple fichier local,
   jamais de cache a rafraichir). */
static void nd_fav_load_into_items(void)
{
    FILE *f = fopen(ND_FAV_PATH, "rb");
    s_item_count = 0;
    if (!f) return;
    while (s_item_count < ND_MAX_ITEMS
           && fread(&s_items[s_item_count], sizeof s_items[0], 1, f) == 1)
        s_item_count++;
    fclose(f);
}

static volatile int s_star_job_running = 0;
static char s_star_job_url[600];

static void nd_star_job_thread(void *arg)
{
    (void)arg;
    char resp[512]; u32 st = 0;
    nd_fetch(s_star_job_url, resp, (int)sizeof resp, &st);
    s_star_job_running = 0;
}

/* Bascule optimiste : l affichage change tout de suite, sans attendre la
   confirmation reseau (le job tourne seul, sur son propre thread, comme la
   pochette - il ne doit pas non plus attendre la fin d un telechargement de
   musique en cours). */
static void nd_player_toggle_star(void)
{
    NdItem *it;
    char vb[80];
    Thread t;
    if (s_star_job_running || s_np_idx < 0 || s_np_idx >= s_np_list_count)
        return;
    it = &s_np_list[s_np_idx];
    it->starred = !it->starred;
    if (it->starred) nd_fav_add(it); else nd_fav_remove(it->id);
    snprintf(vb, sizeof vb, "%s.view?id=%s", it->starred ? "star" : "unstar", it->id);
    nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user,
               g_settings.navidrome_pass, s_star_job_url, (int)sizeof s_star_job_url);
    s_star_job_running = 1;
    t = threadCreate(nd_star_job_thread, NULL, 32 * 1024, 0x30, -1, true);
    if (!t) s_star_job_running = 0;
}

/* Crochet de streaming progressif : quand il est non nul, chaque morceau ecrit
   est vide sur le disque puis signale, et le transfert ecrit directement dans
   le fichier final (pas de .part ni de rename : le decodeur l a deja ouvert). */
static void (*s_dl_chunk_cb)(void) = NULL;

static u64 s_dl_last_data_time = 0;
#define DL_TIMEOUT_MS 120000
/* ---------- ND6 : telechargement fichier (.part + rename) ---------- */
static bool nd_dl_attempt(const char *url, const char *path,
                          char *err, int errsz, int depth)
{
    httpcContext ctx;
    u32 status = 0;
    Result src = (Result)-1;
    char clen[24] = {0};
    FILE *f;
    int i;
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(rc)) {
        /* Ne pas pointer s_active_ctx sur ctx avant d avoir confirme
           l ouverture : voir le meme correctif dans nd_http_get. */
        snprintf(err, (size_t)errsz, "OpenCtx %08lX", (unsigned long)rc);
        return false;
    }
    s_active_ctx = &ctx;
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "3DSoundShell");
    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) {
        snprintf(err, (size_t)errsz, "BeginReq %08lX", (unsigned long)rc);
        nd_close_http(&ctx);
        return false;
    }
    for (i = 0; i < 100; i++) {
        if (s_job_cancel) {   /* changer de piste pendant ce telechargement */
            snprintf(err, (size_t)errsz, "Annule");
            nd_close_http(&ctx);
            return false;
        }
        src = httpcGetResponseStatusCode(&ctx, &status);
        if (!R_FAILED(src)) break;
        svcSleepThread(100000000LL);
    }
    if (R_FAILED(src)) {
        snprintf(err, (size_t)errsz, "Pas de reponse serveur");
        nd_close_http(&ctx);
        return false;
    }
    if ((status == 301 || status == 302 || status == 303) && depth < 3) {
        char loc[512] = {0};
        httpcGetResponseHeader(&ctx, "Location", loc, sizeof loc);
        nd_close_http(&ctx);
        if (!loc[0]) {
            snprintf(err, (size_t)errsz, "Redirection vide");
            return false;
        }
        return nd_dl_attempt(loc, path, err, errsz, depth + 1);
    }
    if (status != 200) {
        snprintf(err, (size_t)errsz, "HTTP %lu", (unsigned long)status);
        nd_close_http(&ctx);
        return false;
    }
    if (R_SUCCEEDED(httpcGetResponseHeader(&ctx, "Content-Length",
                                           clen, sizeof clen)))
        s_dl_total = (u32)strtoul(clen, NULL, 10);

    f = fopen(path, "wb");
    if (!f) {
        snprintf(err, (size_t)errsz, "Ecriture SD impossible");
        nd_close_http(&ctx);
        return false;
    }
    s_dl_have = 0;
    s_dl_last_data_time = osGetTime();
    for (;;) {
        u32 read = 0;
        Result dr;
        /* 3DSS-FIX: Reprise de telechargement HTTP Range. Si le thread secondaire charge un dossier (s_sync_fetch_active), 
           on ferme proprement la connexion et le fichier pour liberer 100% de la puce WiFi de la 3DS.
           Une fois le dossier charge, on reprend le telechargement la ou il s est arrete ! */
        if (s_sync_fetch_active) {
            fclose(f);
            nd_close_http(&ctx);
            s_active_ctx = NULL;
            
            /* Attend que le thread de secours ait fini de charger les pistes */
            while (s_sync_fetch_active) {
                svcSleepThread(50000000LL); /* Pause de 50ms */
                if (s_job_cancel) return false;
            }
            
            /* Reconnexion sur le meme URL avec la requete de reprise (Range) */
            rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
            if (R_FAILED(rc)) {
                snprintf(err, (size_t)errsz, "Range OpenCtx Fail");
                return false;
            }
            httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
            httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
            httpcAddRequestHeaderField(&ctx, "User-Agent", "3DSoundShell");
            
            /* On demande uniquement a partir des octets deja recus */
            char range_h[64];
            snprintf(range_h, sizeof range_h, "bytes=%lu-", (unsigned long)s_dl_have);
            httpcAddRequestHeaderField(&ctx, "Range", range_h);
            
            rc = httpcBeginRequest(&ctx);
            if (R_FAILED(rc)) {
                nd_close_http(&ctx);
                return false;
            }
            for (i = 0; i < 100; i++) {
                if (s_job_cancel) { nd_close_http(&ctx); return false; }
                src = httpcGetResponseStatusCode(&ctx, &status);
                if (!R_FAILED(src)) break;
                svcSleepThread(100000000LL);
            }
            if (status != 200 && status != 206) {
                nd_close_http(&ctx);
                return false;
            }
            s_active_ctx = &ctx;
            
            /* Reouverture du fichier SD en mode APPEND (ab) pour ecrire la suite sans ecraser ! */
            f = fopen(path, "ab");
            if (!f) {
                snprintf(err, (size_t)errsz, "Range SD Open Fail");
                nd_close_http(&ctx);
                return false;
            }
        }
        if (s_job_cancel) {   /* changer de piste pendant ce telechargement */
            snprintf(err, (size_t)errsz, "Annule");
            fclose(f);
            if (!s_dl_chunk_cb) remove(path); /* en streaming, le lecteur l a ouvert */
            nd_close_http(&ctx);
            return false;
        }
        dr = httpcDownloadData(&ctx, s_dl_buf, sizeof s_dl_buf, &read);
        if (read) {
            if (fwrite(s_dl_buf, 1, read, f) != read) {
                snprintf(err, (size_t)errsz, "Ecriture SD echouee");
                fclose(f);
                if (!s_dl_chunk_cb) remove(path); /* en streaming, le lecteur l a ouvert */
                nd_close_http(&ctx);
                return false;
            }
            s_dl_have += read;
            s_dl_last_data_time = osGetTime();
            /* Streaming progressif : on pousse les octets sur le disque tout de
               suite (le decodeur lit le meme fichier en parallele) et on
               previent le consommateur, qui demarre la lecture des qu il a
               assez d avance. */
            if (s_dl_chunk_cb) { fflush(f); s_dl_chunk_cb(); }
        }
        if (s_dl_total > 0 && s_dl_have >= s_dl_total) break;
        if (R_SUCCEEDED(dr)) break;
        if (dr != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) {
            snprintf(err, (size_t)errsz, "Transfert coupe");
            fclose(f);
            if (!s_dl_chunk_cb) remove(path); /* en streaming, le lecteur l a ouvert */
            nd_close_http(&ctx);
            return false;
        }
        if (osGetTime() - s_dl_last_data_time > DL_TIMEOUT_MS * 1000000ULL) { snprintf(err, (size_t)errsz, "Timeout 30s"); fclose(f); if (!s_dl_chunk_cb) remove(path); nd_close_http(&ctx); return false; }
    }
    fclose(f);
    nd_close_http(&ctx);
    return s_dl_have > 0;
}

bool nd_dl_file(const char *url, const char *path, char *err, int errsz)
{
    char part[280];
    char alt[600];
    bool ok;
    /* En streaming, le decodeur lit deja ce fichier : on ecrit dedans
       directement, sans .part ni rename (renommer sous le lecteur le
       casserait), et on ne l efface jamais en cas d echec. */
    const bool stream = (s_dl_chunk_cb != NULL);
    const char *dst;
    snprintf(part, sizeof part, "%s.part", path);
    dst = stream ? path : part;
    s_dl_have = 0;
    s_dl_total = 0;
    if (s_prefer_http && nd_http_variant(url, alt, (int)sizeof alt)) {
        ok = nd_dl_attempt(alt, dst, err, errsz, 0);
        if (!ok && !stream) { s_dl_have = 0; s_dl_total = 0; ok = nd_dl_attempt(url, dst, err, errsz, 0); }
    } else {
        ok = nd_dl_attempt(url, dst, err, errsz, 0);
        if (!ok && !stream && nd_http_variant(url, alt, (int)sizeof alt)) {
            s_prefer_http = 1;
            s_dl_have = 0;
            s_dl_total = 0;
            ok = nd_dl_attempt(alt, dst, err, errsz, 0);
        }
    }
    if (!ok) {
        if (!stream) remove(part);
        if (!strstr(err, "SD") && !strstr(err, "Annule"))     /* ND10 : erreur transport = WiFi [X] */
            nd_net_fail();
        return false;
    }
    if (!stream) {
        remove(path);
        if (rename(part, path) != 0) {
            snprintf(err, (size_t)errsz, "Rename SD echoue");
            remove(part);
            return false;
        }
    }
    nd_net_ok();                    /* ND10 */
    return true;
}

/* ---------- ND10 : telechargement BINAIRE en memoire (pochette) ---------- */
static bool nd_bin_attempt(const char *url, u8 *out, int cap, int *outlen,
                           char *err, int errsz, int depth)
{
    httpcContext ctx;
    u32 status = 0;
    Result src = (Result)-1;
    int total = 0, i;
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(rc)) {
        snprintf(err, (size_t)errsz, "OpenCtx %08lX", (unsigned long)rc);
        return false;
    }
    s_active_ctx = &ctx;
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "3DSoundShell");
    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) {
        snprintf(err, (size_t)errsz, "BeginReq %08lX", (unsigned long)rc);
        nd_close_http(&ctx);
        return false;
    }
    for (i = 0; i < 100; i++) {
        src = httpcGetResponseStatusCode(&ctx, &status);
        if (!R_FAILED(src)) break;
        svcSleepThread(100000000LL);
    }
    if (R_FAILED(src)) {
        snprintf(err, (size_t)errsz, "Pas de reponse serveur");
        nd_close_http(&ctx);
        return false;
    }
    if ((status == 301 || status == 302 || status == 303) && depth < 3) {
        char loc[512] = {0};
        httpcGetResponseHeader(&ctx, "Location", loc, sizeof loc);
        nd_close_http(&ctx);
        if (!loc[0]) {
            snprintf(err, (size_t)errsz, "Redirection vide");
            return false;
        }
        return nd_bin_attempt(loc, out, cap, outlen, err, errsz, depth + 1);
    }
    if (status != 200) {
        snprintf(err, (size_t)errsz, "HTTP %lu", (unsigned long)status);
        nd_close_http(&ctx);
        return false;
    }
    for (;;) {
        u32 read = 0;
        Result dr;
        if (total >= cap) {
            snprintf(err, (size_t)errsz, "Pochette trop grande");
            nd_close_http(&ctx);
            return false;
        }
        dr = httpcDownloadData(&ctx, out + total, (u32)(cap - total), &read);
        total += (int)read;
        if (s_dl_total > 0 && s_dl_have >= s_dl_total) break;
        if (R_SUCCEEDED(dr)) break;
        if (dr != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) {
            snprintf(err, (size_t)errsz, "Transfert coupe");
            nd_close_http(&ctx);
            return false;
        }
    }
    nd_close_http(&ctx);
    *outlen = total;
    return total > 0;
}

static bool nd_fetch_bin(const char *url, u8 *out, int cap, int *outlen,
                         char *err, int errsz)
{
    char alt[600];
    bool ok;
    if (s_prefer_http && nd_http_variant(url, alt, (int)sizeof alt)) {
        ok = nd_bin_attempt(alt, out, cap, outlen, err, errsz, 0);
        if (!ok)
            ok = nd_bin_attempt(url, out, cap, outlen, err, errsz, 0);
    } else {
        ok = nd_bin_attempt(url, out, cap, outlen, err, errsz, 0);
        if (!ok && nd_http_variant(url, alt, (int)sizeof alt)) {
            s_prefer_http = 1;
            ok = nd_bin_attempt(alt, out, cap, outlen, err, errsz, 0);
        }
    }
    if (ok) nd_net_ok(); /* Silencieux */
    return ok;
}

/* ---------- reponse Subsonic : statut / erreur ---------- */
static bool nd_parse_status(const char *resp, char *err, int errsz)
{
    char st[16] = {0};
    nd_json_str(resp, "status", st, sizeof st);
    if (strcmp(st, "ok") == 0) return true;
    {
        int code = 0;
        char msg[64] = {0};
        nd_json_int(resp, "code", &code);
        nd_json_str(resp, "message", msg, sizeof msg);
        if (code == 0 && msg[0] == 0)
            snprintf(err, (size_t)errsz, "Reponse invalide");
        else
            snprintf(err, (size_t)errsz, "Err %d : %s", code, msg);
    }
    return false;
}

/* ---------- thread de connexion (M1) ---------- */
static bool nd_browse_start(int cat);
static void nd_artists_paginate_local(void);
static void nd_artists_paginate_local(void)
{
    NdView *v = &s_stack[0];
    v->sel = 0;
    v->scroll = 0;
    s_rc_sel[4] = 0;
    s_rc_scr[4] = 0;
    int cnt = nd_collect_ex(s_artists_raw_json, "artist", "name", NULL, s_items, s_page_size, s_page_offset);
    if (cnt > 0) {
        s_item_count = cnt;
    } else {
        if (s_page_offset >= s_page_size) {
            s_page_offset -= s_page_size;
        }
    }
}

static void nd_open_browse(int cat);

static void nd_connect_thread(void *arg)
{
    char url[600], *resp = s_resp;
    u32 wifi = 0, status = 0;
    int okf = 0;                     /* ND12 */
    (void)arg;
    s_err[0] = 0;
    s_srvinfo[0] = 0;
    s_fail_at = -1;

    /* 1. WiFi - ND5/ND6 : nd_enter() a deja rappelle wifi_manager_enable()
       si l'option eco l'avait coupe ; on ATTEND l'association reelle
       ND13 : le statut ACU peut MENTIR - sur partage telephone il
       reste a 1 alors que https/http passent parfaitement (prouve
       sur la console de l'user : ACU=1, sondes Y = 200/200).
       On n'attend donc QUE si la radio est vraiment eteinte
       (statut 0), 120 s max, B annule. La VRAIE preuve du reseau
       = les etapes 3/5 et 4/5 (ping serveur). */
    s_step = ND_S_WIFI;
    {
        int tries;
        wifi = 0;
        ACU_GetWifiStatus(&wifi);
        s_wifi_live = wifi;
        for (tries = 0; tries < 240 && wifi < 1; tries++) {
            if (s_connect_abort) {          /* ND10d : B = annuler */
                s_step = ND_S_IDLE;
                s_thread_running = false;
                return;
            }
            svcSleepThread(500000000LL);
            ACU_GetWifiStatus(&wifi);
            s_wifi_live = wifi;
        }
        if (wifi < 1) {
            snprintf(s_err, sizeof s_err,
                     "WiFi eteint (statut %lu)", (unsigned long)wifi);
            s_fail_at = ND_S_WIFI;
            s_step = ND_S_ERR;
            s_thread_running = false;
            return;
        }
    }

    /* 2. Module HTTP (session deja ouverte par nd_enter, ND8) */
    s_step = ND_S_HTTP;
    svcSleepThread(150000000LL);

    /* 3. Serveur - ND12 : https d'abord, http ensuite, CHACUN son
       etape visible a l'ecran (avant : un seul "Serveur" opaque
       pendant 20-30 s). fail_at dit lequel des deux a lache. */
    nd_build_ping_url(g_settings.navidrome_url, g_settings.navidrome_user,
                      g_settings.navidrome_pass, url, (int)sizeof url);
    s_step = ND_S_HTTPS;
    okf = nd_http_get(url, resp, 2048, &status);
    if (!okf) {
        char alt[600];
        s_step = ND_S_SERVER;
        if (nd_http_variant(url, alt, (int)sizeof alt)) {
            s_err[0] = 0;
            okf = nd_http_get(alt, resp, 2048, &status);
            if (okf)
                s_prefer_http = 1;   /* http gagne : s'en souvenir (ND9) */
        }
    }
    if (!okf) {
        if (s_wifi_lost)
            snprintf(s_err, sizeof s_err, "WiFi perdu pendant la connexion");
        else if (s_err[0] == 0)
            snprintf(s_err, sizeof s_err, "Serveur injoignable");
        s_fail_at = s_step;          /* HTTPS ou SERVER : on sait lequel */
        s_step = ND_S_ERR;
        s_thread_running = false;
        return;
    }

    /* 4. Identification */
    s_step = ND_S_AUTH;
    if (nd_parse_status(resp, s_err, (int)sizeof s_err)) {
        char ver[32] = {0}, type[24] = {0}, sv[32] = {0};
        nd_json_str(resp, "version", ver, sizeof ver);
        nd_json_str(resp, "type", type, sizeof type);
        nd_json_str(resp, "serverVersion", sv, sizeof sv);
        snprintf(s_srvinfo, sizeof s_srvinfo, "%s %s",
                 type[0] ? type : "serveur", sv[0] ? sv : ver);
        s_step = ND_S_OK;
        s_persist = 1;             /* ND9 : connexion persistante */
        nd_open_browse(s_lcat);    /* ND7/9 : 1re liste (cache sinon fond) */
    } else {
        s_fail_at = ND_S_AUTH;
        s_step = ND_S_ERR;
    }
    s_thread_running = false;
}

/* ---------- M2 : views, jobs, messages ---------- */
static void nd_msg(const char *m, int ms, bool is_err)
{
    snprintf(s_msg, sizeof(s_msg), "%.250s", m ? m : "");
    s_msg_err = is_err;
    s_msg_until = osGetTime() + (u64)ms;
}

/* ---------- ND7 : navigation facon site web, 2 volets ----------
   Volet gauche  = les choix (listes GLOBALES, mode invite ND6G) :
     Tous (A-Z), Ajouts recents, Aleatoires, Favoris,
     Artistes, Morceaux.
   Volet droit   = albums / artistes / morceaux + metadonnees.
   A sur album -> ses pistes ; A sur artiste -> ses albums ;
   A sur morceau -> telechargement SD (.part puis rename).
   Marqueur : ND7                                                */
static const char *s_lcat_label[ND_LCATS + 1] = {
    "Tous (A-Z)", "Ajouts recents", "Aleatoires",
    "Favoris", "Artistes", "Morceaux (Aléat.)",
    "Deconnexion" };            /* ND9 : rend la main (session + eco WiFi) */

static void nd_net_ok(void)
{
    if (s_wifi_lost) {
        s_wifi_lost = 0;
        nd_msg("Connexion retablie", 2500, false);
    }
}

static void nd_net_fail(void)
{
    if (s_step != ND_S_OK) return;   /* connexion : l'ecran d'erreur gere
                                        (et le watchdog ACU couvre l'intro) */
    if (!s_wifi_lost) {
        s_wifi_lost = 1;
        s_fetch_ok = 0;
        nd_msg("Connexion perdue (WiFi ?)", 3500, true);
    }
}

/* Le WiFi est-il seulement utilisable ? Sans ce controle, une requete partait
   quand meme et l utilisateur attendait le delai d expiration (~10 s) pour
   lire "serveur injoignable", alors que le probleme est local et connu tout
   de suite. */
static bool nd_wifi_ready(void)
{
    u32 w = 0;
    if (wifi_manager_is_disabled()) return false;   /* radio coupee par l app */
    if (R_FAILED(ACU_GetWifiStatus(&w))) return false;
    return w >= 1;
}

static bool nd_job_start(void)
{
    s_job_cancel = false; /* RESET CRUCIAL : Ne s'annule jamais soi-meme ! */
    s_job_t0 = osGetTime();               /* ND11 : chrono sans taille connue */
    Thread t;
    extern void nd_job_thread(void *);
    if (s_job_running) return false;
    if (!nd_wifi_ready()) {
        s_wifi_lost = 1;
        s_job = ND_JOB_NONE;
        nd_msg("WiFi indisponible", 3000, true);
        return false;
    }
    s_job_running = 1;
    aptSetSleepAllowed(false); /* 3DSS-FIX: Bloque la veille */
    s_err[0] = 0;
    t = threadCreate(nd_job_thread, NULL, 64 * 1024, 0x30, -1, true);
    if (!t) {
        s_job_running = 0;
        s_job = ND_JOB_NONE;
        nd_msg("Thread reseau impossible", 2500, true);
        return false;
    }
    return true;
}

static bool nd_browse_start(int cat)
{
    char vb[160];
    if (s_job_running || s_step != ND_S_OK) return false;
    switch (cat) {
        case 0:  snprintf(vb, sizeof vb, "getAlbumList2.view?type=alphabeticalByName&size=%d&offset=%d&f=json", s_page_size, s_page_offset); break;
        case 1:  snprintf(vb, sizeof vb, "getAlbumList2.view?type=newest&size=%d&offset=%d&f=json", s_page_size, s_page_offset); break;
        case 2:  snprintf(vb, sizeof vb, "getAlbumList2.view?type=random&size=%d&offset=%d&f=json", s_page_size, s_page_offset); break;
        case 3:  snprintf(vb, sizeof vb, "getStarred2.view?f=json"); break;
        case 4:  snprintf(vb, sizeof vb, "getArtists.view?f=json"); break;
        default: snprintf(vb, sizeof vb, "getRandomSongs.view?size=%d&f=json", s_page_size); break;
    }
    nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user,
               g_settings.navidrome_pass, s_job_url, (int)sizeof s_job_url);
    s_job_kind = ND_V_BROWSE;
    s_job_lcat = cat;
    s_fetched_lcat = cat;
    s_job = ND_JOB_FETCH;
    return nd_job_start();
}

/* ND9 : ouvre une categorie : cache d'abord, reseau sinon ; si un job
   tourne deja, la demande est mise en attente (non bloquant). */
/* Les categories aleatoires (2 = albums au hasard, 5 = morceaux au hasard) ne
   sont jamais servies depuis le cache : sinon on retombait indefiniment sur le
   meme tirage, et "Aleatoire" ne rebrassait plus rien. */
static bool nd_cat_is_random(int cat) { return cat == 2 || cat == 5; }

static void nd_open_browse(int cat)
{
    s_page_offset = 0; /* Reset pagination lors du changement de categorie */
    s_fetched_lcat = cat;
        if (cat == 4 && s_artists_raw_json[0] != 0) {
        nd_artists_paginate_local();
        return;
    }
    if (cat == 3) {
        /* Favoris : purement LOCAL, jamais de reseau. Rechargee direct du
           fichier a chaque ouverture, donc toujours a jour (rien a mettre en
           cache, rien qui puisse manquer). */
        nd_fav_load_into_items();
        return;
    }
    {
        char key[8];
        snprintf(key, sizeof key, "L%d", cat);
        /* Meme le cache normal s applique aux categories aleatoires : sinon
           chaque simple passage du curseur dessus (nd_lsel_move, y compris en
           ne faisant que traverser vers une autre categorie) relancait un
           tirage. Le rebrassage explicite reste sur A (voir plus bas), pas
           sur la simple navigation. */
        if (nd_cache_fill(key) >= 0) return;
    }
    if (!nd_browse_start(cat)) {
        if (s_job_running) s_pending_lcat = cat;
    }
}

static void nd_view_fetch(int kind, const char *id)
{
    char vb[96];
    if (kind == ND_V_TRACKS)
        snprintf(vb, sizeof vb, "getAlbum.view?id=%s&f=json", id ? id : "");
    else
        snprintf(vb, sizeof vb, "getArtist.view?id=%s&f=json", id ? id : "");
    nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user,
               g_settings.navidrome_pass, s_job_url, (int)sizeof s_job_url);
    s_job_kind = kind;
    snprintf(s_job_id, sizeof s_job_id, "%s", id ? id : "");
    s_job = ND_JOB_FETCH;
    nd_job_start();
}

/* ---------- recherche (ND : bouton Y) ---------- */
/* La vue de resultats reutilise ND_V_TRACKS : le thread de job sait deja
   collecter la cle "song" pour ce kind, et A = ecouter / X = telecharger
   fonctionnent donc sans rien changer d autre. L id bidon ci-dessous sert
   uniquement a ce que la vue se reconnaisse quand la reponse arrive. */
#define ND_SEARCH_ID "~search"
static char s_search_q[64] = {0};

static void nd_url_escape(const char *in, char *out, int outsz)
{
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (int i = 0; in[i] && o < outsz - 4; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out[o++] = (char)c;
        else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

static void nd_search_start(const char *query)
{
    char vb[320], esc[200]; /* requete max 63 car. -> 189 apres echappement */
    if (s_job_running || s_step != ND_S_OK) return;
    nd_url_escape(query, esc, (int)sizeof esc);
    snprintf(vb, sizeof vb,
             "search3.view?query=%s&artistCount=0&albumCount=0&songCount=%d&f=json",
             esc, ND_MAX_ITEMS);
    nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user,
               g_settings.navidrome_pass, s_job_url, (int)sizeof s_job_url);
    s_job_kind = ND_V_TRACKS;
    snprintf(s_job_id, sizeof s_job_id, "%s", ND_SEARCH_ID);
    s_job = ND_JOB_FETCH;
    nd_job_start();
}

/* Saisie au clavier systeme. Bloquant (applet), donc appele depuis la phase
   d entrees du thread principal, jamais entre un C3D_FrameBegin/End. */
static bool nd_keyboard_query(char *out, int outsz)
{
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, outsz - 1);
    swkbdSetHintText(&kb, "Titre, artiste ou album");
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    return swkbdInputText(&kb, out, (size_t)outsz) == SWKBD_BUTTON_CONFIRM;
}

static void nd_open_search(void)
{
    char q[64];
    if (s_job_running || s_step != ND_S_OK) return;
    if (!nd_keyboard_query(q, (int)sizeof q) || !q[0]) return;
    snprintf(s_search_q, sizeof s_search_q, "%s", q);

    /* La recherche s installe toujours juste au-dessus de la racine, quelle
       que soit la profondeur ou on se trouvait. */
    s_depth = 1;
    s_stack[1].kind = ND_V_TRACKS;
    snprintf(s_stack[1].id, sizeof s_stack[1].id, "%s", ND_SEARCH_ID);
    snprintf(s_stack[1].title, sizeof s_stack[1].title, "Recherche : %s", q);
    s_stack[1].sel = 0; s_stack[1].scroll = 0; s_stack[1].pane = 0;
    s_item_count = 0;
    nd_search_start(q);
}

/* Vide le dossier temporaire. Appele a la fermeture de l application. */
void nd_temp_clear(void)
{
    DIR *d = opendir(ND_TMP_DIR);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char f[320];
        if (e->d_name[0] == '.') continue;
        snprintf(f, sizeof f, "%s/%s", ND_TMP_DIR, e->d_name);
        remove(f);
    }
    closedir(d);
}

/* Supprime tout le contenu de Temp SAUF le fichier (et son marqueur .ok) qui
   va servir a la prochaine lecture. Le morceau precedent ne reste donc plus
   sur la carte SD une fois qu on change de piste - seul celui EN COURS
   d ecoute y occupe de la place. */
static void nd_temp_prune_except(const char *keep_path)
{
    DIR *d = opendir(ND_TMP_DIR);
    struct dirent *e;
    char keep_name[256] = {0};
    const char *kn;
    if (!d) return;
    kn = strrchr(keep_path, '/');
    snprintf(keep_name, sizeof keep_name, "%s", kn ? kn + 1 : keep_path);
    while ((e = readdir(d)) != NULL) {
        char f[320], base[256], *dot;
        if (e->d_name[0] == '.') continue;
        snprintf(base, sizeof base, "%s", e->d_name);
        dot = strstr(base, ".ok");   /* le marqueur partage le nom du mp3 */
        if (dot && dot[3] == 0) *dot = 0;
        if (!strcmp(base, keep_name)) continue;
        snprintf(f, sizeof f, "%s/%s", ND_TMP_DIR, e->d_name);
        remove(f);
    }
    closedir(d);
}

/* ---------- lecture en streaming progressif ---------- */
/* Avance a accumuler avant de lancer la lecture. A 160 kbps (le debit impose
   au serveur ci-dessous), 320 Ko ~ 16 s de musique : de quoi encaisser un
   reseau qui faiblit sans que la lecture ne rattrape le telechargement. */
#define ND_PREBUFFER_BYTES (320u * 1024u)
static volatile int s_stream_started = 0;
static int          s_stream_dur     = 0;   /* duree annoncee par le serveur */

/* Marqueur de fichier temporaire complet : sans lui, impossible de distinguer
   un morceau entierement recu d un reste de transfert interrompu. */
static void nd_temp_mark(const char *path, int done)
{
    char m[300];
    snprintf(m, sizeof m, "%s.ok", path);
    if (done) { FILE *f = fopen(m, "wb"); if (f) fclose(f); }
    else      remove(m);
}

static bool nd_temp_is_complete(const char *path)
{
    char m[300];
    FILE *f;
    snprintf(m, sizeof m, "%s.ok", path);
    f = fopen(m, "rb");
    if (!f) return false;
    fclose(f);
    f = fopen(path, "rb");          /* le .ok sans le mp3 ne vaut rien */
    if (!f) { remove(m); return false; }
    fclose(f);
    return true;
}

/* Demarre la lecture des que le pre-tampon est atteint, puis ne fait plus
   rien : le decodeur continue de lire le fichier pendant qu il grandit. */
static void nd_stream_chunk(void)
{
    if (s_stream_started) return;
    if (s_dl_have < ND_PREBUFFER_BYTES) return;
    audio_stop();
    /* IMPORTANT : ne pas activer l attente AVANT audio_load(). L ouverture a
       lieu sur CE thread, qui est aussi celui qui telecharge : s il se mettait
       a attendre des octets ici, plus personne ne les produirait. L attente
       n est armee qu une fois le fichier ouvert, car les lectures suivantes se
       font depuis le thread audio, lui bien distinct du telechargement. */
    if (audio_load(s_job_path) == 0) {
        audio_set_network_source(true);   /* pas d EQ/SoundTouch sur un flux ND */
        audio_stream_begin();
        if (s_stream_dur > 0) audio_set_duration_hint(s_stream_dur);
        audio_play();
        s_stream_started = 1;
    s_transition_active = false;
        s_playing_nd = 1;
        s_player_view = true;
        snprintf(s_play_title, sizeof s_play_title, "%s", s_job_title);
    }
    /* si l ouverture echoue (trop peu de donnees, format inattendu), on ne
       marque rien : le transfert va au bout et la voie classique reprend */
}

/* Reprise d un titre deja recupere pour l ecoute : plutot que de retelecharger,
   on recopie le fichier du dossier Temp vers la bibliotheque. Rempli par
   nd_start_download2(), consomme par le thread de job. */
static char s_job_temp_src[320] = {0};

static bool nd_copy_file(const char *src, const char *dst)
{
    static u8 cbuf[16384];
    FILE *in, *out;
    size_t n;
    bool ok = true;
    in = fopen(src, "rb");
    if (!in) return false;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    while ((n = fread(cbuf, 1, sizeof cbuf, in)) > 0)
        if (fwrite(cbuf, 1, n, out) != n) { ok = false; break; }
    fclose(in);
    fclose(out);
    if (!ok) remove(dst);
    return ok;
}

void nd_job_thread(void *arg)
{
    (void)arg;
    if (s_job == ND_JOB_FETCH) {
        if (!nd_fetch(s_job_url, s_resp_job, (int)sizeof s_resp_job, NULL)) {
            s_fetch_ok = 0;
            if (s_err[0] == 0)
                snprintf(s_err, sizeof s_err, "Serveur injoignable");
            nd_msg(s_err, 3500, true);
        } else if (!nd_parse_status(s_resp_job, s_err, (int)sizeof s_err)) {
            s_fetch_ok = 0;
            nd_msg(s_err, 3500, true);
        } else {
            const char *key, *ft, *fs;
            int cnt, i;
            if (s_job_kind == ND_V_BROWSE) {
                if (s_job_lcat < 4)       { key = "album";  ft = "name";  fs = "artist"; }
                else if (s_job_lcat == 4) { key = "artist"; ft = "name";  fs = NULL;     }
                else                      { key = "song";   ft = "title"; fs = "artist"; }
            } else if (s_job_kind == ND_V_TRACKS) {
                key = "song"; ft = "title"; fs = "artist";
            } else {
                key = "album"; ft = "name"; fs = "artist";
            }
            if (strcmp(s_job_id, "~endless") == 0) {
                NdItem temp_song;
                int collected = nd_collect(s_resp_job, "song", "title", "artist", &temp_song, 1);
                if (collected > 0) {
                    if (s_np_list_count >= ND_MAX_ITEMS) {
                        for (int k = 1; k < s_np_list_count; k++) s_np_list[k-1] = s_np_list[k];
                        s_np_list_count--; s_np_idx--;
                    }
                    s_np_list[s_np_list_count] = temp_song;
                    s_np_list_count++; s_np_idx++;
                    s_fetch_ok = 1;
                    nd_cover_request_now(s_np_list[s_np_idx].cover);
                    s_pending_play_item = s_np_list[s_np_idx];
                    s_pending_play_valid = true;
                } else { s_fetch_ok = 0; s_play_pending = 0; }
                goto job_end;
            }
            if (s_job_kind == ND_V_BROWSE && s_job_lcat == 4) {
                snprintf(s_artists_raw_json, sizeof s_artists_raw_json, "%s", s_resp_job);
                cnt = nd_collect_ex(s_resp_job, "artist", "name", NULL, s_items, s_page_size, s_page_offset);
            } else {
                cnt = nd_collect(s_resp_job, key, ft, fs, s_items, ND_MAX_ITEMS);
            }
            if (s_job_kind == ND_V_BROWSE && s_job_lcat == 4) {
                for (i = 0; i < cnt; i++)
                    if (s_items[i].num >= 0)
                        snprintf(s_items[i].sub, sizeof s_items[i].sub,
                                 "%d album%s", s_items[i].num,
                                 s_items[i].num > 1 ? "s" : "");
            } else if (s_job_kind != ND_V_BROWSE ? 0 : (s_job_lcat == 5)) {
                for (i = 0; i < cnt; i++)
                    if (s_items[i].num >= 0) {
                        char d[16];
                        snprintf(d, sizeof d, " %d:%02d",
                                 s_items[i].num / 60, s_items[i].num % 60);
                        strncat(s_items[i].sub, d,
                                sizeof s_items[i].sub - strlen(s_items[i].sub) - 1);
                    }
            } else if (s_job_kind == ND_V_TRACKS) {
                for (i = 0; i < cnt; i++)
                    if (s_items[i].num >= 0) {
                        char d[16];
                        snprintf(d, sizeof d, "  %d:%02d",
                                 s_items[i].num / 60, s_items[i].num % 60);
                        strncat(s_items[i].sub, d,
                                sizeof s_items[i].sub - strlen(s_items[i].sub) - 1);
                    }
            }
            /* applique seulement si la vue n'a pas change pendant le fetch */
            s_fetch_ok = 1;
            {   /* ND9 : memoire pour reouverture instantanee */
                char key[48];
                if (s_job_kind == ND_V_BROWSE)
                    snprintf(key, sizeof key, "L%d", s_job_lcat);
                else
                    snprintf(key, sizeof key, "%s%s",
                             s_job_kind == ND_V_TRACKS ? "T" : "A", s_job_id);
                nd_cache_put(key, cnt);
            }
            if (s_job_kind == ND_V_BROWSE) {
                if (s_depth == 0 && s_stack[0].kind == ND_V_BROWSE &&
                    s_lcat == s_job_lcat) {
                    s_item_count = cnt;
                    if (s_stack[0].sel >= cnt)
                        s_stack[0].sel = (cnt > 0) ? cnt - 1 : 0;
                    if (cnt == 0) nd_msg("Liste vide", 2000, false);
                }
            } else {
                NdView *v = &s_stack[s_depth];
                if (v->kind == s_job_kind && !strcmp(v->id, s_job_id)) {
                    s_item_count = cnt;
                    if (v->sel >= cnt) v->sel = (cnt > 0) ? cnt - 1 : 0;
                    if (cnt == 0) nd_msg("Liste vide", 2000, false);
                }
            }
        }
    } else if (s_job == ND_JOB_DL) {
        /* 3DSS-FIX: Le thread de fond telecharge la pochette EN PREMIER (avant le streaming).
           Comme c est en arriere-plan, l UI reste a 60 FPS sans aucun freeze.
           La pochette (~160px) se charge en ~200ms, puis le streaming commence.
           MAIS seulement pour un telechargement "pour de vrai" (SD, s_play_after
           faux) : pour une LECTURE (streaming), ce bloc fait une requete
           reseau bloquante PUIS reecrit le fichier entier (embed_cover_mp3_exact
           : lecture complete + fwrite + remove + sleep 20ms + rename) avant
           meme que nd_dl_file() pour la musique ne demarre - et sans jamais
           verifier s_job_cancel. Changer de piste pendant ce pre-chargement
           n etait donc pas pris en compte avant que cette etape (duree reseau
           non bornee) ne finisse : exactement le defaut que le thread de
           pochette dedie existe pour eviter, mais ici dans l autre sens (la
           pochette retardait la musique). Le thread de pochette independant
           s occupe deja de la recuperer en parallele pour l affichage - pas
           besoin de la graver dans le fichier avant de jouer. */
        if (!s_play_after &&
            s_job_cover[0] && strcmp(s_cover_id, s_job_cover) != 0 && !s_cover_inited) {
            /* --- 3DSS: On verifie d abord si la pochette existe deja sur la SD pour l'afficher instantanément --- */
            char cache_path[256];
            snprintf(cache_path, sizeof cache_path, "sdmc:/3DSoundShell/Temp/pochettes/%s.png", s_job_cover);
            FILE *cf = fopen(cache_path, "rb");
            if (cf) {
                fseek(cf, 0, SEEK_END);
                long cache_size = ftell(cf);
                fseek(cf, 0, SEEK_SET);
                if (cache_size > 0 && cache_size <= (long)ND_COVER_CAP) {
                    if (fread(s_cover_raw, 1, cache_size, cf) == (size_t)cache_size) {
                        s_cover_raw_len = (int)cache_size;
                        snprintf(s_cover_id, sizeof s_cover_id, "%s", s_job_cover);
                        s_cover_failed = 0;
                        fclose(cf);
                        goto skip_cover_dl;
                    }
                }
                fclose(cf);
            }
            char cov_url[600], vb[80];
            snprintf(vb, sizeof vb, "getCoverArt.view?id=%s&size=160", s_job_cover);
            nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user,
                       g_settings.navidrome_pass, cov_url, (int)sizeof cov_url);
            int n = 0;
            char cerr[64] = {0};
            if (nd_fetch_bin(cov_url, s_cover_raw, ND_COVER_CAP, &n, cerr, (int)sizeof cerr) && n > 0) {
                s_cover_raw_len = n;
                snprintf(s_cover_id, sizeof s_cover_id, "%s", s_job_cover);
                s_cover_failed = 0;
                
                /* GRAVURE EXCLUSIVE DANS LE MP3 (Pas de fichiers PNG/JPG parasites) */
                embed_cover_mp3_exact(s_job_path, s_cover_raw, (size_t)n);
                /* --- 3DSS: Sauvegarde de la pochette pour eviter qu elle ne disparaisse au retour --- */
                mkdir("sdmc:/3DSoundShell", 0777);
                mkdir("sdmc:/3DSoundShell/Temp", 0777);
                mkdir("sdmc:/3DSoundShell/Temp/pochettes", 0777);
                char cache_path[256];
                snprintf(cache_path, sizeof cache_path, "sdmc:/3DSoundShell/Temp/pochettes/%s.png", s_job_cover);
                FILE *pf = fopen(cache_path, "wb");
                if (pf) { fwrite(s_cover_raw, 1, n, pf); fclose(pf); }
            }
            skip_cover_dl:;
        }
        FILE *t;
        mkdir("sdmc:/Music", 0777);        /* ND10b : SD:/Music/Navidrome */
        mkdir(ND_DL_DIR, 0777);
        /* Deja dans Temp et complet : on rejoue sans retelecharger. */
        if (s_play_after && nd_temp_is_complete(s_job_path)) {
            audio_stop();
            s_cover_failed = 0; s_cover_inited = 0; s_audio_rc = audio_load(s_job_path);
            if (s_audio_rc == 0) {
                audio_set_network_source(true);   /* pas d EQ/SoundTouch sur un flux ND */
                audio_play();
                s_playing_nd = 1;
                s_player_view = true;
                snprintf(s_play_title, sizeof s_play_title, "%s", s_job_title);
                goto job_end;
            }
            nd_temp_mark(s_job_path, 0);   /* illisible : on le refera */
        }
        /* Deja recupere pendant une ecoute : on recopie depuis Temp, sans
           refaire le moindre transfert reseau. */
        if (!s_play_after && s_job_temp_src[0]) {
            if (nd_copy_file(s_job_temp_src, s_job_path)) {
                const char *nm = strrchr(s_job_path, '/');
                nm = nm ? nm + 1 : s_job_path;
                snprintf(s_msg, sizeof s_msg, "Telecharge : %.90s", nm);
                s_msg_err = false;
                s_msg_until = osGetTime() + 2800;
            } else {
                nd_msg("Copie depuis Temp impossible", 3000, true);
            }
            goto job_end;
        }
        t = fopen(s_job_path, "rb");
        if (t) fclose(t);
        /* Cas 1 : fichier deja la, telechargement simple -> rien a faire */
        if (t && !s_play_after) {
            nd_msg("Deja telecharge", 2500, false);
            goto job_end;
        }
        /* Cas 2 : fichier complet deja la, lecture directe -> jouer et sortir */
        if (t && s_play_after) {
            audio_stop();
            s_cover_failed = 0; s_cover_inited = 0;
            s_audio_rc = audio_load(s_job_path);
            if (s_audio_rc == 0) {
                audio_set_network_source(true);
                audio_play();
                s_playing_nd = 1;
                s_player_view = true;
                snprintf(s_play_title, sizeof s_play_title, "%s", s_job_title);
            }
            goto job_end;
        }
        /* Cas 2+3 : telechargement / streaming (fichier existe ou non) */
        {
            int okdl;
            if (s_play_after) {
                s_stream_started = 0;
                nd_temp_mark(s_job_path, 0);
                s_dl_chunk_cb = nd_stream_chunk;
                okdl = nd_dl_file(s_job_url, s_job_path, s_err,
                                  (int)sizeof s_err);
                s_dl_chunk_cb = NULL;
                audio_stream_end();
                if (okdl) nd_temp_mark(s_job_path, 1);
                if (s_stream_started) {
                    if (!okdl && !s_job_cancel)
                        nd_msg("Flux interrompu", 3000, true);
                    goto job_end;
                }
            } else {
                okdl = nd_dl_file(s_job_url, s_job_path, s_err,
                                  (int)sizeof s_err);
            }
            if (!okdl && s_play_after && !s_job_cancel) {
                s_err[0] = 0;
                okdl = nd_dl_file(s_job_url, s_job_path, s_err,
                                  (int)sizeof s_err);
                if (okdl) nd_temp_mark(s_job_path, 1);
            }
            if (okdl) {
                if (s_play_after) {
                    audio_stop();
                    s_cover_failed = 0; s_cover_inited = 0;
                    s_audio_rc = audio_load(s_job_path);
                    if (s_audio_rc == 0) {
                        audio_set_network_source(true);
                        audio_play();
                        s_playing_nd = 1;
                        s_transition_active = false;
                        s_player_view = true;
                        snprintf(s_play_title, sizeof s_play_title, "%s",
                                 s_job_title);
                    } else {
                        char le[96];
                        snprintf(le, sizeof le,
                                 "Lecture impossible (code %08lX, %lu Ko)",
                                 (unsigned long)s_audio_rc,
                                 (unsigned long)(s_dl_have / 1024));
                        nd_msg(le, 5000, true);
                    }
                } else {
                    const char *nm = strrchr(s_job_path, '/');
                    nm = nm ? nm + 1 : s_job_path;
                    char tmsg[120];
                    snprintf(tmsg, sizeof tmsg, "Telecharge : %.90s", nm);
                    nd_msg(tmsg, 3500, false);
                }
            } else if (!s_job_cancel) {
                if (s_err[0] == 0)
                    snprintf(s_err, sizeof s_err, "Telechargement echoue");
                nd_msg(s_err, 3500, true);
            }
        }
        s_play_after = 0;
    }
    /* ND_JOB_COVER n est plus traite ici : voir nd_cover_job_thread, un thread
       dedie et independant de s_job_running (sinon la pochette attendait la
       fin ENTIERE du telechargement de la musique - potentiellement toute la
       duree du morceau en streaming progressif - avant de pouvoir seulement
       commencer sa propre requete). */
job_end:
    { char tmp_clean[520]; snprintf(tmp_clean, sizeof tmp_clean, "%s.id3tmp", s_job_path); remove(tmp_clean); }
    // TEST FIX: embed_cover_mp3_exact commente temporairement
    s_play_pending = 0;
    s_transition_active = false;
    s_job = ND_JOB_NONE;
    s_job_running = 0;
    s_job_cancel = false;
    aptSetSleepAllowed(true); /* 3DSS-FIX: Reautorise la veille */
    if (s_pending_play_valid) {
        s_pending_play_valid = false;
        svcSleepThread(150000000ULL); /* Pause 150ms pour liberation du port HTTP 3DS */
        NdItem pending_item = s_pending_play_item;
        nd_start_download2(&pending_item, 1);
    }
    if (s_pending_lcat >= 0) {          /* ND9 : categorie demandee pendant le job */
        int c = s_pending_lcat;
        s_pending_lcat = -1;
        if (s_depth == 0 && c == s_lcat && s_step == ND_S_OK)
            nd_open_browse(c);
    }
}

static char s_sync_resp[128 * 1024]; /* 128KB pour eviter tout risque d overflow */

static void nd_sync_fetch_thread_func(void *arg)
{
    (void)arg;
    char vb[96], url[600];
    const char *key, *ft, *fs;
    int cnt;

    if (s_sync_fetch_kind == ND_V_TRACKS)
        snprintf(vb, sizeof vb, "getAlbum.view?id=%s&f=json", s_sync_fetch_id);
    else if (s_sync_fetch_kind == ND_V_ARTALB)
        snprintf(vb, sizeof vb, "getArtist.view?id=%s&f=json", s_sync_fetch_id);
    else {
        s_sync_fetch_active = false;
        s_ui_fetch_pending = false;
        return;
    }

    nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user,
               g_settings.navidrome_pass, url, (int)sizeof url);

    if (nd_fetch(url, s_sync_resp, (int)sizeof s_sync_resp, NULL)) {
        if (s_sync_fetch_kind == ND_V_TRACKS) { key = "song"; ft = "title"; fs = "artist"; }
        else                                  { key = "album"; ft = "name"; fs = "artist"; }
        cnt = nd_collect(s_sync_resp, key, ft, fs, s_items, ND_MAX_ITEMS);
        s_item_count = cnt;
    }

    s_sync_fetch_active = false;
    s_ui_fetch_pending = false; /* Remet le WiFi de la musique a 100% de vitesse */
}

static void nd_push(int kind, const char *id, const char *title)
{
    if (s_depth >= 2) return;
    s_depth++;
    s_stack[s_depth].kind = kind;
    snprintf(s_stack[s_depth].id, sizeof s_stack[s_depth].id, "%s", id ? id : "");
    snprintf(s_stack[s_depth].title, sizeof s_stack[s_depth].title,
             "%s", title ? title : "");
    s_stack[s_depth].sel = 0;
    s_stack[s_depth].scroll = 0;
    s_stack[s_depth].pane = 0;
    s_item_count = 0;
    {   /* ND9 : reouverture instantanee si la vue est en cache */
        char key[48];
        snprintf(key, sizeof key, "%s%s",
                 kind == ND_V_TRACKS ? "T" : "A", s_stack[s_depth].id);
        if (nd_cache_fill(key) < 0) {
            if (s_job_running && !s_sync_fetch_active) {
                /* Garde manquante a l origine : sans elle, naviguer vite dans
                   deux dossiers differents pendant qu un telechargement
                   tourne deja lancait DEUX threads nd_sync_fetch_thread_func
                   en parallele, tous les deux ecrivant s_items/s_item_count
                   sans aucune coordination entre eux. */
                s_ui_fetch_pending = true;
                s_sync_fetch_active = true;
                s_sync_fetch_kind = kind;
                snprintf(s_sync_fetch_id, sizeof s_sync_fetch_id, "%s", s_stack[s_depth].id);
                /* Lancement sur le thread secondaire 0x31 pour ne pas figer l ecran principal */
                Thread t = threadCreate(nd_sync_fetch_thread_func, NULL, 32 * 1024, 0x31, -1, true);
                if (!t) {
                    s_sync_fetch_active = false;
                    s_ui_fetch_pending = false;
                }
            } else {
                nd_view_fetch(kind, s_stack[s_depth].id);
            }
        }
    }
}

static void nd_pop(void)
{
    if (s_depth <= 0) return;
    s_depth--;
    s_item_count = 0;
    nd_open_browse(s_lcat);      /* ND9 : cache sinon re-fetch, positions gardees */
}

/* ND10b : formats que les moteurs audio du projet savent lire
   (mpg123, stb_vorbis, dr_flac, dr_wav, opusfile). PAS de m4a/aac :
   pour ceux-la on demande a Navidrome de transcoder en mp3. */
static int nd_sfx_is(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

static int nd_sfx_playable(const char *sfx)
{
    return nd_sfx_is(sfx, "mp3")  || nd_sfx_is(sfx, "mp2")
        || nd_sfx_is(sfx, "ogg")  || nd_sfx_is(sfx, "oga")
        || nd_sfx_is(sfx, "flac") || nd_sfx_is(sfx, "wav")
        || nd_sfx_is(sfx, "opus");
}

/* Le titre est-il deja dans Temp, complet, recupere lors d une ecoute ? Si
   oui, renseigne le nom de destination (le mp3 160k) et arme la reprise. */
static bool nd_reuse_temp_for_download(const NdItem *it, char *name, int namesz)
{
    char tname[128];
    nd_sanitize(it->title, "mp3", tname, (int)sizeof tname);
    snprintf(s_job_temp_src, sizeof s_job_temp_src, "%s/%s", ND_TMP_DIR, tname);
    if (!nd_temp_is_complete(s_job_temp_src)) { s_job_temp_src[0] = 0; return false; }
    snprintf(name, (size_t)namesz, "%s", tname);
    return true;
}

static void nd_start_download2(const NdItem *it, int play_now)
{
    char name[128];
    char view[110];
    /* 3DSS-FIX S8 : Check early multi-extension. Le fichier sur la SD
       peut etre en .mp3 (transcode) alors que it->sfx est "m4a" ou "opus".
       On essaie plusieurs extensions avant de lancer le moindre job. */
    {
        char path_check[320];
        char local_target[320];
        bool is_local = 0;
        bool is_real_dl = 0;  /* true seulement si trouve dans ND_DL_DIR (le vrai telechargement), pas juste en Temp */
        const char *try_ext[4];
        int n_ext = 0;
        try_ext[n_ext++] = "mp3";
        if (it->sfx[0] && !nd_sfx_is(it->sfx, "mp3"))
            try_ext[n_ext++] = it->sfx;
        if (!nd_sfx_is(it->sfx, "flac"))
            try_ext[n_ext++] = "flac";
        try_ext[n_ext] = NULL;

        for (int ei = 0; ei < n_ext && !is_local; ei++) {
            nd_sanitize(it->title, try_ext[ei], name, (int)sizeof name);
            snprintf(path_check, sizeof path_check, "%s/%s", ND_DL_DIR, name);
            FILE *fc = fopen(path_check, "rb");
            if (fc) {
                fclose(fc);
                snprintf(local_target, sizeof local_target, "%s", path_check);
                is_local = 1;
                is_real_dl = 1;
            } else {
                snprintf(path_check, sizeof path_check, "%s/%s", ND_TMP_DIR, name);
                if (nd_temp_is_complete(path_check)) {
                    snprintf(local_target, sizeof local_target, "%s", path_check);
                    is_local = 1;
                    /* is_real_dl reste faux : ce n est que le temporaire de
                       lecture, pas encore une vraie copie dans Music/Navidrome. */
                }
            }
        }

        if (is_local && play_now) {
            audio_stop();
            if (audio_load(local_target) == 0) {
                audio_set_network_source(true);
                audio_play();
                s_playing_nd = 1;
                s_player_view = true;
                s_playing_from_sd = (strstr(local_target, ND_DL_DIR) != NULL);
                snprintf(s_play_title, sizeof s_play_title, "%s", it->title);
                nd_cover_request_now(it->cover);
                s_play_pending = 0;
                s_stream_started = 1;
                s_transition_active = false;
                return;
            }
        }
        /* UNIQUEMENT si le fichier est deja le VRAI telechargement (dans
           Music/Navidrome) : sinon ce court-circuit affichait "Deja
           telecharge" et s arretait la, pour un morceau qui n etait en
           realite que dans Temp (ecoute en streaming) - sans jamais copier
           quoi que ce soit vers Music/Navidrome. Le code plus bas
           (nd_reuse_temp_for_download + nd_copy_file) sait deja faire
           exactement cette copie - pochette et metadonnees incluses, sans
           retelecharger depuis le serveur - mais n etait jamais atteint car
           ce bloc-ci rendait la main avant. */
        if (is_real_dl && !play_now) {
            nd_msg("Deja telecharge", 2500, false);
            return;
        }
    }


    s_playing_from_sd = false; /* Streaming depuis internet */
    if (s_job_running) {
        if (!play_now) return;
        s_job_cancel = true;
        /* Debloque rapidement le telechargement en cours (sinon on attend le
           prochain point de sondage de s_job_cancel dans la boucle reseau) -
           SANS fermer le handle ici : s_active_ctx pointe vers la pile du
           thread de job, toujours vivant a cet instant. nd_dl_attempt fera
           lui-meme son nettoyage normal (nd_close_http) en relisant
           s_job_cancel. s_ctx_is_open n etait jamais mis a vrai : cette
           branche etait du code mort qui, si un jour "corrige" pour
           s activer, aurait referme ce handle en double (une fois ici, une
           fois cote thread de job) sans aucune coordination entre les deux -
           voir le meme correctif applique a nd_abort_active_download(). */
        if (s_active_ctx) httpcCancelConnection(s_active_ctx);
        s_pending_play_item = *it;
        s_pending_play_valid = true;
        return;
    }
    s_job_cancel = false;           /* nouveau job : jamais annule d avance */
    s_job_temp_src[0] = 0;
    if (play_now) {
        /* Le temporaire porte le VRAI titre du morceau (et non plus un
           "nd_play.mp3" ecrase a chaque lecture) : un titre deja ecoute reste
           donc disponible dans Temp, et le "vrai" telechargement n a plus qu a
           reprendre le fichier tel quel.
           ND10b : TOUJOURS transcode en mp3 160k (m4a/opus/whatever
           -> lisible par mpg123, demarrage rapide). */
        nd_sanitize(it->title, "mp3", name, (int)sizeof name);
        snprintf(view, sizeof view,
                 "stream.view?id=%s&format=mp3&maxBitRate=160", it->id);
        s_stream_dur = it->num > 0 ? it->num : 0;
    } else if (nd_reuse_temp_for_download(it, name, (int)sizeof name)) {
        /* Le titre a deja ete recupere pour l ecoute : on reprendra ce
           fichier au lieu de refaire tout le transfert. */
        view[0] = 0;
    } else if (!nd_sfx_playable(it->sfx)) {
        /* ND10b : m4a/aac etc -> telechargement TRANSCODE en mp3 320k
           pour que le fichier soit lisible dans l'app aussi. */
        nd_sanitize(it->title, "mp3", name, (int)sizeof name);
        snprintf(view, sizeof view,
                 "stream.view?id=%s&format=mp3&maxBitRate=320", it->id);
    } else {
        nd_sanitize(it->title, it->sfx, name, (int)sizeof name);
        snprintf(view, sizeof view, "download.view?id=%s", it->id);
    }
    if (play_now) {   /* ND28 : temp dans 3DSoundShell/Temp */
        mkdir("sdmc:/3DSoundShell", 0777);
        mkdir(ND_TMP_DIR, 0777);
        snprintf(s_job_path, sizeof s_job_path, "%s/%s", ND_TMP_DIR, name);
        /* Ne garder que le morceau qu on s apprete a jouer : le precedent ne
           sert plus a rien une fois qu on a change de piste et prenait de la
           place pour rien sur la carte SD. */
        nd_temp_prune_except(s_job_path);
    } else {
        snprintf(s_job_path, sizeof s_job_path, "%s/%s", ND_DL_DIR, name);
    }
    snprintf(s_job_title, sizeof s_job_title, "%s", it->title);
    snprintf(s_job_cover, sizeof s_job_cover, "%s", it->cover[0] ? it->cover : "");
    s_play_after = play_now;
    if (play_now) s_play_pending = 1;
    nd_api_url(g_settings.navidrome_url, view, g_settings.navidrome_user,
               g_settings.navidrome_pass, s_job_url, (int)sizeof s_job_url);
    s_job = ND_JOB_DL;
    if (!nd_job_start()) s_play_pending = 0;  /* sinon le drapeau resterait arme */
}

static void nd_start_download(const NdItem *it) { nd_start_download2(it, 0); }

static void nd_cover_load_sync(const char *want)
{
    if (!want || !*want) return;
    char cov_url[600];
    char vb[80];
    snprintf(vb, sizeof vb, "getCoverArt.view?id=%s&size=160", want);
    nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user, g_settings.navidrome_pass, cov_url, (int)sizeof cov_url);

    int n = 0;
    char cerr[64] = {0};
    if (nd_fetch_bin(cov_url, s_cover_raw, ND_COVER_CAP, &n, cerr, (int)sizeof cerr) && n > 0) {
        int w = 0, h = 0, ch = 0;
        unsigned char *px = stbi_load_from_memory(s_cover_raw, n, &w, &h, &ch, 4);
        if (px && w > 0 && h > 0 && w <= 256 && h <= 256) {
            u32 tw = 256, th = 256;
            if (!s_cover_tex_live) {
                if (C3D_TexInit(&s_cover_tex, 256, 256, GPU_RGBA8)) {
                    s_cover_tex_live = 1;
                }
            }
            if (s_cover_tex_live) {
                u8 *dst = (u8*)s_cover_tex.data;
                memset(dst, 0, (size_t)tw * th * 4);
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        u32 tile = ((u32)y / 8) * (tw / 8) + ((u32)x / 8);
                        u32 mx = (u32)x & 7, my = (u32)y & 7;
                        u32 mi = (mx & 1) | ((my & 1) << 1) | ((mx & 2) << 1) | ((my & 2) << 2) | ((mx & 4) << 2) | ((my & 4) << 3);
                        u32 di   = (tile * 64 + mi) * 4;
                        const u8 *sp = px + ((size_t)y * w + x) * 4;
                        dst[di] = sp[3]; dst[di+1] = sp[2];
                        dst[di+2] = sp[1]; dst[di+3] = sp[0];
                    }
                }
                GSPGPU_FlushDataCache(s_cover_tex.data, (size_t)tw * th * 4);
                C3D_TexSetFilter(&s_cover_tex, GPU_LINEAR, GPU_LINEAR);
                C3D_TexSetWrap(&s_cover_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
                s_cover_subtex.width  = (u16)w;
                s_cover_subtex.height = (u16)h;
                s_cover_subtex.left   = 0;
                s_cover_subtex.top    = 1;
                s_cover_subtex.right  = (float)w / (float)tw;
                s_cover_subtex.bottom = 1.f - (float)h / (float)th;
                s_cover_img.tex    = &s_cover_tex;
                s_cover_img.subtex = &s_cover_subtex;
                s_cover_inited = 1;
                s_cover_failed = 0;
                snprintf(s_cover_id, sizeof s_cover_id, "%s", want);
                snprintf(s_cover_have, sizeof s_cover_have, "%s", want);
            }
            stbi_image_free(px);
        } else {
            if (px) stbi_image_free(px);
            s_cover_failed = 1;
        }
    } else {
        s_cover_failed = 1;
    }
}

static void nd_start_play(const NdItem *it)
{
    s_stream_started = 0;
    if (strncmp(it->id, "art:", 4) == 0 || strncmp(it->id, "alb:", 4) == 0) { nd_open_item(it); return; }
    /* 1. Coupe l audio et force l ecran du lecteur IMMEDIATEMENT */
    audio_stop();
    s_player_view = true;

    /* 2. Recherche et reconstruction de la file d attente */
    if (it >= s_items && it < s_items + s_item_count) {
        int idx = (int)(it - s_items);
        memcpy(s_np_list, s_items, sizeof(NdItem) * (size_t)s_item_count);
        s_np_list_count = s_item_count;
        s_np_idx = idx;
    } else {
        int found_in_items = -1;
        for (int i = 0; i < s_item_count; i++) {
            if (s_items[i].id[0] && it->id[0] && strcmp(s_items[i].id, it->id) == 0) {
                found_in_items = i;
                break;
            }
        }
        if (found_in_items >= 0) {
            memcpy(s_np_list, s_items, sizeof(NdItem) * (size_t)s_item_count);
            s_np_list_count = s_item_count;
            s_np_idx = found_in_items;
            it = &s_np_list[found_in_items];
        } else {
            int found_idx = -1;
            for (int i = 0; i < s_np_list_count; i++) {
                if (s_np_list[i].id[0] && it->id[0] && strcmp(s_np_list[i].id, it->id) == 0) {
                    found_idx = i;
                    break;
                }
            }
            if (found_idx >= 0) {
                s_np_idx = found_idx;
                it = &s_np_list[found_idx];
            } else {
                s_np_list_count = 0;
                s_np_idx = -1;
            }
        }
    }

    if (s_np_idx >= 0) {
        s_np_list[s_np_idx].starred = nd_fav_is_local(s_np_list[s_np_idx].id);

        /* 3DSS-FIX: On ne charge PLUS la pochette sur le thread principal (causait le freeze de 5-10s).
           On nettoie juste l etat. Le thread de fond ND_JOB_DL telechargera la pochette en arriere-plan. */
        if (s_np_list[s_np_idx].cover[0] && strcmp(s_cover_id, s_np_list[s_np_idx].cover) != 0) {
            s_cover_id[0] = 0;
            s_cover_raw_len = 0;
            s_cover_inited = 0;
        }
    }
    nd_start_download2(it, 1);
}

/* Precedent/suivant dans l instantane : ne touche pas a s_items, donc
   fonctionne meme si l utilisateur a navigue ailleurs depuis. */
static void nd_abort_active_download(void)
{
    s_job_cancel = true;
    /* httpcCancelConnection() est fait pour etre appele depuis un AUTRE
       thread que celui qui possede le contexte : il debloque rapidement un
       httpcDownloadData() en cours en le faisant echouer, SANS fermer le
       handle. Le thread de job (nd_dl_attempt), en relisant s_job_cancel,
       fait alors lui-meme son nettoyage normal via nd_close_http() -
       exactement comme sur n importe quel autre chemin d erreur.
       NE PAS fermer le handle ICI en plus (svcCloseHandle) : s_active_ctx
       pointe vers la PILE du thread de job, toujours vivant a ce moment -
       le fermer depuis ce thread-ci court-circuitait son propre
       httpcCloseContext() (double fermeture du meme handle, cote UI ET
       cote job, sans aucune coordination entre les deux). */
    if (s_active_ctx) httpcCancelConnection(s_active_ctx);
    s_abort_timestamp = osGetTime();
}

static void nd_fetch_endless_radio(void)
{
    char vb[320];
    if (s_job_running) return;
    snprintf(vb, sizeof(vb), "getRandomSongs.view?size=1&f=json");
    nd_api_url(g_settings.navidrome_url, vb, g_settings.navidrome_user,
               g_settings.navidrome_pass, s_job_url, (int)sizeof s_job_url);
    s_job_kind = ND_V_TRACKS;
    snprintf(s_job_id, sizeof s_job_id, "~endless");
    s_job = ND_JOB_FETCH;
    s_play_pending = 1;
    nd_job_start();
}

static void nd_player_step(int dir)
{
    int cur_pos = audio_get_position();
    
    /* 1. CONTRÔLE DE LIMITES STRICTS (Bounds Checking avant toute action audio) */
    bool can_prev = s_np_idx > 0;
    bool can_next = s_np_idx >= 0 && (s_np_idx + 1 < s_np_list_count || s_endless_radio);
    
    if (dir == 1 && !can_next) {
        return;
    }
    
    if (dir == -1 && !can_prev) {
        if (cur_pos < 5) {
            return;
        }
        /* 3DSS-FIX : Précédent > 5s sur la premiere chanson = SEEK a 0:00 (pas de rechargement !) */
        audio_seek(0);
        return;
    }

    /* Mode normal */
    int n;
    if (dir == -1 && cur_pos >= 5) {
        n = s_np_idx;
    } else {
        n = s_np_idx + dir;
    }

    /* 3DSS-FIX : Si on reste sur le meme morceau (Précédent > 5s), on fait un SEEK a 0:00 */
    if (n == s_np_idx) {
        audio_seek(0);
        return;
    }

    /* 2. Transition vers un AUTRE morceau */
    audio_stop();
    s_playing_nd = 0;
    s_play_pending = 1;
    s_stream_started = 0;
    s_transition_active = true;

    /* --- MODE RADIO INFINIE : Suivant va chercher une musique aléatoire --- */
    if (dir == 1 && s_endless_radio && (s_np_idx + 1 >= s_np_list_count)) {
        if (s_job_running) {
            nd_abort_active_download();
            s_pending_radio_fetch = true;
        } else {
            nd_fetch_endless_radio();
        }
        return;
    }

    s_np_idx = n;
    s_np_list[n].starred = nd_fav_is_local(s_np_list[n].id);
    nd_cover_request_now(s_np_list[n].cover);
    nd_cover_wait_brief();
    s_player_view = true;

    if (s_job_running) {
        nd_abort_active_download();
        s_pending_play_item = s_np_list[n];
        s_pending_play_valid = true;
    } else {
        nd_start_download2(&s_np_list[n], 1);
    }
}

/* Fin de piste : enchaine sur la suivante de l instantane, comme le fait deja
   la playlist locale de son cote. A appeler une fois par image. */
static void nd_player_autoadvance_tick(void)
{
    if (s_transition_active || s_play_pending || s_job_running || !audio_is_finished()) return;
    if (!s_playing_nd) return;
    if (s_np_idx + 1 >= s_np_list_count) {
        if (s_endless_radio) { s_transition_active = true; nd_fetch_endless_radio(); }
        else { s_playing_nd = 0; }
        return;
    }
    nd_player_step(+1);
}

/* Lance la piste demandee pendant qu une autre etait encore en train de
   telecharger, des que ce job annule a vraiment fini de s arreter. */
void nd_pending_play_tick(void)
{
    if (!s_nd_session_active || s_step != ND_S_OK) {
        s_pending_play_valid = false;
        s_pending_radio_fetch = false;
        return;
    }
    if (s_job_running) return; /* Attend que l'ancien job soit completement arrete */

    /* L'ancien job est arrete : on reset s_job_cancel */
    s_job_cancel = false;

    if (s_pending_radio_fetch) {
        s_pending_radio_fetch = false;
        nd_fetch_endless_radio();
        return;
    }

    if (!s_pending_play_valid) return;

    /* Dépilage et lancement propre de la nouvelle chanson */
    s_pending_play_valid = false;
    NdItem it = s_pending_play_item;
    svcSleepThread(100000000ULL); /* Pause 100ms pour liberation socket 3DS */
    nd_start_play(&it);
}

/* ---------- entree / sortie ---------- */
void nd_enter(void)
{
    s_nd_session_active = true;
    s_endless_radio = g_settings.nd_endless_radio;
    s_wifi_was_off = wifi_manager_is_disabled();
    if (s_wifi_was_off) wifi_manager_enable();
    if (!s_httpc_open) { (void)httpcInit(0); s_httpc_open = 1; }
    /* ND9 : reprise sans reconnexion si la session est vivante */
    if (s_persist && s_step == ND_S_OK) {
        u32 w = 0;
        ACU_GetWifiStatus(&w);
        s_wifi_live = w;
        if (w >= 1) {   /* ND25 : ACU peu fiable (ND13) ; seul w==0 = radio eteinte */
            s_t_start = osGetTime() - ND_T_FADE;   /* pas d'intro : direct nav */
            s_wifi_lost = 0;
            s_msg_until = 0;
            if (s_item_count == 0) nd_open_browse(s_lcat);
            return;
        }
        s_step = ND_S_WIFI;      /* wifi tombe : on repasse par la connexion */
    }
    s_wifi_live = 3;
    s_wifi_lost = 0;
    s_fetch_ok = 1;
    s_wifi_poll = 0;

    s_err[0] = 0;
    s_srvinfo[0] = 0;
    s_fail_at = -1;
    s_step = ND_S_WIFI;
    s_t_start = osGetTime();
    s_depth = 0;
    s_stack[0].kind = ND_V_BROWSE;
    s_stack[0].id[0] = 0;
    s_stack[0].title[0] = 0;
    s_stack[0].sel = 0;
    s_stack[0].scroll = 0;
    s_stack[0].pane = 0;
    s_lcat = 0;
    s_lsel = 0;
    s_confirm_disc = 0;
    s_pending_lcat = -1;
    s_fetched_lcat = -1;
    memset(s_rc_sel, 0, sizeof s_rc_sel);
    memset(s_rc_scr, 0, sizeof s_rc_scr);
    s_item_count = 0;
    s_msg_until = 0;

    s_connect_abort = 0;               /* ND10d */
    if (!s_thread_running) {
        Thread t;
        s_thread_running = true;       /* ND10c : AVANT le spawn (le thread
                                          le repasse a false en finissant) */
        t = threadCreate(nd_connect_thread, NULL, 64 * 1024, 0x30,
                         -1, true);
        if (!t) {
            s_thread_running = false;
            snprintf(s_err, sizeof s_err, "Thread reseau impossible");
            s_fail_at = ND_S_HTTP;
            s_step = ND_S_ERR;
        }
    }
}

/* ND10c : A = reessayer apres un echec de connexion (partage telephone,
   box lente...) : relance le thread de connexion sans sortir du mode. */
static void nd_retry_connect(void)
{
    Thread t;
    if (s_thread_running || s_step != ND_S_ERR) return;
    s_err[0] = 0;
    s_srvinfo[0] = 0;
    s_fail_at = -1;
    s_wifi_lost = 0;
    s_step = ND_S_WIFI;
    s_thread_running = true;           /* ND10c : avant le spawn */
    t = threadCreate(nd_connect_thread, NULL, 64 * 1024, 0x30, -1, true);
    if (!t) {
        s_thread_running = false;
        snprintf(s_err, sizeof s_err, "Thread reseau impossible");
        s_fail_at = ND_S_HTTP;
        s_step = ND_S_ERR;
    }
}

/* ND9 : sortir du mode NE coupe plus la connexion : on la retrouve
   en revenant (reprise instantanee). Seule "Deconnexion" rend la
   main (session httpc + eco WiFi rendus). */
static void nd_leave(AppState *state)
{
    s_nd_session_active = false;
    s_connect_abort = 1;      /* ND10d : stoppe l'attente WiFi eventuelle */
    s_msg_until = 0;
    *state = STATE_SETTINGS;
}

static void nd_disconnect(AppState *state)
{
    extern Settings g_settings;
    if (!g_settings.wifi_off_in_app) wifi_manager_enable();
    s_confirm_disc = 0;
    s_nd_session_active = false;
    s_playing_nd = 0;
    if (s_job_running || s_active_ctx) {
        /* Ne JAMAIS fermer le handle ni forcer s_job_running=0 depuis ce
           thread : s_active_ctx pointe vers la pile d un thread de job
           potentiellement encore vivant, bloque dans un httpcDownloadData()
           sur ce meme handle. Le fermer ici et croire la place libre
           pendant que ce thread tourne encore permettrait a un NOUVEAU job
           de demarrer en parallele de l ancien, les deux touchant les
           memes tampons partages (s_dl_buf, s_items, ...) sans aucune
           coordination - exactement le genre de course qui ressemble a un
           freeze aleatoire. On demande l annulation cooperative, on debloque
           l appel reseau en cours, puis on ATTEND que le thread de job se
           soit lui-meme arrete (son propre nd_close_http()) avant de
           continuer. Attente bornee : mieux vaut un court gel assume ici
           qu une corruption d etat partagee. */
        s_job_cancel = true;
        if (s_active_ctx) httpcCancelConnection(s_active_ctx);
        {
            int waited = 0;
            while (s_job_running && waited < 3000) {
                svcSleepThread(20000000LL);
                waited += 20;
            }
        }
        s_job_running = 0;   /* filet : au pire le thread etait deja mort sans l avoir signale */
        s_job = ND_JOB_NONE;
        s_active_ctx = NULL;
    }

    /* Purge totale de toutes les files d attente musicales */
    s_pending_play_valid = false;
    s_pending_radio_fetch = false;
    s_play_pending = 0;
    s_transition_active = false;

    /* Coupe proprement la musique de Navidrome lors de la deconnexion */
    audio_stop();
    s_playing_nd = 0;
    s_player_view = false;
    s_playing_from_sd = false;

    /* Remet la derniere musique locale lue dans le lecteur 3DSS via le wrapper securise de main.c */
    extern void main_restore_local_music(void);
    main_restore_local_music();

    s_persist = 0;
    s_step = ND_S_IDLE;
    s_depth = 0;
    s_item_count = 0;
    s_lsel = 0;
    s_lcat = 0;
    s_fetched_lcat = -1;
    s_pending_lcat = -1;
    memset(s_cache, 0, sizeof s_cache);
    s_cache_init = 0;
    if (s_httpc_open) { httpcExit(); s_httpc_open = 0; }
    /* Le temp n est plus vide ici : les titres deja ecoutes y restent
       disponibles (rejoues sans retelecharger, et recuperables avec leur vrai
       nom). Il est vide a la fermeture de l app, via nd_temp_clear(). */
    remove(ND_TMP_DIR "/nd_play.mp3");   /* ancien fichier unique des versions precedentes */
    if (s_wifi_was_off) {
        wifi_manager_disable();
        s_wifi_was_off = false;
    }
    s_msg_until = 0;
    *state = STATE_SETTINGS;
}

/* ND8 : watchdog WiFi - sonde le statut toutes les 500 ms.
   Coupure  -> message + indicateur [X] + fetchs marques ko.
   Retour   -> message + relance du fetch de la vue courante. */
static void nd_wifi_poll(void)
{
    u64 now = osGetTime();
    u32 w = s_wifi_live;
    if (now - s_wifi_poll < 300ULL) return;   /* ND9 : 300 ms */
    s_wifi_poll = now;
    ACU_GetWifiStatus(&w);
    /* ND13 : le statut 1 n'est PAS une coupure (il ment sur les
       hotspots) ; seule la radio eteinte (0) marque la perte. Les
       vraies coupures Internet sont detectees par nd_net_fail
       (echec des requetes), qui est plus fiable que l'ACU. */
    if (w < 1 && s_wifi_live >= 1) {
        s_wifi_lost = 1;
        s_fetch_ok = 0;
        if (s_step == ND_S_OK)
            nd_msg("WiFi perdu : connexion coupee", 4000, true);
    } else if (w >= 1 && s_wifi_live < 1) {
        s_wifi_lost = 0;
        if (s_step == ND_S_OK) {
            nd_msg("WiFi revenu", 2500, false);
            if ((!s_fetch_ok || s_item_count == 0) && !s_job_running) {
                /* ND9 : vrai refetch reseau (pas le cache) apres coupure */
                if (s_depth == 0) nd_browse_start(s_lcat);
                else nd_view_fetch(s_stack[s_depth].kind,
                                   s_stack[s_depth].id);
            }
        }
    }
    s_wifi_live = w;
}

/* ---------- input ---------- */
/* UNIFIED SCROLL PATTERN : Navidrome (Haut/Bas + Gauche/Droite +-5) */
static void nd_list_move(NdView *v, u32 kd)
{
    if (v->sel >= s_item_count)
        v->sel = (s_item_count > 0) ? s_item_count - 1 : 0;
    if (kd & KEY_UP) {
        if (v->sel > 0) v->sel--;
        else if (s_item_count > 0) v->sel = s_item_count - 1;
    }
    if (kd & KEY_DOWN) {
        if (v->sel < s_item_count - 1) v->sel++;
        else v->sel = 0;
    }
    if (kd & KEY_RIGHT) {
        v->sel += 5;
        if (v->sel >= s_item_count) v->sel = (s_item_count > 0) ? s_item_count - 1 : 0;
    }
    if (kd & KEY_LEFT) {
        v->sel -= 5;
        if (v->sel < 0) v->sel = 0;
    }
    if (v->sel < v->scroll) v->scroll = v->sel;
    if (v->sel >= v->scroll + ND_ROWS) v->scroll = v->sel - ND_ROWS + 1;
    if (v->scroll < 0) v->scroll = 0;
}

static void nd_lsel_move(int d)
{
    NdView *v = &s_stack[0];
    if (s_lsel < ND_LCATS) {            /* position gardee par categorie */
        s_rc_sel[s_lcat] = v->sel;
        s_rc_scr[s_lcat] = v->scroll;
    }
    s_confirm_disc = 0;                  /* ND10b : naviguer annule */
    s_lsel = (s_lsel + d + ND_LCATS + 1) % (ND_LCATS + 1);
    if (s_lsel >= ND_LCATS) return;     /* entree Deconnexion : rien a charger */
    s_lcat = s_lsel;
    v->sel = s_rc_sel[s_lcat];
    v->scroll = s_rc_scr[s_lcat];
    s_item_count = 0;
    nd_open_browse(s_lcat);             /* ND9 : cache sinon fond, non bloquant */
}

static void nd_open_item(const NdItem *it)
{
    /* lcat 0/1/2 = listes d ALBUMS -> on y entre. lcat 3 (Favoris) et 5
       (Morceaux) sont des listes de MORCEAUX individuels, pas des albums :
       A telecharge, X ecoute (comme pour Morceaux). lcat 4 = artistes. */
    if (s_lcat == 0 || s_lcat == 1 || s_lcat == 2)
        nd_push(ND_V_TRACKS, it->id, it->title);
    else if (s_lcat == 4) nd_push(ND_V_ARTALB, it->id, it->title);
    else                 nd_start_download(it);
}

/* ND11 : Y sur l'ecran d'erreur -> sonde https PUIS http et affiche
   les resultats. Permet de voir si le reseau (partage telephone)
   bloque un des deux schemas. */
static void nd_run_diag(void)
{
    char url[600];
    u32 st = 0;
    int save_pref = s_prefer_http;
    s_diag_done = 0;
    s_diag1[0] = 0;
    s_diag2[0] = 0;
    nd_build_ping_url(g_settings.navidrome_url, g_settings.navidrome_user,
                      g_settings.navidrome_pass, url, (int)sizeof url);
    s_prefer_http = 0;
    s_err[0] = 0;
    if (nd_fetch(url, s_diag_resp, (int)sizeof s_diag_resp, &st))
        snprintf(s_diag1, sizeof s_diag1, "https: OK (code HTTP %lu)",
                 (unsigned long)st);
    else
        snprintf(s_diag1, sizeof s_diag1, "https: %.48s", s_err);
    s_prefer_http = 1;
    s_err[0] = 0;
    if (nd_fetch(url, s_diag_resp, (int)sizeof s_diag_resp, &st))
        snprintf(s_diag2, sizeof s_diag2, "http : OK (code HTTP %lu)",
                 (unsigned long)st);
    else
        snprintf(s_diag2, sizeof s_diag2, "http : %.48s", s_err);
    s_prefer_http = save_pref;
    s_diag_done = 1;
}

static void nd_player_input(u32 kd);   /* defini plus bas, pres du dessin du lecteur */

void navidrome_input(AppState *state)
{
    u32 kd = hidKeysDown();
    if (power_save_is_b_locked(hidKeysHeld())) { kd = 0; }
    NdView *v = &s_stack[s_depth];
    nd_wifi_poll();

    /* Bloquer toutes les touches en mode telechargement vers SD, sauf B pour annuler et START */
    if (s_job_running && s_job == ND_JOB_DL && !s_play_after) {
        if (kd & KEY_B) {
            s_job_cancel = true;
            nd_msg("Telechargement annule", 2000, false);
        }
        if (kd & KEY_START) {
            nd_leave(state);
        }
        return;
    }

    if (kd & KEY_START) { nd_leave(state); return; }
    /* SELECT : retour a l ecran lecteur pendant une lecture (remplace l ancien
       affichage de diagnostic WiFi brut, peu utile hors debogage). */
    if (kd & KEY_SELECT) { if (s_playing_nd) s_player_view = true; return; }
    if (s_confirm_disc) {                /* ND10b : confirmation en cours */
        if (kd & KEY_A) { s_confirm_disc = 0; nd_disconnect(state); }
        else if (kd & KEY_B) s_confirm_disc = 0;
        return;
    }
    if (s_step != ND_S_OK) {                 /* connexion / erreur : B sort */
        if (kd & KEY_B) { nd_leave(state); return; }
        if (s_step == ND_S_ERR) {
            if (kd & KEY_A) nd_retry_connect();   /* ND10c : reessayer */
            else if (kd & KEY_Y) nd_run_diag();   /* ND11 : test reseau */
        }
        return;
    }
    /* ND9 : plus de blocage global : le volet gauche reste utilisable
       pendant un chargement ; A est protege localement par !s_job_running. */
    if (s_msg_until) {                       /* message : A/B le ferme */
        if (osGetTime() >= s_msg_until) s_msg_until = 0;
        else if (kd & (KEY_A | KEY_B)) s_msg_until = 0;
        return;
    }
    if (osGetTime() - s_t_start < ND_T_FADE) {   /* intro : B sort seulement */
        if (kd & KEY_B) nd_leave(state);
        return;
    }

    /* Ecran lecteur dedie : ses propres commandes, on ne retombe jamais dans
       la navigation de liste en dessous tant qu il est affiche. */
    if (s_player_view) { nd_player_input(kd); return; }

    /* Retour au lecteur depuis n importe quelle vue de navigation : SELECT
       (voir plus haut) ou toucher le bandeau "Lecture : ...". */
    if (s_playing_nd && (kd & KEY_TOUCH)) {
        touchPosition tp; hidTouchRead(&tp);
        if (tp.px >= 10 && tp.px < BOT_WIDTH - 10 && tp.py >= 144 && tp.py < 164) {
            s_player_view = true;
            return;
        }
    }

    /* Y = rechercher, depuis n importe quelle vue */
    if ((kd & KEY_Y) && !s_job_running) { nd_open_search(); return; }

    if (v->kind == ND_V_BROWSE) {
        if (kd & KEY_LEFT) {
            /* Sur le volet gauche, fleche gauche = actualiser / rebrasser l'onglet courant */
            if (v->pane == 0 && s_lcat < ND_LCATS && !s_job_running) {
                if (s_lcat == 3) {
                    nd_open_browse(s_lcat); /* Recharge les favoris locaux depuis la SD */
                } else {
                    nd_browse_start(s_lcat); /* Force une requete reseau complete sans cache */
                }
            } else {
                v->pane = 0;
            }
            return;
        }
        if (kd & KEY_RIGHT) { if (s_lsel < ND_LCATS) v->pane = 1; return; }
        if (v->pane == 0) {
            if (kd & KEY_UP)        { nd_lsel_move(-1); return; }
            if (kd & KEY_DOWN)      { nd_lsel_move(+1); return; }
            if (kd & KEY_A) {
                if (s_lsel >= ND_LCATS) s_confirm_disc = 1;  /* ND10b */
                else {
                    /* Sur une categorie aleatoire, A relance un tirage neuf
                       avant de passer a droite : c est le geste "rebrasser",
                       et lui SEUL (nd_browse_start ignore le cache ; passer
                       par nd_open_browse ici l aurait sinon reutilise). */
                    if (nd_cat_is_random(s_lcat) && !s_job_running)
                        nd_browse_start(s_lcat);
                    v->pane = 1;
                }
                return;
            }
            if (kd & KEY_B)         { nd_leave(state); return; }
            return;
        }
        if (kd & KEY_B) { v->pane = 0; return; }
        /* Pagination L/R intelligente selon la categorie */
        if ((kd & KEY_L) != 0) {
            if (s_lcat == 0 || s_lcat == 1) {
                if (s_page_offset >= s_page_size) {
                    s_page_offset -= s_page_size;
                    v->sel = 0; v->scroll = 0;
                    s_rc_sel[s_lcat] = 0; s_rc_scr[s_lcat] = 0;
                    nd_browse_start(s_lcat);
                    return;
                }
            } else if (s_lcat == 4) {
                if (s_page_offset >= s_page_size) {
                    s_page_offset -= s_page_size;
                    nd_artists_paginate_local();
                    return;
                }
            } else if (s_lcat == 2 || s_lcat == 5) {
                nd_browse_start(s_lcat);
                return;
            } else {
                v->sel = (v->sel >= ND_ROWS) ? (v->sel - ND_ROWS) : 0;
                if (v->sel < v->scroll) v->scroll = v->sel;
                return;
            }
        }
        if ((kd & KEY_R) != 0) {
            if (s_lcat == 0 || s_lcat == 1) {
                if (s_item_count >= s_page_size) {
                    s_page_offset += s_page_size;
                    v->sel = 0; v->scroll = 0;
                    s_rc_sel[s_lcat] = 0; s_rc_scr[s_lcat] = 0;
                    nd_browse_start(s_lcat);
                    return;
                }
            } else if (s_lcat == 4) {
                if (s_item_count >= s_page_size) {
                    s_page_offset += s_page_size;
                    nd_artists_paginate_local();
                    return;
                }
            } else if (s_lcat == 2 || s_lcat == 5) {
                nd_browse_start(s_lcat);
                return;
            } else {
                if (s_item_count > 0) {
                    v->sel = (v->sel + ND_ROWS < s_item_count) ? (v->sel + ND_ROWS) : (s_item_count - 1);
                    if (v->sel >= v->scroll + ND_ROWS) v->scroll = v->sel - ND_ROWS + 1;
                }
                return;
            }
        }
        nd_list_move(v, kd);

        if (s_lsel < ND_LCATS) {
            s_rc_sel[s_lcat] = v->sel;
            s_rc_scr[s_lcat] = v->scroll;
        }
        if (kd & KEY_A) {
            if (s_item_count > 0 && v->sel < s_item_count) {
                if (s_lcat == 5 || s_lcat == 3) {
                    nd_start_play(&s_items[v->sel]);    /* A = ECOUTER (Streaming direct) */
                } else {
                    nd_open_item(&s_items[v->sel]);     /* A = OUVRIR dossier / album */
                }
            }
            return;
        }
        if (kd & KEY_X) {
            if (s_job_running) {
                nd_msg("Patientez... (Chargement en cours)", 2000, false);
                return;
            }
            if ((s_lcat == 5 || s_lcat == 3) && s_item_count > 0 && v->sel < s_item_count) {
                nd_open_item(&s_items[v->sel]);         /* X = TELECHARGER sur la carte SD */
            }
            return;
        }
        return;
    }

    /* pistes / albums d'artiste : liste simple */
    if (kd & KEY_B) { nd_pop(); return; }
    nd_list_move(v, kd);
    if (kd & KEY_A) {
        if (s_item_count > 0 && v->sel < s_item_count) {
            if (v->kind == ND_V_TRACKS) {
                nd_start_play(&s_items[v->sel]); /* Permet de changer de morceau en direct pendant le streaming */
            }
            if (v->kind == ND_V_ARTALB) {
                nd_push(ND_V_TRACKS, s_items[v->sel].id, s_items[v->sel].title);
            }
        }
        return;
    }
    if (kd & KEY_X) {
        if (s_job_running) {
            nd_msg("Patientez... (Chargement en cours)", 2000, false);
            return;
        }
        if (v->kind == ND_V_TRACKS && s_item_count > 0 && v->sel < s_item_count) {
            nd_start_download(&s_items[v->sel]);
        }
        return;
    }
}

/* ---------- dessin ---------- */
static void nd_draw_right(NdView *v)
{
    Theme *th = nd_th();
    int r, shown, idx;
    char t[40], sb[48];
    if (s_item_count == 0 && (s_job_running || s_sync_fetch_active || s_pending_lcat == s_lcat)) {
        /* La liste attend derriere un job deja en cours (typiquement le
           telechargement de la musique en train d etre ecoutee) : sans ca,
           l ecran restait vide jusqu a ce que le job se libere. */
        nd_text(130, 40, 0.44f, th->text_secondary, "Un instant...");
        return;
    }
    if (v->sel >= s_item_count)
        v->sel = (s_item_count > 0) ? s_item_count - 1 : 0;
    if (v->scroll > v->sel) v->scroll = v->sel;
    shown = s_item_count - v->scroll;
    if (shown > ND_ROWS) shown = ND_ROWS;
    for (r = 0; r < shown; r++) {
        float y = 32.0f + r * 26.0f;
        idx = v->scroll + r;
        bool is_sel = (idx == v->sel) && (v->kind != ND_V_BROWSE || v->pane == 1);
        if (is_sel) {
            nd_rect(122, y - 2, 276, 24, th->bg_selected);
            nd_rect(122, y - 2, 3, 24, th->accent);
        }
        nd_trunc(s_items[idx].title, t, 32);
        nd_text(130, y, 0.50f,
                is_sel ? th->text_primary : th->text_secondary, t);
        if (s_items[idx].sub[0]) {
            nd_trunc(s_items[idx].sub, sb, 44);
            nd_text(130, y + 13, 0.35f, th->text_disabled, sb);
        }
    }
}

static void nd_draw_browse(NdView *v)
{
    Theme *th = nd_th();
    int i;
    char foot[40];
    nd_rect(0, 26, 120, TOP_HEIGHT - 26, th->bg_secondary);
    nd_rect(120, 26, 1, TOP_HEIGHT - 26, th->border);
    for (i = 0; i <= ND_LCATS; i++) {
        float y = 34.0f + i * 20.0f;
        u32 col = th->text_secondary;
        if (i == ND_LCATS) nd_rect(8, y - 6, 104, 1, th->border);
        if (i == s_lsel) {
            if (v->pane == 0) {
                nd_rect(0, y - 3, 120, 19, th->bg_selected);
                col = (i == ND_LCATS) ? ND_ERRC : th->text_primary;
            } else {
                col = th->text_accent;
            }
        }
        nd_text(8, y, 0.45f, col, s_lcat_label[i]);
    }
    snprintf(foot, sizeof foot, "V%s", APP_VERSION);
    nd_text(8, 198, 0.38f, th->text_disabled, foot);
        nd_trunc(s_srvinfo, foot, 24);
    nd_text(8, 214, 0.32f, th->text_disabled, foot);
    if (s_confirm_disc) {
        return;
    }
    if (s_lsel >= ND_LCATS) {          /* ND9 : entree Deconnexion selectionnee */
        nd_text_ct(120.0f, 0.42f, th->text_secondary,
                   "A = quitter le serveur", true, 255);
        nd_text_ct(136.0f, 0.36f, th->text_disabled,
                   "(la connexion reste sinon)", true, 255);
        return;
    }
    nd_draw_right(v);
}

static void nd_draw_deep(NdView *v)
{
    Theme *th = nd_th();
    char cur[24], word[24], line[24];
    int lines = 0, total = 0, i, wl;
    const char *q = v->title;
    nd_rect(0, 26, 120, TOP_HEIGHT - 26, th->bg_secondary);
    nd_rect(120, 26, 1, TOP_HEIGHT - 26, th->border);
    cur[0] = 0;
    while (*q && lines < 5) {                   /* titre wrappe mot a mot */
        while (*q == ' ') q++;
        wl = 0;
        while (*q && *q != ' ' && wl < 20) word[wl++] = *q++;
        word[wl] = 0;
        if (!wl) break;
        if ((int)strlen(cur) + wl + 1 > 18) {
            nd_text(8, 36 + lines * 14, 0.42f, th->text_primary, cur);
            lines++;
            cur[0] = 0;
        }
        if (cur[0]) {
            strncat(cur, " ", sizeof cur - strlen(cur) - 1);
            strncat(cur, word, sizeof cur - strlen(cur) - 1);
        } else {
            snprintf(cur, sizeof cur, "%s", word);
        }
    }
    if (cur[0] && lines < 5) {
        nd_text(8, 36 + lines * 14, 0.42f, th->text_primary, cur);
        lines++;
    }
    nd_rect(8, 40 + lines * 14, 104, 1, th->border);
    snprintf(line, sizeof line, "%d %s", s_item_count,
             v->kind == ND_V_TRACKS ? "morceaux" : "albums");
    nd_text(8, 46 + lines * 14, 0.38f, th->text_secondary, line);
    if (v->kind == ND_V_TRACKS) {
        for (i = 0; i < s_item_count; i++)
            if (s_items[i].num > 0) total += s_items[i].num;
        if (total > 0) {
            snprintf(line, sizeof line, "%d min", (total + 30) / 60);
            nd_text(8, 60 + lines * 14, 0.38f, th->text_secondary, line);
        }
    }
    nd_draw_right(v);
}

static void nd_draw_nav(NdView *v)
{
    Theme *th = nd_th();
    char cnt[24], ht[40];
    nd_rect(0, 0, TOP_WIDTH, 26, th->bg_header);
    nd_trunc(v->kind == ND_V_BROWSE ? "Navidrome" ND_VERSION : v->title, ht, 34);
    nd_text(8, 5, 0.55f, th->text_accent, ht);
    if (s_item_count > 0) {
        snprintf(cnt, sizeof cnt, "%d/%d", v->sel + 1, s_item_count);
        nd_text(TOP_WIDTH - 60, 7, 0.42f, th->text_secondary, cnt);
    }
    if (s_wifi_lost) {                       /* ND8 : WiFi [X] visible */
        nd_rect(0, 26, TOP_WIDTH, 2, ND_ERRC);
        nd_text(TOP_WIDTH - 118, 7, 0.42f, ND_ERRC, "WiFi [X]");
    } else {
        nd_text(TOP_WIDTH - 118, 7, 0.42f, th->text_disabled, "WiFi [ok]");
    }
    if (v->kind == ND_V_BROWSE) nd_draw_browse(v);
    else nd_draw_deep(v);
}

/* Bandeau de chargement discret facon lecteur en ligne : une fine barre qui
   glisse sous l en-tete, sans voler l ecran. */
static void nd_draw_thin_loader(void)
{
    Theme *th = nd_th();
    const float span = (float)TOP_WIDTH + 90.f;
    float x = (float)((osGetTime() / 4ULL) % (u64)span) - 90.f;
    float w = 90.f;
    if (x < 0.f) { w += x; x = 0.f; }
    if (x + w > (float)TOP_WIDTH) w = (float)TOP_WIDTH - x;
    nd_rect(0, 24, TOP_WIDTH, 2, th->progress_bg);
    if (w > 0.f) nd_rect(x, 24, w, 2, th->text_accent);
}

static void nd_draw_job_overlay(void)
{
    Theme *th = nd_th();

    /* Lecture depuis le serveur : le transfert est plus rapide que la lecture,
       il n y a donc rien a annoncer une fois le son parti — et pendant la mise
       en tampon, un bandeau suffit. Pas de page de stockage ni de compteur de
       kilo-octets : ca ne concerne pas l utilisateur ici. */
    if (s_job == ND_JOB_DL && s_play_after) {
        if (!s_stream_started) nd_draw_thin_loader();
        return;
    }

    /* ND9 : overlay sur le volet DROIT seulement : le gauche reste libre */
    nd_rect(75, 84, 250, 72, th->bg_secondary);   /* ND28b : boite centree */
    nd_rect(75, 84, 250, 1, th->border);
    nd_rect(75, 155, 250, 1, th->border);
    if (s_job == ND_JOB_DL) {
        char line[96];
        /* ND13 : pas de taille annoncee = transcodage serveur (m4a) */
        nd_text_ct(94.0f, 0.48f, th->text_primary,   /* ND28c : monte, ne chevauche plus la barre */
                   (s_dl_total > 0) ? "Telechargement..."
                                    : "Transcodage serveur...", true, 255);
        nd_rect(95, 116, 210, 10, th->progress_bg);
        if (s_dl_total > 0) {
            float frac = (float)s_dl_have / (float)s_dl_total;
            if (frac > 1.0f) frac = 1.0f;
            nd_rect(95, 116, 210.0f * frac, 10, th->progress_fill);
            snprintf(line, sizeof line, "%lu%%  (%lu Ko / %lu Ko)",
                     (unsigned long)(frac * 100.0f),
                     (unsigned long)(s_dl_have / 1024),
                     (unsigned long)(s_dl_total / 1024));
        } else {
            snprintf(line, sizeof line, "%lu Ko recus - %llus",
                     (unsigned long)(s_dl_have / 1024),
                     (unsigned long long)((osGetTime() - s_job_t0) / 1000ULL));
        }
        nd_text_ct(130.0f, 0.42f, th->text_secondary, line, true, 255);
    } else {
        static const char spin[4] = { '|', '/', '-', '\\' };
        char line[32];
        snprintf(line, sizeof line, "Chargement %c",
                 spin[(osGetTime() / 150ULL) & 3]);
        nd_text_ct(110.0f, 0.50f, th->text_primary, line, true, 255);
    }
}

static void nd_draw_msg_overlay(void)
{
    Theme *th = nd_th();
    nd_rect(40, 100, 320, 46, th->bg_secondary);
    nd_rect(40, 100, 320, 1, s_msg_err ? ND_ERRC : th->border);
    nd_rect(40, 145, 320, 1, s_msg_err ? ND_ERRC : th->border);
    nd_text_ct(114.0f, 0.46f, s_msg_err ? ND_ERRC : th->text_primary,
               s_msg, true, 255);
    if (!s_msg_err && s_job == ND_JOB_NONE && s_msg[0] == 'T')
        nd_text_ct(130.0f, 0.36f, th->text_disabled,
                   "dossier Music/Navidrome sur la SD", true, 255);
}

/* ND6 : getters navigation (pour banc hote) */
int         nd_get_nav_depth(void) { return s_depth; }
int         nd_get_nav_sel(void)   { return s_stack[s_depth].sel; }
int         nd_get_count(void)     { return s_item_count; }
const char *nd_get_msg(void)       { return s_msg; }
int         nd_get_nav_kind(void)  { return s_stack[s_depth].kind; }
int         nd_get_pane(void)      { return s_stack[s_depth].pane; }
int         nd_get_wifi_live(void) { return (int)s_wifi_live; }
int         nd_get_wifi_lost(void) { return s_wifi_lost; }
int         nd_get_lsel(void)      { return s_lsel; }
const char *nd_get_item_title(int i)
{ return (i >= 0 && i < s_item_count) ? s_items[i].title : ""; }
int         nd_get_persist(void)   { return s_persist; }
int         nd_get_playing(void)   { return s_playing_nd || s_play_pending; }
/* Appele par main.c quand une piste LOCALE demarre : la lecture Navidrome
   n est plus celle en cours, l auto-avance de la playlist locale peut
   reprendre la main. */
void        nd_clear_playing(void) { s_playing_nd = 0; }
int         nd_get_confirm(void)   { return s_confirm_disc; }
int         nd_get_diag_done(void) { return s_diag_done; }
int         nd_get_fail_at(void)   { return s_fail_at; }   /* ND12 */
const char *nd_get_diag1(void)     { return s_diag1; }
const char *nd_get_diag2(void)     { return s_diag2; }
int         nd_cover_state(void)
{ return s_cover_failed ? 3 : (s_cover_inited ? 2 : (s_cover_raw_len > 0 ? 1 : 0)); }
const char *nd_cover_id(void)      { return s_cover_have; }
void        nd_set_cover_enabled(int e) { s_cover_enabled = e ? 1 : 0; }
/* hook banc hote : force une reconnexion complete au prochain enter */
void nd_force_reconnect(void)
{
    s_persist = 0;
    s_step = ND_S_IDLE;
    s_cache_init = 0;      /* vide le cache ND9 pour les tests */
    s_prefer_http = 0;
    s_pending_lcat = -1;
    s_lsel = 0;
    s_lcat = 0;
    s_item_count = 0;
    /* ND10 */
    s_cover_id[0] = 0; s_cover_have[0] = 0;
    s_cover_raw_len = 0; s_cover_inited = 0; s_cover_failed = 0;
    s_prev_id[0] = 0; s_prev_have[0] = 0;
    s_prev_raw_len = 0; s_prev_inited = 0; s_prev_failed = 0;
    s_cover_deb_sel = -1;
    s_play_after = 0; s_playing_nd = 0; s_play_pending = 0;
    s_player_view = false; s_np_idx = -1; s_np_list_count = 0;
    s_play_title[0] = 0; s_job_title[0] = 0;
    s_confirm_disc = 0;
    s_diag_done = 0; s_diag1[0] = 0; s_diag2[0] = 0;
    s_job_t0 = 0; s_audio_rc = 0;
}
int         nd_get_lcat(void)      { return s_lcat; }

/* ---------- ND10 : pochette (demande + decode + texture) ---------- */
static void nd_cover_tick(void)
{
    u64 now = osGetTime();
    NdView *v = &s_stack[s_depth];
    const char *want = NULL;

    if (!s_cover_enabled || s_step != ND_S_OK) return;

    if (s_player_view) {
        if (s_np_idx >= 0 && s_np_idx < s_np_list_count)
            want = s_np_list[s_np_idx].cover;
        if (!want || !*want) {
            s_cover_id[0] = 0; s_cover_raw_len = 0;
            s_cover_inited = 0; s_cover_failed = 0;
            return;
        }
        if (strcmp(s_cover_id, want) != 0) {
            snprintf(s_cover_id, sizeof s_cover_id, "%s", want);
            s_cover_raw_len = 0; s_cover_inited = 0; s_cover_failed = 0;
        }
        if (!s_cover_inited && !s_cover_failed && s_cover_raw_len == 0) {
            char cp[256];
            snprintf(cp, sizeof cp, "sdmc:/3DSoundShell/Temp/pochettes/%s.png", s_cover_id);
            FILE *cf = fopen(cp, "rb");
            if (cf) {
                fseek(cf, 0, SEEK_END); long cs = ftell(cf); fseek(cf, 0, SEEK_SET);
                if (cs > 0 && cs <= (long)ND_COVER_CAP && fread(s_cover_raw, 1, cs, cf) == (size_t)cs)
                    s_cover_raw_len = (int)cs;
                fclose(cf);
            }
        }
        if (!s_cover_inited && !s_cover_failed && s_cover_raw_len > 0) {
            int w=0,h=0,ch=0;
            unsigned char *px = stbi_load_from_memory(s_cover_raw, s_cover_raw_len, &w, &h, &ch, 4);
            s_cover_raw_len = 0;
            if (px && w > 0 && h > 0 && w <= 256 && h <= 256) {
                if (!s_cover_tex_live && C3D_TexInit(&s_cover_tex, 256, 256, GPU_RGBA8))
                    s_cover_tex_live = 1;
                if (s_cover_tex_live) {
                    u8 *dst = (u8*)s_cover_tex.data;
                    memset(dst, 0, 256*256*4);
                    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
                        u32 tile=((u32)y/8)*32+((u32)x/8);
                        u32 mx=(u32)x&7,my=(u32)y&7;
                        u32 mi=(mx&1)|((my&1)<<1)|((mx&2)<<1)|((my&2)<<2)|((mx&4)<<2)|((my&4)<<3);
                        u32 di=(tile*64+mi)*4;
                        const u8 *sp=px+((size_t)y*w+x)*4;
                        dst[di]=sp[3];dst[di+1]=sp[2];dst[di+2]=sp[1];dst[di+3]=sp[0];
                    }
                    GSPGPU_FlushDataCache(s_cover_tex.data,256*256*4);
                    C3D_TexSetFilter(&s_cover_tex,GPU_LINEAR,GPU_LINEAR);
                    C3D_TexSetWrap(&s_cover_tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
                    s_cover_subtex.width=(u16)w; s_cover_subtex.height=(u16)h;
                    s_cover_subtex.left=0; s_cover_subtex.top=1;
                    s_cover_subtex.right=(float)w/256.f; s_cover_subtex.bottom=1.f-(float)h/256.f;
                    s_cover_img.tex=&s_cover_tex; s_cover_img.subtex=&s_cover_subtex;
                    s_cover_inited=1;
                    snprintf(s_cover_have,sizeof s_cover_have,"%s",s_cover_id);
                }
                stbi_image_free(px);
            } else { if(px)stbi_image_free(px); s_cover_failed=1; }
            return;
        }
        if (!s_cover_inited && !s_cover_failed && s_cover_raw_len==0 && !s_cover_job_running) {
            Thread t; char vb[80];
            snprintf(vb,sizeof vb,"getCoverArt.view?id=%s&size=160",s_cover_id);
            nd_api_url(g_settings.navidrome_url,vb,g_settings.navidrome_user,
                       g_settings.navidrome_pass,s_cover_job_url,sizeof s_cover_job_url);
            snprintf(s_cover_job_id,sizeof s_cover_job_id,"%s",s_cover_id);
            s_cover_job_running=1;
            t=threadCreate(nd_cover_job_thread,NULL,32*1024,0x30,-1,true);
            if(!t) s_cover_job_running=0;
        }
    } else {
        bool need_debounce = false;
        if (v->pane == 0 && v->kind == ND_V_BROWSE) {
            want = NULL;
        } else {
            if (v->kind == ND_V_BROWSE) {
                if (s_item_count > 0 && v->sel < s_item_count && s_lcat != 4) {
                    want = s_items[v->sel].cover; need_debounce = true;
                }
            } else if (!strcmp(v->id, ND_SEARCH_ID)) {
                if (s_item_count > 0 && v->sel < s_item_count) {
                    want = s_items[v->sel].cover; need_debounce = true;
                }
            } else if (v->kind == ND_V_ARTALB) {
                if (s_item_count > 0 && v->sel < s_item_count) {
                    want = s_items[v->sel].cover; need_debounce = true;
                }
            } else if (v->kind == ND_V_TRACKS) {
                want = v->id; need_debounce = false;
            }
        }
        if (!want || !*want) {
            s_prev_id[0]=0; s_prev_raw_len=0; s_prev_inited=0; s_prev_failed=0;
            s_cover_deb_sel=-1; return;
        }
        if (need_debounce) {
            if (v->sel != s_cover_deb_sel) {
                s_cover_deb_sel=v->sel; s_cover_deb_t=now; return;
            } else if (now - s_cover_deb_t < 150ULL) { return; }
        } else { s_cover_deb_sel = -1; }
        if (strcmp(s_prev_id, want) != 0) {
            snprintf(s_prev_id,sizeof s_prev_id,"%s",want);
            s_prev_raw_len=0; s_prev_inited=0; s_prev_failed=0;
        }
        if (!s_prev_inited && !s_prev_failed && s_prev_raw_len == 0) {
            char cp[256];
            snprintf(cp,sizeof cp,"sdmc:/3DSoundShell/Temp/pochettes/%s.png",s_prev_id);
            FILE *cf = fopen(cp,"rb");
            if (cf) {
                fseek(cf,0,SEEK_END); long cs=ftell(cf); fseek(cf,0,SEEK_SET);
                if (cs>0 && cs<=(long)ND_COVER_CAP && fread(s_prev_raw,1,cs,cf)==(size_t)cs)
                    s_prev_raw_len=(int)cs;
                fclose(cf);
            }
        }
        if (!s_prev_inited && !s_prev_failed && s_prev_raw_len > 0) {
            int w=0,h=0,ch=0;
            unsigned char *px = stbi_load_from_memory(s_prev_raw, s_prev_raw_len, &w, &h, &ch, 4);
            s_prev_raw_len = 0;
            if (px && w>0 && h>0 && w<=256 && h<=256) {
                if (!s_prev_tex_live && C3D_TexInit(&s_prev_tex,256,256,GPU_RGBA8))
                    s_prev_tex_live=1;
                if (s_prev_tex_live) {
                    u8 *dst=(u8*)s_prev_tex.data;
                    memset(dst,0,256*256*4);
                    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
                        u32 tile=((u32)y/8)*32+((u32)x/8);
                        u32 mx=(u32)x&7,my=(u32)y&7;
                        u32 mi=(mx&1)|((my&1)<<1)|((mx&2)<<1)|((my&2)<<2)|((mx&4)<<2)|((my&4)<<3);
                        u32 di=(tile*64+mi)*4;
                        const u8 *sp=px+((size_t)y*w+x)*4;
                        dst[di]=sp[3];dst[di+1]=sp[2];dst[di+2]=sp[1];dst[di+3]=sp[0];
                    }
                    GSPGPU_FlushDataCache(s_prev_tex.data,256*256*4);
                    C3D_TexSetFilter(&s_prev_tex,GPU_LINEAR,GPU_LINEAR);
                    C3D_TexSetWrap(&s_prev_tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
                    s_prev_subtex.width=(u16)w; s_prev_subtex.height=(u16)h;
                    s_prev_subtex.left=0; s_prev_subtex.top=1;
                    s_prev_subtex.right=(float)w/256.f; s_prev_subtex.bottom=1.f-(float)h/256.f;
                    s_prev_img.tex=&s_prev_tex; s_prev_img.subtex=&s_prev_subtex;
                    s_prev_inited=1;
                    snprintf(s_prev_have,sizeof s_prev_have,"%s",s_prev_id);
                }
                stbi_image_free(px);
            } else { if(px)stbi_image_free(px); s_prev_failed=1; }
            return;
        }
        if (!s_prev_inited && !s_prev_failed && s_prev_raw_len==0 && !s_prev_job_running) {
            Thread t; char vb[80];
            snprintf(vb,sizeof vb,"getCoverArt.view?id=%s&size=160",s_prev_id);
            nd_api_url(g_settings.navidrome_url,vb,g_settings.navidrome_user,
                       g_settings.navidrome_pass,s_prev_job_url,sizeof s_prev_job_url);
            snprintf(s_prev_job_id,sizeof s_prev_job_id,"%s",s_prev_id);
            s_prev_job_running=1;
            t=threadCreate(nd_prev_job_thread,NULL,32*1024,0x30,-1,true);
            if(!t) s_prev_job_running=0;
        }
    }
}

/* ---------- lecteur Navidrome dedie ---------- */
static void nd_fmt_time(int sec, char *out, int outsz)
{
    if (sec < 0) sec = 0;
    snprintf(out, (size_t)outsz, "%d:%02d", sec / 60, sec % 60);
}

static void nd_draw_player_top(void)
{
    Theme *th = nd_th();
    const NdItem *it = (s_np_idx >= 0 && s_np_idx < s_np_list_count)
                       ? &s_np_list[s_np_idx] : NULL;
    char t1[32], t2[32], line[112];
    int pos, dur;

    nd_rect(0, 0, TOP_WIDTH, 26, th->bg_header);
    nd_text(8, 5, 0.5f, th->text_accent, "Navidrome - Lecture");
    if (s_job_running && s_job == ND_JOB_DL && s_play_after) {
        static const char spin[4] = { '|', '/', '-', '\\' };
        char buf[64];
        snprintf(buf, sizeof buf, "chargement %c (en ligne)", spin[(osGetTime() / 150ULL) & 3]);
        nd_text(TOP_WIDTH - 140, 6, 0.36f, th->text_accent, buf);
    } else if (s_playing_from_sd) {
        nd_text(TOP_WIDTH - 110, 6, 0.36f, th->progress_fill, "repris de la carte SD");
    } else {
        nd_text(TOP_WIDTH - 96, 6, 0.36f, th->text_disabled, "en ligne, pas la SD");
    }

    /* pochette, grande, centree */
    if (s_cover_inited) {
        C2D_DrawParams p;
        memset(&p, 0, sizeof p);
        /* =========================================================================
           [3D] REGLAGE DU RELIEF DE LA GRANDE POCHETTE DU LECTEUR
           Modifier le coefficient (1.5f) ci-dessous pour changer la profondeur de relief
           ========================================================================= */
        extern int g_3d_eye;
        extern float g_3d_iod;
        float offset_3d = ((g_3d_eye == 0) ? -0.5f : 1.0f) * g_3d_iod;
        /* ========================================================================= */
        p.pos.x = ((float)(TOP_WIDTH - 128) / 2.f) + offset_3d; p.pos.y = 34.f;
        p.pos.w = 128.f; p.pos.h = 128.f;
        p.depth = 0.5f;
        nd_rect(p.pos.x - 2, p.pos.y - 2, 132, 132, th->border);
        C2D_DrawImage(s_cover_img, &p, NULL);
    } else {
        nd_rect((TOP_WIDTH - 128) / 2, 34, 128, 128, th->bg_secondary);
    }

    nd_text_ct(172.0f, 0.5f, th->text_primary,
               it ? it->title : "(morceau inconnu)", true, 255);
    if (it && it->sub[0])
        nd_text_ct(192.0f, 0.4f, th->text_secondary, it->sub, true, 255);

    /* barre de progression */
    pos = audio_get_position();
    dur = audio_get_duration();
    nd_rect(40, 210, TOP_WIDTH - 80, 6, th->progress_bg);
    if (dur > 0) {
        float frac = (float)pos / (float)dur;
        if (frac > 1.f) frac = 1.f;
        if (frac < 0.f) frac = 0.f;
        nd_rect(40, 210, (float)(TOP_WIDTH - 80) * frac, 6, th->progress_fill);
    }
    nd_fmt_time(pos, t1, sizeof t1);
    nd_fmt_time(dur, t2, sizeof t2);
    snprintf(line, sizeof line, "%s / %s", t1, dur > 0 ? t2 : "--:--");
    nd_text_ct(222.0f, 0.38f, th->text_secondary, line, true, 255);

    if (s_np_idx >= 0 && s_np_list_count > 0) {
        snprintf(line, sizeof line, "%d / %d", s_np_idx + 1, s_np_list_count);
        nd_text(8, TOP_HEIGHT - 16, 0.34f, th->text_disabled, line);
    }
}

/* Zones tactiles de l ecran du bas (memes coordonnees pour le dessin et la
   detection tactile, evidemment). */
#define ND_PBTN_Y      70.f
#define ND_PBTN_H      54.f
#define ND_PBTN_PREV_X 10.f
#define ND_PBTN_PLAY_X 118.f
#define ND_PBTN_NEXT_X 226.f
#define ND_PBTN_W      84.f
#define ND_BACKBTN_Y   140.f
#define ND_BACKBTN_H   40.f
#define ND_STARBTN_Y   32.f
#define ND_STARBTN_H   28.f

static void nd_draw_player_bottom(void)
{
    Theme *th = nd_th();
    bool playing = (audio_get_state() == AUDIO_PLAYING);
    bool can_prev = s_np_idx > 0;
    bool can_next = s_np_idx >= 0 && (s_np_idx + 1 < s_np_list_count || s_endless_radio);
    bool starred = (s_np_idx >= 0 && s_np_idx < s_np_list_count)
                  && s_np_list[s_np_idx].starred;

    nd_rect(10, ND_STARBTN_Y, BOT_WIDTH - 20, ND_STARBTN_H,
            starred ? th->progress_fill : th->bg_secondary);
    nd_text(125, ND_STARBTN_Y + 6, 0.4f, starred ? th->bg_primary : th->text_secondary, starred ? "[L] Favori" : "[L] Ajouter Favori");

    nd_rect(ND_PBTN_PREV_X, ND_PBTN_Y, ND_PBTN_W, ND_PBTN_H, th->bg_secondary);
    nd_rect(ND_PBTN_PLAY_X, ND_PBTN_Y, ND_PBTN_W, ND_PBTN_H, th->bg_secondary);
    nd_rect(ND_PBTN_NEXT_X, ND_PBTN_Y, ND_PBTN_W, ND_PBTN_H, th->bg_secondary);
    /* nd_text_ct centre sur toute la largeur de l ecran : pour des boutons
       cote a cote on cale donc le texte a la main avec nd_text. */
    nd_text(ND_PBTN_PREV_X + 6, ND_PBTN_Y + ND_PBTN_H / 2 - 8, 0.38f,
            can_prev ? th->text_primary : th->text_disabled, "<< Precedent");
    nd_text(ND_PBTN_PLAY_X + 14, ND_PBTN_Y + ND_PBTN_H / 2 - 8, 0.42f,
            th->text_accent, playing ? "Pause" : "Lecture");
    nd_text(ND_PBTN_NEXT_X + 6, ND_PBTN_Y + ND_PBTN_H / 2 - 8, 0.38f,
            can_next ? th->text_primary : th->text_disabled, "Suivant >>");

    nd_rect(10.f, ND_BACKBTN_Y, 145.f, ND_BACKBTN_H, th->bg_secondary);
    nd_text(10.f + 16.f, ND_BACKBTN_Y + ND_BACKBTN_H / 2 - 8, 0.40f, th->text_secondary, "Retour liste");
    u32 rbg = s_endless_radio ? th->text_accent : th->bg_secondary;
    u32 rfg = s_endless_radio ? th->bg_primary : th->text_secondary;
    nd_rect(165.f, ND_BACKBTN_Y, 145.f, ND_BACKBTN_H, rbg);
    nd_text(165.f + 12.f, ND_BACKBTN_Y + ND_BACKBTN_H / 2 - 8, 0.38f, rfg, s_endless_radio ? "Radio : ACTIVE" : "Radio : DESACTIVE");

    nd_text(10, 195, 0.36f, th->text_disabled,
            "A = lecture/pause   Haut/Bas = suivant   L = favori   B = retour");
}

/* true si (tx,ty) tombe dans le rectangle (x,y,w,h) de l ecran bas */
static bool nd_hit(int tx, int ty, float x, float y, float w, float h)
{ return tx >= (int)x && tx < (int)(x + w) && ty >= (int)y && ty < (int)(y + h); }

/* Gestes tactiles + touches du lecteur dedie. Retourne apres avoir traite un
   evenement pour ne jamais retomber dans la navigation de liste en dessous :
   c est bien un ecran a part, comme demande. */
static void nd_player_input(u32 kd)
{
    if (s_play_pending && !s_stream_started) return;
    /* START quitte deja tout le mode Navidrome plus haut : ici B seul revient
       a la liste. */
    if (kd & KEY_A)          audio_toggle_pause();
    else if (kd & (KEY_UP | KEY_LEFT))   nd_player_step(-1);
    else if (kd & (KEY_DOWN | KEY_RIGHT)) nd_player_step(+1);
    else if (kd & KEY_B)     s_player_view = false;
    else if (kd & KEY_L)     nd_player_toggle_star();
    else if ((kd & KEY_Y) && !s_job_running) nd_open_search();

    if (hidKeysDown() & KEY_TOUCH) {
        touchPosition tp; hidTouchRead(&tp);
        int tx = tp.px, ty = tp.py;
        if (nd_hit(tx, ty, 10, ND_STARBTN_Y, BOT_WIDTH - 20, ND_STARBTN_H))
            nd_player_toggle_star();
        else if (nd_hit(tx, ty, ND_PBTN_PREV_X, ND_PBTN_Y, ND_PBTN_W, ND_PBTN_H))
            nd_player_step(-1);
        else if (nd_hit(tx, ty, ND_PBTN_PLAY_X, ND_PBTN_Y, ND_PBTN_W, ND_PBTN_H))
            audio_toggle_pause();
        else if (nd_hit(tx, ty, ND_PBTN_NEXT_X, ND_PBTN_Y, ND_PBTN_W, ND_PBTN_H))
            nd_player_step(+1);
        else if (nd_hit(tx, ty, 10, ND_BACKBTN_Y, 145.f, ND_BACKBTN_H)) {
            s_player_view = false;
        } else if (nd_hit(tx, ty, 165.f, ND_BACKBTN_Y, 145.f, ND_BACKBTN_H)) {
            s_endless_radio = !s_endless_radio;
            g_settings.nd_endless_radio = s_endless_radio;
            extern void settings_save(void);
            settings_save();
        }
    }
}

void nd_pending_play_tick(void);

void navidrome_draw_top(void)
{
    nd_pending_play_tick();
    u64 now = osGetTime();
    u32 ms  = (u32)(now - s_t_start);
    float ph = (float)now * 0.004f;   /* ND3 : 4 rad/s CONTINU */
    float scale, logo_y, bar_y, body_a, logo_a, prog;
    int i;
    bool nav_mode;
    Theme *th = nd_th();

    nd_wifi_poll();
    nd_rect(0, 0, TOP_WIDTH, TOP_HEIGHT, th->bg_primary);
    nav_mode = (s_step == ND_S_OK && ms >= ND_T_FADE && !s_thread_running);

    /* --- timeline du logo (intro) --- */
    if (ms < ND_T_APPEAR) {
        prog = nd_ease((float)ms / (float)ND_T_APPEAR);
        scale = 0.55f + (ND_LOGO_MID - 0.55f) * prog;
        logo_a = prog;
        logo_y = TOP_HEIGHT * 0.5f - 12.0f;
        body_a = 0.0f;
    } else if (ms < ND_T_HOLD) {
        scale = ND_LOGO_MID; logo_a = 1.0f;
        logo_y = TOP_HEIGHT * 0.5f - 12.0f; body_a = 0.0f;
    } else if (ms < ND_T_SLIDE) {
        prog = nd_ease((float)(ms - ND_T_HOLD)
                       / (float)(ND_T_SLIDE - ND_T_HOLD));
        scale = ND_LOGO_MID + (ND_LOGO_TOP - ND_LOGO_MID) * prog;
        logo_a = 1.0f;
        logo_y = (TOP_HEIGHT * 0.5f - 12.0f)
                 + (20.0f - (TOP_HEIGHT * 0.5f - 12.0f)) * prog;
        body_a = 0.0f;
    } else {
        scale = ND_LOGO_TOP; logo_a = 1.0f; logo_y = 20.0f;
        body_a = (ms < ND_T_FADE)
                 ? (float)(ms - ND_T_SLIDE) / (float)(ND_T_FADE - ND_T_SLIDE)
                 : 1.0f;
    }

    if (nav_mode) {
        static bool s_last_player_view = false;
        if (s_player_view && !s_last_player_view) {
            if (s_np_idx >= 0 && s_np_idx < s_np_list_count) {
                s_cover_id[0] = 0; /* Force cache bust pour re-tÃ©lÃ©charger la pochette instantanÃ©ment */
                s_cover_inited = 0;
                s_cover_raw_len = 0;
                s_cover_failed = 0;
                nd_cover_request_now(s_np_list[s_np_idx].cover);
            }
        }
        s_last_player_view = s_player_view;
        nd_cover_tick();               /* ND10 : pochette (decode + requete) */
        nd_player_autoadvance_tick();  /* fin de piste -> suivante de la liste */
        nd_pending_play_tick();        /* piste demandee pendant un job annule */
        if (s_player_view)
            nd_draw_player_top();      /* ecran lecteur dedie */
        else
            nd_draw_nav(&s_stack[s_depth]);   /* ---- ND7 : 2 volets facon site ---- */
        if (s_msg_until && now >= s_msg_until)
            s_msg_until = 0;

        /* OVERLAYS PRIORITAIRES : Dessinés en tout dernier (flat, masque tout le fond) */
        if (s_confirm_disc) {
            s_nd_flat_draw = 1;
            nd_rect(0, 0, (float)TOP_WIDTH, (float)TOP_HEIGHT, RGBA8(0, 0, 0, 220));
            nd_rect(50.0f, 60.0f, 300.0f, 120.0f, th->bg_secondary);
            nd_rect(50.0f, 60.0f, 300.0f, 2.0f, th->accent);
            nd_rect(50.0f, 178.0f, 300.0f, 2.0f, th->border);
            nd_text_ct(85.0f, 0.52f, ND_ERRC, "Se deconnecter du serveur ?", true, 255);
            nd_text_ct(125.0f, 0.42f, th->text_primary, "A : Confirmer   B : Annuler", true, 255);
            s_nd_flat_draw = 0;
        } else if (s_job_running) {
            s_nd_flat_draw = 0; s_nd_overlay_draw = 1;
            nd_draw_job_overlay();
            s_nd_overlay_draw = 0;
        } else if (s_msg_until) {
            s_nd_flat_draw = 0; s_nd_overlay_draw = 1;
            nd_draw_msg_overlay();
            s_nd_overlay_draw = 0;
        }
        return;
    }

    /* ---- intro / connexion / erreur : barres + logo + etapes ---- */
    bar_y = (ms < ND_T_HOLD) ? (TOP_HEIGHT * 0.5f + 34.0f)
          : (ms < ND_T_SLIDE)
            ? (TOP_HEIGHT * 0.5f + 34.0f)
              + (92.0f - (TOP_HEIGHT * 0.5f + 34.0f))
              * nd_ease((float)(ms - ND_T_HOLD)
                        / (float)(ND_T_SLIDE - ND_T_HOLD))
          : 92.0f;
    {
        float grow = (ms < ND_T_APPEAR)
                     ? (float)ms / (float)ND_T_APPEAR
                     : 1.0f;
        /* ND5 : 8 barres = les 8 couleurs du degrade, sans reboucler */
        for (i = 0; i < 8; i++) {
            float h = (4.0f + 16.0f * fabsf(sinf(ph + i * 0.7f))) * grow;
            u32 barcol = (s_step == ND_S_ERR) ? ND_ERRC
                       : (s_step == ND_S_OK)  ? th->progress_fill
                       : th->visualizer_bars[i];
            barcol = (barcol & 0x00FFFFFFu) | ((u32)(logo_a * 255.0f) << 24);
            nd_rect(ND_CX - 53.0f + i * 14.0f, bar_y - h, 8.0f, h, barcol);
        }
    }
    nd_text_ex("NAVIDROME", 0, logo_y, scale, th->text_accent, true,
               (u8)(logo_a * 255.0f));

    if (body_a > 0.0f) {
        u8 a = (u8)(body_a * 255.0f);
        u32 ca = (th->text_secondary & 0x00FFFFFFu) | ((u32)a << 24);
        char vline[48];

        /* ND4 : version lue depuis include/main.h (APP_VERSION) */
        snprintf(vline, sizeof vline, "3DSoundShell - V%s", APP_VERSION);
        nd_text_ex(vline, 0, logo_y + 34.0f, 0.46f, ca, true, a);

        for (i = 0; i < 5; i++) {                  /* ND12 : 5 etapes */
            int stv = i + 1;
            const char *mark = "    ";
            u32 col = th->text_disabled;
            char line[64];
            if (stv == 1 && s_wifi_lost) {
                mark = " X  "; col = ND_ERRC;      /* ND8 : WiFi coupe */
            } else if (s_step == ND_S_OK || (s_step != ND_S_ERR && s_step > stv)) {
                mark = " ok "; col = th->progress_fill;
            } else if (s_step == ND_S_ERR && s_fail_at == stv) {
                mark = " X  "; col = ND_ERRC;
            } else if (s_step == stv) {
                mark = " >  "; col = th->text_primary;
            }
            if (i == 0 && s_step == ND_S_WIFI)     /* ND12 : statut ACU live */
                snprintf(line, sizeof line, "[%s] WiFi (statut %d)",
                         mark, (int)s_wifi_live);
            else
                snprintf(line, sizeof line, "[%s] %s", mark, s_step_names[i]);
            nd_text_ex(line, 0, 124.0f + i * 18.0f, 0.48f, col, true, a);
        }
        if (s_step == ND_S_ERR)
            nd_text_ex(s_err, 0, 220.0f, 0.44f, ND_ERRC, true, a);
    }
}

/* ---------- ecran bas ---------- */
void navidrome_draw_bottom(void)
{
    s_nd_flat_draw = 1;
    s_nd_flat_draw = 1;
    Theme *th = nd_th();
    const char *st;
    u32 stc;
    bool nav_mode = (s_step == ND_S_OK
                     && (u32)(osGetTime() - s_t_start) >= ND_T_FADE);

    nd_rect(0, 0, BOT_WIDTH, BOT_HEIGHT, th->bg_primary);
    nd_rect(0, 0, BOT_WIDTH, 26, th->bg_header);
    nd_text(6, 5, 0.52f, th->text_accent, "Navidrome");

    if (nav_mode && s_player_view) {
        nd_draw_player_bottom();
        s_nd_flat_draw = 0;
        return;
    }
    if (nav_mode) {
        NdView *v = &s_stack[s_depth];
        if (v->kind == ND_V_BROWSE) {
            if (v->pane == 0) {
                nd_text(10, 40, 0.46f, th->text_secondary,
                        "Haut/Bas = choix  A = volet droit");
                nd_text(10, 60, 0.46f, th->text_secondary,
                        "B/START = quitter le mode");
                nd_text(10, 80, 0.46f, th->text_secondary,
                        "Y = rechercher");
            } else {
                nd_text(10, 40, 0.46f, th->text_secondary,
                        "Haut/Bas = naviguer  A = ouvrir");
                nd_text(10, 60, 0.46f, th->text_secondary,
                        "G = volet gauche  B = retour gauche");
                nd_text(10, 80, 0.46f, th->text_secondary,
                        "Y = rechercher");
            }
        } else {
            nd_text(10, 40, 0.46f, th->text_secondary,
                    "Haut/Bas = naviguer");
            nd_text(10, 60, 0.46f, th->text_secondary,
                    v->kind == ND_V_TRACKS ? "A = ECOUTER maintenant"
                                           : "A = ouvrir l'album");
            if (v->kind == ND_V_TRACKS)
                nd_text(10, 80, 0.46f, th->text_accent,
                        "X = telecharger sur la SD");
            else
                nd_text(10, 80, 0.46f, th->text_secondary, "B = retour");
            /* PAS y=100 : la ligne de statut WiFi (toujours dessinee, quelques
               lignes plus bas) l'ecrivait par-dessus. */
            nd_text(10, 184, 0.46f, th->text_secondary, "Y = rechercher");
        }
        /* ND10 : pochette 96x96 a droite de l'ecran bas.
           s_cover_inited seul suffirait desormais (nd_cover_tick l efface a
           chaque fois que le contexte n en veut pas), la condition de vue
           reste par prudence, alignee sur nd_cover_tick (lcat 5 = Morceaux). */
        /* Pochette d'aperçu sur l'écran du bas (depth 0.99f = devant le fond 0.90f et le texte 0.95f) */
        if (s_prev_inited) {
            C2D_DrawParams p;
            memset(&p, 0, sizeof p);
            p.pos.x = 212.0f; p.pos.y = 99.0f; /* Position intermédiaire parfaite, pile au milieu */
            p.pos.w = 96.0f;  p.pos.h = 96.0f;
            p.depth = 0.99f; /* DEVANT le fond (0.90f) et le texte (0.95f) de l'écran du bas */
            C2D_DrawImage(s_prev_img, &p, NULL);
        }
        if (s_wifi_lost)
            nd_text(10, 100, 0.46f, ND_ERRC,
                    "WiFi [X] : connexion perdue");
        else
            nd_text(10, 100, 0.46f, th->progress_fill, "WiFi [ok]");
        
        /* Affichage de la pagination */
        if (s_page_offset > 0 || s_item_count >= s_page_size) {
            char page_info[64];
            int current_page = (s_page_offset / s_page_size) + 1;
            snprintf(page_info, sizeof(page_info), "Page %d (L/R)", current_page);
            nd_text(10, 184, 0.38f, th->text_accent, page_info);
        }
        if (s_job == ND_JOB_DL && s_job_running && s_play_after) {
            /* Lecture serveur : juste "ca charge", et plus rien des que le son
               est parti (le transfert finit tout seul derriere). */
            if (!s_stream_started)
                nd_text(10, 132, 0.42f, th->text_disabled, "Chargement...");
        } else if (s_job == ND_JOB_DL && s_job_running) {
            nd_rect(10, 116, 196, 10, th->progress_bg);
            if (s_dl_total > 0) {
                float frac = (float)s_dl_have / (float)s_dl_total;
                if (frac > 1.0f) frac = 1.0f;
                nd_rect(10, 116, 196.0f * frac, 10, th->progress_fill);
            }
            if (s_dl_total == 0 && s_job_t0) {
                /* ND11 : transcodage serveur = pas de taille annoncee */
                char el[64];
                snprintf(el, sizeof el,
                         "transcodage/telechargement... %llus",
                         (unsigned long long)((osGetTime() - s_job_t0) / 1000ULL));
                nd_text(10, 132, 0.40f, th->text_disabled, el);
            } else {
                nd_text(10, 132, 0.40f, th->text_disabled,
                        "telechargement en cours...");
            }
        }
        if (s_playing_nd) {            /* ND10 : titre en cours d'ecoute */
            char lp[112];
            snprintf(lp, sizeof lp, "Lecture : %.46s  (SELECT ou tactile)", s_play_title);
            nd_text(10, 148, 0.42f, th->text_accent, lp);
        }
        if (s_show_wifi) {
            char wl[40];
            snprintf(wl, sizeof wl, "statut ACU live = %d (0 = radio off)",
                     nd_get_wifi_live());
            nd_text(10, 166, 0.40f, th->text_disabled, wl);
        }
    } else {
        switch (s_step) {                          /* ND12 : etapes fines */
            case ND_S_OK:     st = "Connecte";               stc = th->progress_fill; break;
            case ND_S_ERR:    st = "Echec de connexion";     stc = ND_ERRC;           break;
            case ND_S_IDLE:   st = "Pret";                   stc = th->text_disabled; break;
            case ND_S_WIFI:   st = "1/5 Association WiFi..."; stc = th->accent;       break;
            case ND_S_HTTP:   st = "2/5 Module HTTP...";     stc = th->accent;        break;
            case ND_S_HTTPS:  st = "3/5 Serveur (https)..."; stc = th->accent;        break;
            case ND_S_SERVER: st = "4/5 Serveur (http)...";  stc = th->accent;        break;
            case ND_S_AUTH:   st = "5/5 Identification...";  stc = th->accent;        break;
            default:          st = "Connexion en cours";     stc = th->accent;        break;
        }
        nd_text(10, 44, 0.50f, stc, st);
        if (s_wifi_lost)
            nd_text(10, 88, 0.46f, ND_ERRC, "WiFi [X] : connexion perdue");
        else if (s_step == ND_S_OK)
            nd_text(10, 88, 0.46f, th->progress_fill, "WiFi [ok]");
        else if (s_step == ND_S_WIFI) {      /* ND10c : association en direct */
            char wal[52];
            snprintf(wal, sizeof wal,
                     "Association WiFi... statut = %d (>=2 = ok)",
                     nd_get_wifi_live());
            nd_text(10, 88, 0.44f, th->accent, wal);
            nd_text(10, 106, 0.44f, th->text_disabled,
                    "patiente... (B = annuler)");   /* ND10d : 120 s max */
        }
        if (s_step == ND_S_ERR) {
            nd_text(10, 106, 0.44f, th->text_accent,
                    "A = reessayer  Y = test reseau  B = retour");
            if (s_diag_done) {              /* ND11 : photo d'ecran svp */
                char d3[56];
                nd_text(10, 124, 0.40f, th->text_secondary, s_diag1);
                nd_text(10, 140, 0.40f, th->text_secondary, s_diag2);
                snprintf(d3, sizeof d3, "ACU=%d  http_pref=%d",
                         nd_get_wifi_live(), s_prefer_http);
                nd_text(10, 156, 0.40f, th->text_disabled, d3);
            }
        }
        if (s_show_wifi) {
            char wl[40];
            snprintf(wl, sizeof wl, "statut ACU live = %d (0 = radio off)",
                     nd_get_wifi_live());
            nd_text(10, 150, 0.40f, th->text_disabled, wl);
        }
        if (s_step == ND_S_ERR)
            nd_text(10, 68, 0.40f, th->text_secondary, s_err);
        else if (s_step == ND_S_OK)
            nd_text(10, 68, 0.40f, th->text_secondary, s_srvinfo);
    }

    nd_rect(0, BOT_HEIGHT - 28, BOT_WIDTH, 1, th->border);
    nd_text(10, BOT_HEIGHT - 22, 0.42f, th->text_disabled,
            "B / START = retour aux parametres");
    s_nd_flat_draw = 0;
}

/* âââ Redirection directe de la pochette et metadonnees pour le lecteur 3DSS âââ */
C2D_Image* nd_get_current_cover_img(void)
{
    extern int s_cover_inited;
    extern int s_cover_tex_live;
    extern C2D_Image s_cover_img;
    if (s_cover_inited && s_cover_tex_live) {
        return &s_cover_img;
    }
    return NULL;
}

const char* nd_get_playing_title(void)
{
    extern int s_np_idx;
    extern int s_np_list_count;
    extern NdItem s_np_list[];
    extern char s_play_title[];
    if (s_np_idx >= 0 && s_np_idx < s_np_list_count) {
        return s_np_list[s_np_idx].title;
    }
    return s_play_title;
}

const char* nd_get_playing_artist(void)
{
    extern int s_np_idx;
    extern int s_np_list_count;
    extern NdItem s_np_list[];
    static char artist[100];
    artist[0] = 0;
    if (s_np_idx >= 0 && s_np_idx < s_np_list_count) {
        strncpy(artist, s_np_list[s_np_idx].sub, sizeof(artist)-1);
        char *d = strstr(artist, "  ");
        if (d) *d = 0;
        return artist;
    }
    return "Artiste inconnu";
}

