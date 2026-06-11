// favorites.c

#include "favorites.h"
#include <stdio.h>
#include <string.h>

Favorites g_favorites = {0};

void fav_init(void) { memset(&g_favorites, 0, sizeof(g_favorites)); }

void fav_load(void)
{
    fav_init();
    FILE *f = fopen(FAV_PATH, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f) && g_favorites.count < MAX_FAVORITES) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        strncpy(g_favorites.items[g_favorites.count].path,  line, 511);
        strncpy(g_favorites.items[g_favorites.count].title,
                strrchr(line,'/') ? strrchr(line,'/')+1 : line, 255);
        g_favorites.count++;
    }
    fclose(f);
}

void fav_save(void)
{
    FILE *f = fopen(FAV_PATH, "w");
    if (!f) return;
    for (int i = 0; i < g_favorites.count; i++)
        fprintf(f, "%s\n", g_favorites.items[i].path);
    fclose(f);
}

bool fav_is_fav(const char *path)
{
    for (int i = 0; i < g_favorites.count; i++)
        if (!strcmp(g_favorites.items[i].path, path)) return true;
    return false;
}

bool fav_add(const char *path, const char *title)
{
    if (g_favorites.count >= MAX_FAVORITES) return false;
    if (fav_is_fav(path)) return false;
    strncpy(g_favorites.items[g_favorites.count].path,  path,  511);
    strncpy(g_favorites.items[g_favorites.count].title, title, 255);
    g_favorites.count++;
    fav_save();
    return true;
}

bool fav_remove(const char *path)
{
    for (int i = 0; i < g_favorites.count; i++) {
        if (!strcmp(g_favorites.items[i].path, path)) {
            for (int j = i; j < g_favorites.count-1; j++)
                g_favorites.items[j] = g_favorites.items[j+1];
            g_favorites.count--;
            fav_save();
            return true;
        }
    }
    return false;
}

void fav_toggle(const char *path, const char *title)
{
    if (fav_is_fav(path)) fav_remove(path);
    else                  fav_add(path, title);
}
