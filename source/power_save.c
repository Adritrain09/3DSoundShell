// power_save.c - Mode economie energie (V0.97 propre)
#include "power_save.h"
#include "main.h"
#include "theme.h"
#include "settings.h"
#include "audio.h"
#include "i18n.h"
#include "sleep_timer.h"
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ── Etat interne ─────────────────────────────────────────── */
static bool     s_active           = false;
static u64      s_last_activity_ms = 0;
static u64      s_hold_start_ms    = 0;  /* V0.96 : hold B pour sortir */
static u8       s_saved_brightness = 5;
static bool     s_bottom_off       = false;
static C2D_TextBuf s_ps_textbuf    = NULL;

#define POWER_SAVE_DELAY_MS 5000
#define POWER_SAVE_BRIGHTNESS 0

/* ── Init ─────────────────────────────────────────────────── */
void power_save_init(void)
{
    s_active = false;
    s_last_activity_ms = osGetTime();
    s_bottom_off = false;
    if (!s_ps_textbuf) s_ps_textbuf = C2D_TextBufNew(512);
}

void power_save_exit(void)
{
    /* V0.97 : IMPORTANT restaurer les 2 ecrans avant de quitter (bug 2DS) */
    gspLcdInit();
    if (s_bottom_off) {
        /* V0.97 : hardware backlight retire - overlay noir suffit */
        s_bottom_off = false;
    }
    gspLcdExit();
    s_active = false;

    if (s_ps_textbuf) {
        C2D_TextBufDelete(s_ps_textbuf);
        s_ps_textbuf = NULL;
    }
}

/* ── Activation ───────────────────────────────────────────── */
void power_save_activate(void)
{
    if (s_active) return;
    s_active = true;
    gspLcdInit();
    s_saved_brightness = 5;
    /* V0.97 : Detection 2DS - baisser lum sur les 2 ecrans (1 seul LCD physique)
       Valeur minimum 1 (0 = ignore par le systeme sur 2DS) */
    u8 model = 0;
    CFGU_GetSystemModel(&model);
    bool is_2ds = (model == 3 || model == 5);  /* 3=Old2DS, 5=New2DSXL */


    /* V0.97 : Mode eco backlight
       OFF     : backlight bas OFF seulement
       REDUIRE : lum haut min + backlight bas OFF
       ETEINDRE: backlight haut + bas OFF */
    int mode = g_settings.eco_backlight_mode;

    /* Ecran bas TOUJOURS eteint en mode eco (sauf 2DS) */
    if (!is_2ds) {
        GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
        s_bottom_off = true;
    }

    if (mode == 1) {
        /* REDUIRE : lum haut au minimum */
        GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP, 1);
    } else if (mode == 2) {
        /* ETEINDRE : couper aussi backlight haut */
        if (!is_2ds) {
            GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_TOP);
        } else {
            GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM, 1);
        }
    }

    gspLcdExit();

    audio_set_viz_active(false);

    extern bool g_is_new3ds;
    if (g_is_new3ds) {
        osSetSpeedupEnable(false);
    }
}

void power_save_deactivate(void)
{
    if (!s_active) return;
    s_active = false;

    gspLcdInit();
    /* V0.97 : Rallumer les 2 backlight (haut + bas) systematiquement */
    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_TOP);
    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
    s_bottom_off = false;

    /* V0.97 : Si on avait baisse la lum (mode REDUIRE), la restaurer */
    if (g_settings.eco_backlight_mode == 1) {
        int lum = g_settings.eco_restore_brightness;
        if (lum < 1 || lum > 5) lum = 5;
        GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM, lum);
    }
    gspLcdExit();

    audio_set_viz_active(true);

    extern bool g_is_new3ds;
    if (g_is_new3ds) {
        osSetSpeedupEnable(true);
    }
}

void power_save_refresh(void)
{
    if (!s_active) return;
    gspLcdInit();

    u8 model = 0;
    CFGU_GetSystemModel(&model);
    bool is_2ds = (model == 3 || model == 5);

    /* V0.97 : Re-appliquer le mode eco (retour de HOME) */
    int mode = g_settings.eco_backlight_mode;

    /* Ecran bas TOUJOURS eteint (sauf 2DS) */
    if (!is_2ds) {
        GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
        s_bottom_off = true;
    }

    if (mode == 1) {
        GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP, 1);
    } else if (mode == 2) {
        if (!is_2ds) {
            GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_TOP);
        } else {
            GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM, 1);
        }
    }
    gspLcdExit();
}

