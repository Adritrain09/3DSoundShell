/* mp4_aac.h - Decodeur et Demuxer MP4/M4A/AAC natif pour 3DSoundShell */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    FILE *file;
    uint32_t sample_rate;
    uint32_t channels;
    uint64_t total_samples;
    uint32_t duration_sec;

    /* Offset mdat */
    uint64_t mdat_offset;
    uint64_t mdat_size;
    uint64_t current_read_offset;

    /* Metadata */
    char title[256];
    char artist[256];
    char album[256];
    uint8_t *cover_data;
    uint32_t cover_size;
} MP4AACDecoder;

MP4AACDecoder* mp4_aac_open(const char *path);
int            mp4_aac_read_pcm16(MP4AACDecoder *dec, int16_t *buf, int max_pairs);
void           mp4_aac_seek_sample(MP4AACDecoder *dec, uint64_t target_sample);
void           mp4_aac_close(MP4AACDecoder *dec);

#ifdef __cplusplus
}
/* AAC12 : lecture legere pour l'explorateur (preview.c), reentrant */
int mp4_aac_peek(const char *path, char *title, char *artist, char *album, int cap,
                 uint8_t **cover, uint32_t *cover_size,
                 uint32_t *rate, uint32_t *channels, uint32_t *duration_sec);
#endif
