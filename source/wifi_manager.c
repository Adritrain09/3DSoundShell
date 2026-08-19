// wifi_manager.c - Gestion Wi-Fi hardware
#include "wifi_manager.h"
#include <3ds.h>
#include <stdio.h>

static bool s_nwm_ext_ready = false;
static bool s_wifi_disabled = false;
static bool s_original_wifi_state = true; /* Etat au demarrage de l app */

void wifi_manager_init(void)
{
    s_nwm_ext_ready = false;
    s_wifi_disabled = false;
    s_original_wifi_state = true;

    /* Tenter d init nwm:EXT (nécessaire pour couper le wifi)
       Attention : nécessite un CFW (Luma) et permission */
    Result rc = nwmExtInit();
    if (R_SUCCEEDED(rc)) {
        s_nwm_ext_ready = true;
    }
}

void wifi_manager_exit(void)
{
    /* IMPERATIF : reactiver le wifi si on l a coupe */
    wifi_manager_enable();

    if (s_nwm_ext_ready) {
        nwmExtExit();
        s_nwm_ext_ready = false;
    }
}

void wifi_manager_disable(void)
{
    if (!s_nwm_ext_ready || s_wifi_disabled) return;

    Result rc = NWMEXT_ControlWirelessEnabled(false);
    if (R_SUCCEEDED(rc)) {
        s_wifi_disabled = true;
    }
}

void wifi_manager_enable(void)
{
    if (!s_nwm_ext_ready) return;

    /* V0.97 : FORCE l activation (meme si on ne pense pas l avoir desactive)
       car le systeme peut avoir garde l etat OFF apres shutdown/HOME */
    NWMEXT_ControlWirelessEnabled(true);
    s_wifi_disabled = false;
}

bool wifi_manager_is_disabled(void)
{
    return s_wifi_disabled;
}
