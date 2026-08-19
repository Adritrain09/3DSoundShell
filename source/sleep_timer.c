// sleep_timer.c - Sleep timer avec pause propre (V0.97 v2)
#include "sleep_timer.h"
#include <3ds.h>
#include "audio.h"
#include "settings.h"
#include <stdio.h>

/* Forward declarations V0.97 */
extern bool power_save_is_active(void);
extern void power_save_deactivate(void);
extern void wifi_manager_enable(void);
extern void settings_save_sync(void);

/* ── Etat simple ─────────────────────────────────────────── */
static bool  s_active         = false;   /* timer configure */
static bool  s_paused         = false;   /* actuellement en pause */
static int   s_total_sec      = 0;       /* duree totale configuree */
static int   s_elapsed_sec    = 0;       /* secondes ecoulees */
static u64   s_last_tick_ms   = 0;       /* dernier tick osGetTime */
static float s_fade           = 1.0f;

/* Fade out durant les X dernieres secondes */
#define FADE_DURATION_SEC 5

void sleep_timer_init(void)
{
    s_active = false;
    s_paused = false;
    s_total_sec = 0;
    s_elapsed_sec = 0;
    s_fade = 1.0f;
}

void sleep_timer_exit(void)
{
    sleep_timer_stop();
}

void sleep_timer_start(int minutes)
{
    if (minutes <= 0) {
        sleep_timer_stop();
        return;
    }
    /* Reset complet */
    s_total_sec = minutes * 60;
    s_elapsed_sec = 0;
    s_last_tick_ms = osGetTime();
    s_active = true;
    s_paused = false;
    s_fade = 1.0f;
}

void sleep_timer_stop(void)
{
    s_active = false;
    s_paused = false;
    s_total_sec = 0;
    s_elapsed_sec = 0;
    s_fade = 1.0f;
}

bool sleep_timer_is_active(void)
{
    return s_active;
}

bool sleep_timer_is_paused(void)
{
    return s_active && s_paused;
}

int sleep_timer_get_remaining_sec(void)
{
    if (!s_active) return 0;
    int rem = s_total_sec - s_elapsed_sec;
    return rem > 0 ? rem : 0;
}

float sleep_timer_get_fade(void)
{
    return s_fade;
}

void sleep_timer_pause(void)
{
    if (!s_active || s_paused) return;
    s_paused = true;
    /* On ne touche pas a s_elapsed_sec, il reste fige */
}

void sleep_timer_resume(void)
{
    if (!s_active || !s_paused) return;
    s_paused = false;
    /* Reset last_tick pour eviter un "saut" au reprise */
    s_last_tick_ms = osGetTime();
}

void sleep_timer_update(void)
{
    if (!s_active) return;
    if (s_paused) {
        /* En pause : on ne fait RIEN, on ne compte pas le temps */
        return;
    }

    u64 now = osGetTime();
    /* Increment s_elapsed_sec de facon precise */
    u64 delta_ms = now - s_last_tick_ms;
    if (delta_ms >= 1000) {
        int delta_sec = (int)(delta_ms / 1000);
        s_elapsed_sec += delta_sec;
        s_last_tick_ms += (u64)delta_sec * 1000ULL;
    }

    int remaining = sleep_timer_get_remaining_sec();

    if (remaining <= 0) {
        /* Timer expire → action selon setting */
        extern Settings g_settings;
        s_fade = 0.0f;
        audio_set_volume(0.0f);
        audio_pause();

        /* V0.97 : Si option "shutdown" active → vraie extinction console
           Sinon : juste pause audio (deja fait au dessus) */
        if (g_settings.sleep_shutdown) {
            /* DEBUG : log pour voir pourquoi shutdown ne marche pas */
            FILE *_f = fopen("sdmc:/3DSoundShell/shutdown_debug.log", "w");
            if (_f) {
                fprintf(_f, "=== Sleep timer expire, tentative shutdown ===\n");
                fprintf(_f, "power_save_is_active = %d\n", power_save_is_active());
                fclose(_f);
            }
            /* V0.97 : Desactiver le mode eco avant shutdown pour eviter blocage */
            if (power_save_is_active()) power_save_deactivate();

            /* Sauvegarder les settings avant */
            settings_save_sync();

            /* V0.97 : IMPERATIF reactiver le Wi-Fi AVANT le shutdown */
            wifi_manager_enable();

            /* Petite pause pour que la commande wifi soit effective */
            svcSleepThread(500000000LL); /* 500ms */

            /* Vraie extinction via PTMSYSM_ShutdownAsync (propre)
               Necessite ptmSysmInit qui a des permissions elevees */
            Result rc = ptmSysmInit();
            FILE *_ff = fopen("sdmc:/3DSoundShell/shutdown_debug.log", "a");
            if (_ff) {
                fprintf(_ff, "ptmSysmInit rc=%08lX\n", rc);
                fclose(_ff);
            }
            if (R_SUCCEEDED(rc)) {
                Result rc2 = PTMSYSM_ShutdownAsync(0);
                _ff = fopen("sdmc:/3DSoundShell/shutdown_debug.log", "a");
                if (_ff) {
                    fprintf(_ff, "PTMSYSM_ShutdownAsync rc=%08lX\n", rc2);
                    fclose(_ff);
                }
                ptmSysmExit();
            } else {
                _ff = fopen("sdmc:/3DSoundShell/shutdown_debug.log", "a");
                if (_ff) {
                    fprintf(_ff, "FALLBACK APT_HardwareResetAsync\n");
                    fclose(_ff);
                }
                APT_HardwareResetAsync();
            }
        }

        sleep_timer_stop();
        audio_set_volume(g_settings.volume);
        return;
    }

    /* Fade out durant les X dernieres secondes */
    if (remaining < FADE_DURATION_SEC) {
        s_fade = (float)remaining / (float)FADE_DURATION_SEC;
        audio_set_volume(g_settings.volume * s_fade);
    } else {
        s_fade = 1.0f;
    }
}
