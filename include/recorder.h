#ifndef RECORDER_H
#define RECORDER_H

#include <3ds.h>

void recorder_enter(void);
void recorder_leave(void);
void recorder_input(int *state);
void recorder_draw_top(void);
void recorder_draw_bottom(void);
void recorder_update(void);
int  recorder_is_active(void);
void recorder_pause_for_sleep(void);
void recorder_resume_from_sleep(void);

#endif
