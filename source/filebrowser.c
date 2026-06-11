// filebrowser.c

#include "filebrowser.h"
#include "favorites.h"
#include "theme.h"
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
        ".wav",".aiff",".aac",".m4a",".opus",
        ".wma",".ac3", NULL
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
        case SORT_NAME_AZ: return "Nom A-Z";
        case SORT_NAME_ZA: return "Nom Z-A";
        default:           return "Nom A-Z";
    }
    return "Nom A-Z";
}

void fb_select_next(FileBrowser *fb)
{
    if(fb->selected < fb->count-1) fb->selected++;
    if(fb->selected >= fb->scroll_offset + fb->visible_rows)
        fb->scroll_offset++;
}
void fb_select_prev(FileBrowser *fb)
{
    if(fb->selected > 0) fb->selected--;
    if(fb->selected < fb->scroll_offset)
        fb->scroll_offset--;
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

// Top screen: show current path + big title
void fb_draw_top(const FileBrowser *fb)
{
    Theme *th = current_theme;
    // Header bar
    draw_rect(0,0,TOP_WIDTH,26,th->bg_header);
    draw_text(8,5,0.55f,th->text_accent,"3DSoundShell  —  File Browser");
    // Path
    draw_rect(0,26,TOP_WIDTH,14,th->bg_secondary);
    char pathbuf[56];
    snprintf(pathbuf,56,"  %s",fb->cwd);
    draw_text(4,28,0.43f,th->text_secondary,pathbuf);
    // Big hint
    draw_text(10,120,0.5f,th->text_disabled,"A = Open   B = Back    Select = Reglage");
    draw_text(10,140,0.5f,th->text_disabled,"Start = Player    L = Favori");
    /* Tri actuel */
    char sort_info[32];
    snprintf(sort_info, 32, "Tri: %s  [R]", fb_sort_name(fb));
    draw_text(10, 160, 0.45f, th->text_accent, sort_info);
}

// Bottom screen: scrollable file list
void fb_draw_bottom(const FileBrowser *fb)
{
    Theme *th = current_theme;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);
    // Header row
    draw_rect(0,0,BOT_WIDTH,LIST_Y,th->bg_header);
    draw_text(6,7,0.52f,th->text_primary,"Files");

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

    // Scrollbar
    if(fb->count > fb->visible_rows) {
        float sb_h = (float)BOT_HEIGHT / fb->count * fb->visible_rows;
        float sb_y = LIST_Y + (float)(fb->scroll_offset)/fb->count*(BOT_HEIGHT-LIST_Y);
        draw_rect(BOT_WIDTH-4, LIST_Y, 4, BOT_HEIGHT-LIST_Y, th->bg_secondary);
        draw_rect(BOT_WIDTH-4, sb_y,   4, sb_h, th->scrollbar);
    }
}
