/* cpu_detect_arm.cpp - Stub pour ARM (3DS) */
/* SoundTouch n a pas besoin de detection CPU speciale sur ARM */
#include "cpu_detect.h"

uint detectCPUextensions(void)
{
    return 0; /* Pas de SSE/AVX sur ARM */
}
