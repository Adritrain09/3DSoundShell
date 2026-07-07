// update.c — Vérification MAJ

#include "main.h"
#include "theme.h"
#include <citro2d.h>
#include <3ds.h>
#include <string.h>
#include <stdio.h>

char g_latest_version[16] = {0};
bool g_update_available   = false;

static void dbg(const char *msg)
{
    FILE *f = fopen("sdmc:/3DSoundShell/update_debug.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
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
        dbg("redirect ->");
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

    strncpy(out, buf, outsz-1);
    out[outsz-1] = 0;
    return true;
}

static void update_thread_func(void *arg)
{
    (void)arg;
    dbg("=== update start V2 ===");

    /* Attendre que le système soit prêt */
    svcSleepThread(3000000000LL);
    dbg("sleep done");

    char fetched[16] = {0};
    bool got = false;

    /* Toujours essayer le réseau d'abord */
    Result rc = httpcInit(0);
    char hbuf[48]; snprintf(hbuf,48,"httpcInit rc=%08lX",rc);
    dbg(hbuf);

    if (!R_FAILED(rc)) {
        dbg("trying fetch...");
        got = fetch_version(
            "http://copyparty.hosten.uk/Publique-Adri/version.txt?raw",
            fetched, sizeof(fetched));

        if (got) {
            /* Sauvegarder sur SD */
            FILE *fw = fopen("sdmc:/3DSoundShell/version.txt","w");
            if (fw) { fprintf(fw,"%s\n",fetched); fclose(fw); dbg("saved!"); }
        }
        httpcExit();
    }

    /* Fallback local si réseau échoue */
    if (!got) {
        dbg("trying local version.txt...");
        FILE *f = fopen("sdmc:/3DSoundShell/version.txt","r");
        if (f) {
            char buf[32] = {0};
            fgets(buf, sizeof(buf), f);
            fclose(f);
            for (int i = 0; buf[i]; i++) {
                if (buf[i]=='\n'||buf[i]=='\r'||buf[i]==' ') {
                    buf[i]=0; break;
                }
            }
            if (buf[0]) {
                strncpy(fetched, buf, 15);
                got = true;
                char msg[32]; snprintf(msg,32,"local: [%s]",fetched); dbg(msg);
            }
        } else {
            dbg("no local file");
        }
    }

    if (!got) { dbg("no version info"); return; }

    strncpy(g_latest_version, fetched, 15);
    g_latest_version[15] = 0;
    g_update_available = (strcmp(g_latest_version, APP_VERSION) != 0);

    /* Démarrer le timer MAINTENANT qu on a le résultat */
    g_update_notif_timer = 300; /* 5sec a 60fps */

    char vbuf[48];
    snprintf(vbuf,48,"final: [%s] update=%d",g_latest_version,g_update_available);
    dbg(vbuf);
}

void update_check_start(void)
{
    FILE *f = fopen("sdmc:/3DSoundShell/update_debug.log","w");
    if (f) { fprintf(f,"=== update_check_start ===\n"); fclose(f); }
    threadCreate(update_thread_func, NULL, 32*1024, 0x3F, -1, true);
}

/* Affiche la notif MAJ en bas à gauche si timer actif */
void draw_update_notif_timed(void)
{
    if (g_update_notif_timer <= 0) return;
    Theme *th = current_theme;

    /* Fond semi-transparent */
    C2D_DrawRectSolid(0, 220, 0, 320, 20, RGBA8(0,0,0,160));

    if (g_update_available) {
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