/* V0.97 : Relire la vraie luminosite systeme (utile si l utilisateur
   la change en cours d app via APT+bouton Home) */
void power_save_sync_brightness(void)
{
    u8 cur = 5;
    Result rb = MCUHWC_ReadRegister(0x21, &cur, 1);
    if (R_SUCCEEDED(rb) && cur >= 1 && cur <= 5) {
        s_saved_brightness = cur;
    }
}

void power_save_force_restore(void)
{
    gspLcdInit();
    /* V0.97 : TOUJOURS rallumer les 2 backlight */
    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_TOP);
    GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
    s_bottom_off = false;

    /* Si on avait baisse la lum, la restaurer */
    if (g_settings.eco_backlight_mode == 1) {
        int lum = g_settings.eco_restore_brightness;
        if (lum < 1 || lum > 5) lum = 5;
        GSPLCD_SetBrightness(GSPLCD_SCREEN_TOP | GSPLCD_SCREEN_BOTTOM, lum);
    }
    gspLcdExit();
}

/* ── Update ───────────────────────────────────────────────── */
void power_save_update(u32 keys_pressed, bool touch_down)
{
    if (!g_settings.power_save_enabled) {
        if (s_active) power_save_deactivate();
        s_last_activity_ms = osGetTime();
        s_hold_start_ms = 0;
        return;
    }

    u64 now = osGetTime();

    if (s_active) {
        /* En mode eco : SEUL B hold 500ms fait sortir */
        if (keys_pressed & KEY_B) {
            if (s_hold_start_ms == 0) {
                s_hold_start_ms = now;
            } else if (now - s_hold_start_ms >= 500) {
                s_last_activity_ms = now;
                s_hold_start_ms = 0;
                power_save_deactivate();
            }
        } else {
            s_hold_start_ms = 0;
        }
        return;
    }

    /* Hors mode eco : toute activite reset le timer */
    if (keys_pressed != 0 || touch_down) {
        s_last_activity_ms = now;
        s_hold_start_ms = 0;
        return;
    }

    u64 delay_ms = (u64)g_settings.power_save_delay_sec * 1000ULL;
    if (delay_ms == 0) delay_ms = POWER_SAVE_DELAY_MS;

    if (now - s_last_activity_ms >= delay_ms) {
        power_save_activate();
    }
}

/* ── Getters ──────────────────────────────────────────────── */
bool power_save_is_active(void) { return s_active; }

/* ── Dessin ecran haut ────────────────────────────────────── */
static void ps_draw_text(float x, float y, float sz, u32 col, const char *txt)
{
    if (!txt || !txt[0] || !s_ps_textbuf) return;
    C2D_Text tx;
    C2D_TextParse(&tx, s_ps_textbuf, txt);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx, C2D_AlignCenter | C2D_WithColor,
                 x, y, 0, sz, sz, col);
}

