// update.c — Vérification MAJ

#include "main.h"
#include "wifi_manager.h"
#include "settings.h"
#include "theme.h"
#include <citro2d.h>
#include <3ds.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration */
void ping_stats_start(void);


static void dbg(const char *msg) { (void)msg; }
char g_latest_version[16] = {0};
bool g_update_available   = false;
bool g_server_unreachable = false; /* V0.96 : serveur MAJ HS */
bool g_version_ahead      = false; /* V0.96 : version installee > enregistree */

/* V0.96 : compare 2 versions type "0.95" vs "0.96"
   Retourne -1 si a<b, 0 si egal, 1 si a>b */
static int compare_versions(const char *a, const char *b)
{
    int a_maj = 0, a_min = 0;
    int b_maj = 0, b_min = 0;
    sscanf(a, "%d.%d", &a_maj, &a_min);
    sscanf(b, "%d.%d", &b_maj, &b_min);
    if (a_maj != b_maj) return (a_maj < b_maj) ? -1 : 1;
    if (a_min != b_min) return (a_min < b_min) ? -1 : 1;
    return 0;
}



static bool fetch_version(const char *url, char *out, int outsz)
{
    httpcContext ctx;
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(rc)) {
        char buf[48]; snprintf(buf,48,"OpenCtx fail %08lX",rc);
        dbg(buf); return false;
    }

    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "3DSoundShell");

    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) {
        char buf[48]; snprintf(buf,48,"BeginReq fail %08lX",rc);
        dbg(buf); httpcCloseContext(&ctx); return false;
    }
    /* V0.96 : yield apres begin request pour laisser respirer */
    svcSleepThread(50000000LL); /* 50ms */

    /* Attendre réponse max 10sec */
    u32 status = 0;
    Result src = (Result)-1;
    for (int i = 0; i < 100; i++) {
        src = httpcGetResponseStatusCode(&ctx, &status);
        if (!R_FAILED(src)) break;
        svcSleepThread(100000000LL);
    }

    char sbuf[48]; snprintf(sbuf,48,"status=%lu rc=%08lX",status,src);
    dbg(sbuf);

    if (R_FAILED(src)) { httpcCloseContext(&ctx); return false; }

    /* Redirection */
    if (status == 301 || status == 302 || status == 303) {
        char loc[512] = {0};
        httpcGetResponseHeader(&ctx, "Location", loc, sizeof(loc));
        httpcCloseContext(&ctx);
        if (!loc[0]) return false;
        dbg(loc);
        return fetch_version(loc, out, outsz);
    }

    if (status != 200) { httpcCloseContext(&ctx); return false; }

    char buf[64] = {0};
    u32 read = 0;
    httpcDownloadData(&ctx, (u8*)buf, sizeof(buf)-1, &read);
    httpcCloseContext(&ctx);

    char rbuf[48]; snprintf(rbuf,48,"read=%lu [%s]",read,buf); dbg(rbuf);
    if (!read) return false;

    /* Nettoyer */
    for (int i = 0; buf[i]; i++) {
        if (buf[i]=='\n'||buf[i]=='\r'||buf[i]==' '||buf[i]=='\t') {
            buf[i]=0; break;
        }
    }
    if (!buf[0]) return false;

    /* V0.96 : VALIDER que la reponse ressemble a une version type "X.Y" ou "X.Y.Z"
       et pas du HTML (erreur Cloudflare) ou autre garbage */
    bool valid = true;
    int  digit_count = 0;
    int  dot_count = 0;
    for (int i = 0; buf[i] && i < 15; i++) {
        char c = buf[i];
        if (c >= '0' && c <= '9') digit_count++;
        else if (c == '.') dot_count++;
        else { valid = false; break; }
    }
    /* Une vraie version a au moins 1 digit et max 2 points (X.Y ou X.Y.Z) */
    if (!valid || digit_count < 1 || dot_count > 2) {
        return false;
    }

    strncpy(out, buf, outsz-1);
    out[outsz-1] = 0;
    return true;
}

