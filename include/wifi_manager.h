// wifi_manager.h - Gestion Wi-Fi pour economie batterie
// IMPORTANT : le Wi-Fi doit imperativement etre reactive quand on quitte
#pragma once
#include <3ds.h>
#include <stdbool.h>

void wifi_manager_init(void);
void wifi_manager_exit(void);

/* Coupe le Wi-Fi (appele apres init si option activee) */
void wifi_manager_disable(void);

/* Reactive le Wi-Fi (appele avant HOME/exit imperativement) */
void wifi_manager_enable(void);

/* Retourne true si on a coupe le wifi (pour savoir s il faut le remettre) */
bool wifi_manager_is_disabled(void);
