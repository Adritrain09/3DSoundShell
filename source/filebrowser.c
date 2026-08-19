// filebrowser.c

#include "filebrowser.h"
#include "main.h"
#include "favorites.h"
#include "theme.h"
#include "i18n.h"
#include "preview.h"
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <citro2d.h>

/* ── TextBuf global ── */


#define ROW_H      22
#define LIST_Y     30
#define LIST_X     6

static bool ext_match(const char *name)
{
    const char *exts[] = {
        ".mp3",".mp2",".ogg",".oga",".flac",
        ".wav",".opus",
        NULL
        /* AAC, AIFF, WMA, AC3 non supportes sur 3DS */
    };
    const char *dot = strrchr(name,'.');
    if(!dot) return false;
    for(int i=0;exts[i];i++)
        if(!strcasecmp(dot, exts[i])) return true;
    return false;
}

static void fb_sort(FileBrowser *fb); // forward declaration

static SortMode s_sort_mode = SORT_NAME_AZ;

static int cmp_entries(const void *a, const void *b)
{
    const FileEntry *ea = (const FileEntry*)a;
    const FileEntry *eb = (const FileEntry*)b;
    // Dossiers toujours en premier
    if(ea->is_dir && !eb->is_dir) return -1;
    if(!ea->is_dir && eb->is_dir) return  1;
    if(s_sort_mode == SORT_NAME_ZA)
        return strcasecmp(eb->name, ea->name);
    return strcasecmp(ea->name, eb->name);
}

void fb_init(FileBrowser *fb, const char *start_path)
{
    fb->visible_rows = (BOT_HEIGHT - LIST_Y - 10) / ROW_H;
    fb_enter_dir(fb, start_path);
}

void fb_enter_dir(FileBrowser *fb, const char *path)
{
    strncpy(fb->cwd, path, MAX_PATH-1);
    fb->selected = 0;
    fb->scroll_offset = 0;
    fb_reload(fb);
}

void fb_go_up(FileBrowser *fb)
{
    char *slash = strrchr(fb->cwd, '/');
    if(!slash || slash == fb->cwd) return;
    *slash = '\0';
    fb_enter_dir(fb, fb->cwd);
}

void fb_reload(FileBrowser *fb)
{
    fb->count = 0;
    DIR *d = opendir(fb->cwd);
    if(!d) return;
    struct dirent *ent;
    while((ent=readdir(d)) && fb->count < MAX_FILES-1) {
        if(!strcmp(ent->d_name,".")) continue;
        if(!strcmp(ent->d_name,"..")) {
            if(!strcmp(fb->cwd,"sdmc:/") || !strcmp(fb->cwd,"/")) continue;
        }
        FileEntry *fe = &fb->entries[fb->count];
        strncpy(fe->name, ent->d_name, MAX_FILENAME-1);
        fe->name[MAX_FILENAME-1] = '\0';
        snprintf(fe->full_path, MAX_PATH-1, "%s/%s", fb->cwd, ent->d_name);
        fe->full_path[MAX_PATH-1] = '\0';
        fe->is_dir   = (ent->d_type == DT_DIR);
        fe->is_audio = !fe->is_dir && ext_match(ent->d_name);
        // Taille non lue au chargement (trop lent sur SD 3DS)
        fe->size = 0;
        fb->count++;
    }
    closedir(d);
    fb_sort(fb);
}

// Tri seul, sans relire la SD
void fb_sort(FileBrowser *fb)
{
    qsort(fb->entries, fb->count, sizeof(FileEntry), cmp_entries);
}

bool fb_is_audio(const char *filename) { return ext_match(filename); }

void fb_cycle_sort(FileBrowser *fb)
{
    s_sort_mode = (SortMode)((s_sort_mode + 1) % SORT_COUNT);
    fb_sort(fb);  // Tri en RAM uniquement, pas de relecture SD !
}

