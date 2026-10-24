#ifndef POWER_SAVE_H
#define POWER_SAVE_H

#include <3ds.h>
#include <stdbool.h>
#include "audio.h"

void power_save_init(void);
void power_save_activate(void);
void power_save_deactivate(void);
bool power_save_is_active(void);
void power_save_update(u32 keys_pressed, bool touch_down);
void power_save_draw_top(const AudioMetadata *meta);

bool power_save_should_suppress_b(void);
bool power_save_is_b_locked(u32 keys_held);

void power_save_force_restore(void);
void power_save_sync_brightness(void);
void power_save_refresh(void);
void power_save_exit(void);

#endif // POWER_SAVE_H
