#pragma once
#include <stdbool.h>

#define MAX_FAVORITES 50
#define FAV_PATH      "sdmc:/3DSoundShell/favorites.txt"

typedef struct {
    char path[512];
    char title[256];
} FavoriteEntry;

typedef struct {
    FavoriteEntry items[MAX_FAVORITES];
    int           count;
} Favorites;

extern Favorites g_favorites;

void fav_init(void);
void fav_load(void);
void fav_save(void);
bool fav_add(const char *path, const char *title);
bool fav_remove(const char *path);
bool fav_is_fav(const char *path);
void fav_toggle(const char *path, const char *title);
