// power_save.h - Mode economie energie (ecran bas OFF + horloge)
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "audio.h"

/* Init / cleanup */
void power_save_init(void);
void power_save_exit(void);

/* Appele a chaque frame - met a jour le timer d inactivite */
void power_save_update(u32 keys_pressed, bool touch_down);

/* Retourne true si le mode eco est actif */
bool power_save_is_active(void);

/* Force activation/desactivation */
void power_save_activate(void);
void power_save_deactivate(void);

/* V0.96 : refresh mode eco apres reveil/wakeup */
void power_save_refresh(void);

/* V0.97 : Re-lire la vraie luminosite systeme (utile si user la change) */
void power_save_sync_brightness(void);

/* V0.96 : force restauration des ecrans (avant HOME/exit) */
void power_save_force_restore(void);

/* Dessin de l ecran du haut en mode eco */
void power_save_draw_top(const AudioMetadata *meta);
