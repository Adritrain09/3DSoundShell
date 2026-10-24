/* recorder.h - 3DSoundShell Magnetophone */
#ifndef RECORDER_H
#define RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

void recorder_enter(void);
void recorder_leave(void);
void recorder_update(void);
void recorder_input(int *state);
void recorder_draw_top(void);
void recorder_draw_bottom(void);
int  recorder_is_active(void);

#ifdef __cplusplus
}
#endif

#endif
