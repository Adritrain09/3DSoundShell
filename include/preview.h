// preview.h - Prévisualisation métadonnées fichier sélectionné
// Chargement asynchrone dans un thread dédié (non bloquant)
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdbool.h>

#define PREVIEW_CACHE_SIZE 8

typedef struct {
    char path[512];
    char title[256];
    char artist[256];
    char album[256];
    char genre[64];
    char format[8];
    int  year;
    int  duration_sec;
    u64  file_size;
    u32  sample_rate;
    int  channels;
    int  bitrate_kbps;

    bool          has_cover;
    bool          cover_uploaded;
    C3D_Tex       cover_tex;
    Tex3DS_SubTexture cover_subtex;
    int           cover_w;
    int           cover_h;

    bool loaded;
    bool valid;
    u64  last_used;
} PreviewInfo;

void preview_init(void);
void preview_exit(void);
void preview_request(const char *path);
const PreviewInfo *preview_get(const char *path);
void preview_update_gpu(void);

/* V0.95 : activer/desactiver le worker preview
   → utile pour ne pas gener l audio quand on est dans le player */
void preview_set_active(bool active);
