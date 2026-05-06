#include "input.h"

void input_update(InputState *inp)
{
    hidScanInput();
    inp->held  = hidKeysHeld();
    inp->down  = hidKeysDown();
    inp->up    = hidKeysUp();
    hidTouchRead(&inp->touch);
    inp->touch_down = (inp->down  & KEY_TOUCH) != 0;
    inp->touch_held = (inp->held  & KEY_TOUCH) != 0;
}