void power_save_draw_top(const AudioMetadata *meta)
{
    if (!s_ps_textbuf) s_ps_textbuf = C2D_TextBufNew(512);
    C2D_TextBufClear(s_ps_textbuf);

    /* Fond noir */
    C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, TOP_HEIGHT,
                      C2D_Color32(0, 0, 0, 255));

    /* V0.97 : Sleep timer haut-droit (MEME position que player_ui) */
    if (sleep_timer_is_active()) {
        int rem = sleep_timer_get_remaining_sec();
        char tstr[32];
        if (rem >= 3600)
            snprintf(tstr, 32, "SLEEP %s %d:%02d:%02d",
                sleep_timer_is_paused() ? "||" : ">",
                rem / 3600, (rem % 3600) / 60, rem % 60);
        else
            snprintf(tstr, 32, "SLEEP %s %d:%02d",
                sleep_timer_is_paused() ? "||" : ">",
                rem / 60, rem % 60);
        u32 tcol = sleep_timer_is_paused()
            ? C2D_Color32(140, 140, 160, 255)
            : settings_get_power_save_color();
        C2D_Text tx_slp;
        C2D_TextParse(&tx_slp, s_ps_textbuf, tstr);
        C2D_TextOptimize(&tx_slp);
        C2D_DrawText(&tx_slp, C2D_AlignRight | C2D_WithColor,
                     TOP_WIDTH - 8, 8, 0, 0.42f, 0.42f, tcol);
    }

    /* Horloge */
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char clock_str[16] = "00:00:00";
    if (tm_info) {
        snprintf(clock_str, 16, "%02d:%02d:%02d",
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    ps_draw_text(TOP_WIDTH / 2.f, 40, 1.8f,
                 settings_get_power_save_color(), clock_str);

    /* Musique en cours */
    AudioState st = audio_get_state();
    if (meta && st == AUDIO_PLAYING && meta->title[0]) {
        ps_draw_text(TOP_WIDTH / 2.f, 130, 0.5f,
                     C2D_Color32(100, 200, 100, 255), T("> LECTURE"));

        char title[42];
        snprintf(title, 42, "%.40s", meta->title);
        ps_draw_text(TOP_WIDTH / 2.f, 155, 0.65f,
                     settings_get_power_save_color(), title);

        if (meta->artist[0]) {
            char artist[42];
            snprintf(artist, 42, "%.40s", meta->artist);
            u32 c = settings_get_power_save_color();
            u8 r = ((c>>0)&0xff) * 7 / 10;
            u8 g = ((c>>8)&0xff) * 7 / 10;
            u8 b = ((c>>16)&0xff) * 7 / 10;
            ps_draw_text(TOP_WIDTH / 2.f, 180, 0.5f,
                         C2D_Color32(r, g, b, 255), artist);
        }

        if (meta->album[0]) {
            char album[42];
            snprintf(album, 42, "%.40s", meta->album);
            u32 c = settings_get_power_save_color();
            u8 r = ((c>>0)&0xff) / 2;
            u8 g = ((c>>8)&0xff) / 2;
            u8 b = ((c>>16)&0xff) / 2;
            ps_draw_text(TOP_WIDTH / 2.f, 200, 0.42f,
                         C2D_Color32(r, g, b, 255), album);
        }
    } else {
        ps_draw_text(TOP_WIDTH / 2.f, 160, 0.5f,
                     C2D_Color32(100, 100, 120, 255),
                     T("Mode economie d energie"));
    }

    /* Hint centre */
    ps_draw_text(TOP_WIDTH / 2.f, TOP_HEIGHT - 25, 0.35f,
                 C2D_Color32(60, 60, 80, 255),
                 T("Maintenir B (0.5s) pour quitter le mode eco"));
    ps_draw_text(TOP_WIDTH / 2.f, TOP_HEIGHT - 10, 0.38f,
                 C2D_Color32(80, 80, 110, 255),
                 T("Commandes ralenties en mode eco"));

    /* Batterie en bas a droite */
    u8 batt_pct = 0, charging = 0;
    MCUHWC_GetBatteryLevel(&batt_pct);
    PTMU_GetBatteryChargeState(&charging);
    int pct = (int)batt_pct;

    char bstr[16];
    if (charging)  snprintf(bstr, 16, "%d%%+", pct);
    else           snprintf(bstr, 16, "%d%%", pct);

    u32 batt_col;
    if (pct <= 20 && !charging) {
        batt_col = C2D_Color32(255, 80, 80, 255);
    } else {
        u32 c = settings_get_power_save_color();
        u8 r = ((c>>0)&0xff) * 8 / 10;
        u8 g = ((c>>8)&0xff) * 8 / 10;
        u8 b = ((c>>16)&0xff) * 8 / 10;
        batt_col = C2D_Color32(r, g, b, 255);
    }

    C2D_Text tx_batt;
    C2D_TextParse(&tx_batt, s_ps_textbuf, bstr);
    C2D_TextOptimize(&tx_batt);
    C2D_DrawText(&tx_batt, C2D_AlignRight | C2D_WithColor,
                 TOP_WIDTH - 8, TOP_HEIGHT - 20, 0, 0.5f, 0.5f, batt_col);

    float bx = TOP_WIDTH - 8 - (strlen(bstr) * 12) - 24;
    float by = TOP_HEIGHT - 18;
    C2D_DrawRectSolid(bx, by, 0, 18, 10, batt_col);
    C2D_DrawRectSolid(bx+1, by+1, 0, 16, 8, C2D_Color32(0, 0, 0, 255));
    float fill_w = (float)pct / 100.f * 14.f;
    C2D_DrawRectSolid(bx+2, by+2, 0, fill_w, 6, batt_col);
    C2D_DrawRectSolid(bx+18, by+3, 0, 2, 4, batt_col);
}