static void update_thread_func(void *arg)
{
    (void)arg;

    /* Attendre que le système soit prêt */
    svcSleepThread(3000000000LL);

    /* V0.96 : Reset des flags au debut - on ne sait rien encore */
    g_update_available   = false;
    g_version_ahead      = false;
    g_server_unreachable = false;

    /* V0.96 : Charger le local d abord pour que settings ait toujours qqch a afficher */
    {
        FILE *fl = fopen("sdmc:/3DSoundShell/version.txt","r");
        if (fl) {
            char buf[32] = {0};
            fgets(buf, sizeof(buf), fl);
            fclose(fl);
            for (int i = 0; buf[i]; i++) {
                if (buf[i]=='\n'||buf[i]=='\r'||buf[i]==' ') { buf[i]=0; break; }
            }
            if (buf[0]) {
                strncpy(g_latest_version, buf, 15);
                g_latest_version[15] = 0;
                /* V0.96 : NE PAS set update/ahead ici - on attend le fetch reseau
                   pour eviter d afficher "Version en avance" a tort pendant les 10s de sleep */
            }
        }
    }

    char fetched[16] = {0};
    bool got = false;
    bool has_network = false;

    /* V0.96 : Verifier wifi avant tenter reseau */
    u32 wifi_status = 0;
    ACU_GetWifiStatus(&wifi_status);
    has_network = (wifi_status != 0);
    bool got_from_network = false; /* V0.96 : true si fetch reseau OK */

    /* V0.96 : essayer le réseau UNIQUEMENT si wifi dispo */
    Result rc = -1;
    if (has_network) {
        rc = httpcInit(0);
    char hbuf[48]; snprintf(hbuf,48,"httpcInit rc=%08lX",rc);
    dbg(hbuf);

    if (!R_FAILED(rc)) {
        got = fetch_version(
            "http://copyparty.hosten.uk/Publique-Adri/version.txt?raw",
            fetched, sizeof(fetched));

        if (got) {
            /* Sauvegarder sur SD */
            FILE *fw = fopen("sdmc:/3DSoundShell/version.txt","w");
            if (fw) { fprintf(fw,"%s\n",fetched); fclose(fw); }
        }
        httpcExit();
    }
    } /* fin if has_network */

    /* V0.96 : PAS de fallback local ici (deja charge en base) */

    /* V0.96 : LOGIQUE 3 CAS NETTE */
    if (!has_network) {
        /* CAS 1 : pas de wifi → SILENCIEUX (settings garde le local charge en base) */
        g_server_unreachable = false;
        /* PAS de notif timer = silencieux */
    } else if (!got) {
        /* CAS 2 : wifi OK mais serveur HS → afficher "Serveur inaccessible" */
        g_server_unreachable = true;
        g_update_notif_timer = 300; /* 5sec */
        /* g_latest_version reste avec la valeur locale chargee au debut */
        /* MAIS on masque les flags update/ahead car le serveur ne repond pas
           (la notif "Serveur inaccessible" a priorite absolue) */
        g_update_available = false;
        g_version_ahead    = false;
    } else {
        /* CAS 3 : Tout OK → mise a jour et notif normale */
        g_server_unreachable = false;
        strncpy(g_latest_version, fetched, 15);
        g_latest_version[15] = 0;
        int cmp = compare_versions(APP_VERSION, g_latest_version);
        g_update_available = (cmp < 0);
        g_version_ahead    = (cmp > 0);
        g_update_notif_timer = 300; /* 5sec */
    }

    char vbuf[48];
    snprintf(vbuf,48,"final: [%s] update=%d unreach=%d ahead=%d",
        g_latest_version, g_update_available,
        g_server_unreachable, g_version_ahead);
    dbg(vbuf);

    /* V0.96 : chainer le ping stats maintenant que update est fini
       → une seule connexion HTTPC a la fois = plus de freeze */
    svcSleepThread(2000000000LL); /* 2s de pause avant le ping */
    ping_stats_start();
}


/* ============================================================
   PING STATS - envoie un ping anonyme au serveur
   pour compter les utilisateurs actifs (1x par jour, IP hashee)
   ============================================================ */
