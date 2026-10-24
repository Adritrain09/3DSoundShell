#pragma once
#include <string.h>
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(p)  (*(const unsigned char*)(p))
#define pgm_read_word(p)  (*(const unsigned short*)(p))
#define pgm_read_dword(p) (*(const unsigned int*)(p))
#define pgm_read_float(p) (*(const float*)(p))
#define pgm_read_pointer(p) (*(void* const*)(p))
#define memcpy_P memcpy
#define strlen_P strlen
#define strcpy_P strcpy