const char *fb_sort_name(const FileBrowser *fb)
{
    switch(s_sort_mode) {
        case SORT_NAME_AZ: return T("Nom A-Z");
        case SORT_NAME_ZA: return T("Nom Z-A");
        default:           return T("Nom A-Z");
    }
    return T("Nom A-Z");
}

void fb_select_next(FileBrowser *fb)
{
    if(fb->selected < fb->count-1) fb->selected++;
    if(fb->selected >= fb->scroll_offset + fb->visible_rows)
        fb->scroll_offset++;
    if (fb->selected >= 0 && fb->selected < fb->count) {
        FileEntry *fe = &fb->entries[fb->selected];
        
        if (fe->is_audio) preview_request(fe->full_path);
    }
}
void fb_select_prev(FileBrowser *fb)
{
    if(fb->selected > 0) fb->selected--;
    if(fb->selected < fb->scroll_offset)
        fb->scroll_offset--;
    if (fb->selected >= 0 && fb->selected < fb->count) {
        FileEntry *fe = &fb->entries[fb->selected];
        if (fe->is_audio) preview_request(fe->full_path);
    }
}

// ─── Drawing helpers ──────────────────────────────────────────
static void draw_rect(float x,float y,float w,float h,u32 col)
{
    C2D_DrawRectSolid(x,y,0,w,h,col);
}

static void draw_text(float x,float y,float sz,u32 col,const char *txt)
{
    g_textbuf_init();
    C2D_TextBufClear(g_textbuf);
    C2D_Text t;
    C2D_TextParse(&t, g_textbuf, txt);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,col);
}

