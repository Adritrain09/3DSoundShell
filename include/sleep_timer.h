// sleep_timer.h - Sleep timer : arret auto apres X minutes
#pragma once
#include <3ds.h>
#include <stdbool.h>

/* Init / cleanup */
void sleep_timer_init(void);
void sleep_timer_exit(void);

/* Demarre le timer avec X minutes (0 = OFF) */
void sleep_timer_start(int minutes);

/* Arrete le timer */
void sleep_timer_stop(void);

/* A appeler chaque frame pour mettre a jour et declencher l arret */
void sleep_timer_update(void);

/* Retourne true si le timer est actif */
bool sleep_timer_is_active(void);

/* Retourne le temps restant en secondes (0 si inactif) */
int  sleep_timer_get_remaining_sec(void);

/* Retourne le fade multiplier actuel (0.0 a 1.0) - utilise pour fade out */
float sleep_timer_get_fade(void);

/* Pause / reprend le timer (garde le temps restant) */
void sleep_timer_pause(void);
void sleep_timer_resume(void);
bool sleep_timer_is_paused(void);