static void ping_stats_thread(void *arg)
{
    (void)arg;

    /* V0.96 : ping declenche apres update, plus besoin d attendre 20s */
    svcSleepThread(500000000LL); /* 500ms de securite */
    Result rc = httpcInit(0);
    if (R_FAILED(rc)) {
        return;
    }

    /* V0.95 : Detection console (modele + langue + uid) */

    /* 1. Modele exact */
    u8 model_id = 0;
    CFGU_GetSystemModel(&model_id);
    const char *model_names[] = {
        "Old3DS", "Old3DS_XL", "New3DS", "Old2DS", "New3DS_XL", "New2DS_XL"
    };
    const char *model_str = (model_id < 6) ? model_names[model_id] : "Unknown";

    /* 2. Langue systeme */
    u8 sys_lang = 1;
    CFGU_GetSystemLanguage(&sys_lang);
    const char *lang_names[] = {"JP","EN","FR","DE","IT","ES","ZH","KO","NL","PT","RU","TW"};
    const char *lang_str = (sys_lang < 12) ? lang_names[sys_lang] : "EN";

    /* 3. UID console (hash anonyme pour compter les consoles uniques) */
    u64 device_id = 0;
    CFGU_GenHashConsoleUnique(0, &device_id);
    /* Hash FNV-1a sur les 8 octets pour anonymiser */
    u32 uid_hash = 2166136261u;
    u8 *dp = (u8*)&device_id;
    for (int i = 0; i < 8; i++) {
        uid_hash ^= dp[i];
        uid_hash *= 16777619u;
    }

    char url[512];
    snprintf(url, sizeof(url),
        "http://3dsoundshell.hosten.uk/api/ping?v=%s&model=%s&lang=%s&uid=%08lx",
        APP_VERSION, model_str, lang_str, (unsigned long)uid_hash);

    /* Boucle pour suivre les redirections (max 3 fois) */
    int redirect_count = 0;
    while (redirect_count < 3) {

        

        httpcContext ctx;
        Result open_rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
        if (R_FAILED(open_rc)) {
            break;
        }
        /* CONFIGURATION IDENTIQUE a fetch_version qui MARCHE */
        httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
        httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
        httpcAddRequestHeaderField(&ctx, "User-Agent", "3DSoundShell");

        Result req_rc = httpcBeginRequest(&ctx);
        if (R_FAILED(req_rc)) {
            httpcCloseContext(&ctx);
            break;
        }
        /* Attendre reponse max 10 sec (comme fetch_version) */
        u32 status = 0;
        Result src = (Result)-1;
        for (int i = 0; i < 100; i++) {
            src = httpcGetResponseStatusCode(&ctx, &status);
            if (R_SUCCEEDED(src)) break;
            svcSleepThread(100000000LL);
        }

        

        if (R_FAILED(src)) {
            httpcCloseContext(&ctx);
            break;
        }

        /* Gerer les redirections (301, 302, 303) */
        if (status == 301 || status == 302 || status == 303) {
            char new_url[512] = {0};
            httpcGetResponseHeader(&ctx, "Location", new_url, sizeof(new_url));
            httpcCloseContext(&ctx);

            

            if (!new_url[0]) break;

            strncpy(url, new_url, sizeof(url)-1);
            url[sizeof(url)-1] = 0;
            redirect_count++;
            continue;
        }

        /* Lire la reponse */
        char response[512] = {0};
        u32 bytes_read = 0;
        Result dl_rc = httpcDownloadData(&ctx, (u8*)response, sizeof(response)-1, &bytes_read);

        

        httpcCloseContext(&ctx);
        break;
    }

    httpcExit();

    /* V0.97 : Couper le wifi si option ON (fin du ping stats) */
    extern Settings g_settings;
    if (g_settings.wifi_off_in_app) {
        svcSleepThread(1000000000LL); /* 1s de securite */
        wifi_manager_disable();
    }

    
}

/* Lance le ping en thread detache (non bloquant) */
void ping_stats_start(void)
{
    threadCreate(ping_stats_thread, NULL, 32*1024, 0x3F, -1, true);
}

void update_check_start(void)
{
    
    threadCreate(update_thread_func, NULL, 32*1024, 0x3F, -1, true);
}

/* Affiche la notif MAJ en bas à gauche si timer actif */
void draw_update_notif_timed(void)
{
    if (g_update_notif_timer <= 0) return;
    Theme *th = current_theme;

    /* Fond semi-transparent */
    C2D_DrawRectSolid(0, 220, 0, 320, 20, RGBA8(0,0,0,160));

    if (g_server_unreachable) {
        C2D_Text tx; C2D_TextBuf tb = C2D_TextBufNew(64);
        C2D_TextParse(&tx, tb, "Serveur inaccessible");
        C2D_TextOptimize(&tx);
        C2D_DrawText(&tx, C2D_AlignLeft|C2D_WithColor,
            8, 222, 0, 0.50f, 0.50f, RGBA8(255,150,50,255));
        C2D_TextBufDelete(tb);
    } else if (g_version_ahead) {
        /* V0.96 : version en avance */
        C2D_Text tx; C2D_TextBuf tb = C2D_TextBufNew(64);
        C2D_TextParse(&tx, tb, "Version en avance");
        C2D_TextOptimize(&tx);
        C2D_DrawText(&tx, C2D_AlignLeft|C2D_WithColor,
            8, 222, 0, 0.50f, 0.50f, RGBA8(100,200,255,255));
        C2D_TextBufDelete(tb);
    } else if (g_update_available) {
        char maj_notif[48];
        snprintf(maj_notif, 48, "Mise a jour V%s disponible !", g_latest_version);
        C2D_Text tx; C2D_TextBuf tb = C2D_TextBufNew(128);
        C2D_TextParse(&tx, tb, maj_notif);
        C2D_TextOptimize(&tx);
        C2D_DrawText(&tx, C2D_AlignLeft|C2D_WithColor,
            8, 222, 0, 0.50f, 0.50f, th->accent2);
        C2D_TextBufDelete(tb);
    } else if (g_latest_version[0]) {
        C2D_Text tx; C2D_TextBuf tb = C2D_TextBufNew(64);
        C2D_TextParse(&tx, tb, "Version a jour.");
        C2D_TextOptimize(&tx);
        C2D_DrawText(&tx, C2D_AlignLeft|C2D_WithColor,
            8, 222, 0, 0.50f, 0.50f, th->text_disabled);
        C2D_TextBufDelete(tb);
    }
}