// Top screen: infos du fichier selectionne (V0.95)
void fb_draw_top(const FileBrowser *fb)
{
    Theme *th = current_theme;

    draw_rect(0,0,TOP_WIDTH,26,th->bg_header);
    draw_text(8,5,0.55f,th->text_accent,"3DSoundShell  -  File Browser");

    draw_rect(0,26,TOP_WIDTH,14,th->bg_secondary);
    char pathbuf[80];
    snprintf(pathbuf,80,"  %s",fb->cwd);
    draw_text(4,28,0.43f,th->text_secondary,pathbuf);

    float info_x = 210, info_y = 44;
    float info_w = TOP_WIDTH - info_x - 5, info_h = 156;

    draw_rect(info_x, info_y, info_w, info_h, th->bg_secondary);
    draw_rect(info_x, info_y, info_w, 16,      th->bg_header);
    draw_text(info_x + 4, info_y + 2, 0.42f, th->text_accent, T("Infos"));

    const FileEntry *fe = NULL;
    if (fb->count > 0 && fb->selected >= 0 && fb->selected < fb->count)
        fe = &fb->entries[fb->selected];

    if (!fe) {
        draw_text(info_x + 8, info_y + 22, 0.38f, th->text_disabled,
                  T("Dossier vide"));
    } else if (fe->is_dir) {
        draw_text(info_x + 8, info_y + 22, 0.38f, th->text_secondary,
                  T("[Dossier]"));
        char n[24]; snprintf(n, 24, "%s", fe->name);
        draw_text(info_x + 8, info_y + 40, 0.38f, th->text_primary, n);
    } else if (!fe->is_audio) {
        draw_text(info_x + 8, info_y + 22, 0.38f, th->text_disabled,
                  T("Non audio"));
        char n[24]; snprintf(n, 24, "%s", fe->name);
        draw_text(info_x + 8, info_y + 40, 0.36f, th->text_secondary, n);
    } else {
        const PreviewInfo *pv = preview_get(fe->full_path);

        if (!pv || !pv->loaded) {
            draw_text(info_x + 8, info_y + 22, 0.38f, th->text_disabled,
                      T("Chargement..."));
            char n[24]; snprintf(n, 24, "%s", fe->name);
            draw_text(info_x + 8, info_y + 40, 0.36f, th->text_secondary, n);
        } else if (!pv->valid) {
            draw_text(info_x + 8, info_y + 22, 0.38f, th->accent2,
                      T("Erreur lecture"));
        } else {
            float ty = info_y + 20;
            float cx = info_x + 6, cy = ty, cs = 60;

            if (pv->has_cover && pv->cover_uploaded) {
                C2D_Image img = {
                    (C3D_Tex*)&pv->cover_tex,
                    (Tex3DS_SubTexture*)&pv->cover_subtex
                };
                float sx = cs / (float)pv->cover_subtex.width;
                float sy = cs / (float)pv->cover_subtex.height;
                C2D_DrawImageAt(img, cx, cy, 0, NULL, sx, sy);
                draw_rect(cx-1, cy-1, cs+2, 1,  th->accent);
                draw_rect(cx-1, cy+cs, cs+2, 1, th->accent);
                draw_rect(cx-1, cy-1, 1, cs+2,  th->accent);
                draw_rect(cx+cs, cy-1, 1, cs+2, th->accent);
            } else {
                draw_rect(cx, cy, cs, cs, th->bg_primary);
                draw_rect(cx, cy, cs, 1,  th->border);
                draw_rect(cx, cy+cs-1, cs, 1, th->border);
                draw_rect(cx, cy, 1, cs,  th->border);
                draw_rect(cx+cs-1, cy, 1, cs, th->border);
                draw_text(cx + 25, cy + 24, 0.44f, th->text_disabled, "?");
            }

            float tx = info_x + 72;
            char buf[64];

            snprintf(buf, 20, "%.18s", pv->title);
            draw_text(tx, ty, 0.38f, th->text_primary, buf);
            snprintf(buf, 20, "%.18s", pv->artist);
            draw_text(tx, ty + 14, 0.34f, th->text_accent, buf);
            snprintf(buf, 20, "%.18s", pv->album);
            draw_text(tx, ty + 28, 0.32f, th->text_secondary, buf);

            if (pv->year > 0) {
                snprintf(buf, 16, "%d", pv->year);
                draw_text(tx, ty + 42, 0.32f, th->text_disabled, buf);
            }

            float dy = cy + cs + 6;

            snprintf(buf, 32, "Format: %s", pv->format);
            draw_text(info_x + 6, dy, 0.34f, th->accent2, buf);
            dy += 12;

            if (pv->duration_sec > 0) {
                snprintf(buf, 32, "Duree: %d:%02d",
                    pv->duration_sec / 60, pv->duration_sec % 60);
                draw_text(info_x + 6, dy, 0.34f, th->text_secondary, buf);
                dy += 12;
            }

            if (pv->file_size > 0) {
                double mb = (double)pv->file_size / (1024.0 * 1024.0);
                if (mb >= 1.0)
                    snprintf(buf, 32, "Taille: %.1f Mo", mb);
                else
                    snprintf(buf, 32, "Taille: %.0f Ko",
                             (double)pv->file_size / 1024.0);
                draw_text(info_x + 6, dy, 0.34f, th->text_secondary, buf);
                dy += 12;
            }

            if (pv->channels > 0) {
                snprintf(buf, 32, "%s",
                    pv->channels == 2 ? "Stereo" : "Mono");
                draw_text(info_x + 6, dy, 0.34f, th->text_secondary, buf);
                dy += 12;
            }

            if (pv->bitrate_kbps > 0) {
                snprintf(buf, 32, "Bitrate: %d kbps", pv->bitrate_kbps);
                draw_text(info_x + 6, dy, 0.34f, th->text_secondary, buf);
                dy += 12;
            }

            if (pv->genre[0]) {
                snprintf(buf, 32, "%.20s", pv->genre);
                draw_text(info_x + 6, dy, 0.32f, th->text_disabled, buf);
            }
        }
    }

    draw_text(10, 60,  0.42f, th->text_primary,   T("Navigation"));
    draw_text(10, 76,  0.38f, th->text_disabled,  T("A = Ouvrir"));
    draw_text(10, 90,  0.38f, th->text_disabled,  T("B = Retour"));
    draw_text(10, 104, 0.38f, th->text_disabled,  T("Start = Lecteur"));
    draw_text(10, 118, 0.38f, th->text_disabled,  T("Select = Reglages"));
    draw_text(10, 132, 0.38f, th->text_disabled,  T("L = Favori"));
    draw_text(10, 146, 0.38f, th->text_disabled,  T("R = Trier"));
    draw_text(10, 160, 0.38f, th->text_disabled,  T("X = Ajouter playlist"));
    draw_text(10, 174, 0.38f, th->text_disabled,  T("Y = Ajouter dossier"));

    char sort_info[32];
    snprintf(sort_info, 32, "%s: %s", T("Tri"), fb_sort_name(fb));
    draw_text(10, 196, 0.42f, th->text_accent, sort_info);
}

// Bottom screen: scrollable file list
void fb_draw_bottom(const FileBrowser *fb)
{
    Theme *th = current_theme;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);
    // Header row
    draw_rect(0,0,BOT_WIDTH,LIST_Y,th->bg_header);
    draw_text(6,7,0.52f,th->text_primary,T("Fichiers"));

    int end = fb->scroll_offset + fb->visible_rows;
    if(end > fb->count) end = fb->count;

    for(int i=fb->scroll_offset; i<end; i++) {
        const FileEntry *fe = &fb->entries[i];
        float y = LIST_Y + (i - fb->scroll_offset)*ROW_H;
        u32 bg = (i==fb->selected) ? th->bg_selected : th->bg_primary;
        draw_rect(0,y,BOT_WIDTH,ROW_H,bg);

        // Alternating subtle stripe
        if(i%2==0 && i!=fb->selected)
            draw_rect(0,y,BOT_WIDTH,ROW_H,RGBA8(0,0,0,15));

        // Icon char
        const char *icon = fe->is_dir ? "[D]" : (fe->is_audio ? "[A]" : "[ ]");
        u32 icon_col = fe->is_dir ? th->accent : (fe->is_audio ? th->text_accent : th->text_disabled);
        draw_text(LIST_X, y+4, 0.44f, icon_col, icon);

        // Favori etoile
        if (fe->is_audio && fav_is_fav(fe->full_path))
            draw_text(LIST_X+28, y+4, 0.44f, th->accent2, "*");

        // Name (truncate)
        char name[36]; snprintf(name,36,"%s",fe->name);
        u32 name_col = (i==fb->selected) ? th->text_primary :
                       (fe->is_audio ? th->text_primary : th->text_secondary);
        float name_off = (fe->is_audio && fav_is_fav(fe->full_path)) ? 38.f : 28.f;
        draw_text(LIST_X+name_off, y+4, 0.44f, name_col, name);



        // Separator
        draw_rect(0,y+ROW_H-1,BOT_WIDTH,1,th->border);
    }

    // Fleches navigation tactile (haut/bas, coin droit)
    // Fleche haut : Y=30 a 52, X=300 a 320
    draw_rect(300, 30, 20, 22, th->bg_secondary);
    draw_text(304, 33, 0.50f, th->text_primary, "^");
    // Fleche bas : Y=218 a 240, X=300 a 320
    draw_rect(300, 218, 20, 22, th->bg_secondary);
    draw_text(304, 221, 0.50f, th->text_primary, "v");

    /* Notif MAJ temporaire */
    draw_update_notif_timed();

    // Scrollbar
    if(fb->count > fb->visible_rows) {
        float sb_h = (float)BOT_HEIGHT / fb->count * fb->visible_rows;
        float sb_y = LIST_Y + (float)(fb->scroll_offset)/fb->count*(BOT_HEIGHT-LIST_Y);
        draw_rect(BOT_WIDTH-4, LIST_Y, 4, BOT_HEIGHT-LIST_Y, th->bg_secondary);
        draw_rect(BOT_WIDTH-4, sb_y,   4, sb_h, th->scrollbar);
    }
}
