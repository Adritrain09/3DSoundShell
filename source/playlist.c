#include "playlist.h"
#include "theme.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <citro2d.h>

#define ROW_H  22
#define LIST_Y 30

// ─── Helpers ──────────────────────────────────────────────────
static bool is_audio(const char *n)
{
    const char *exts[]=
        {".mp3",".mp2",".ogg",".flac",".wav",
         ".aiff",".aac",".m4a",".opus",".wma",NULL};
    const char *d=strrchr(n,'.');
    if(!d) return false;
    for(int i=0;exts[i];i++) if(!strcasecmp(d,exts[i])) return true;
    return false;
}

static void draw_rect(float x,float y,float w,float h,u32 c)
{ C2D_DrawRectSolid(x,y,0,w,h,c); }

static void draw_text(float x,float y,float sz,u32 c,const char *t)
{
    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(512);
    C2D_TextParse(&tx,tb,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
    C2D_TextBufDelete(tb);
}

// ─── Init ─────────────────────────────────────────────────────
void pl_init(Playlist *pl)
{
    memset(pl,0,sizeof(Playlist));
    pl->current=-1;
    pl->selected=0;
    pl->shuffle=false;
    pl->repeat=REPEAT_NONE;
}

// ─── Add single file ──────────────────────────────────────────
void pl_add(Playlist *pl, const char *path, const char *title)
{
    if(pl->count>=MAX_PLAYLIST) return;
    PlaylistEntry *e=&pl->items[pl->count];
    strncpy(e->path, path, MAX_PATH-1);
    strncpy(e->title,title,MAX_FILENAME-1);
    e->duration_sec=0;
    if(pl->current<0) pl->current=0;
    pl->count++;
    if(pl->shuffle) pl_rebuild_shuffle(pl);
}

// ─── Add all audio files from a directory ────────────────────
void pl_add_dir(Playlist *pl, const char *dir_path)
{
    DIR *d=opendir(dir_path);
    if(!d) return;
    struct dirent *ent;
    while((ent=readdir(d))) {
        if(!is_audio(ent->d_name)) continue;
        char fp[MAX_PATH];
        snprintf(fp,MAX_PATH,"%s/%s",dir_path,ent->d_name);
        pl_add(pl,fp,ent->d_name);
    }
    closedir(d);
}

void pl_remove(Playlist *pl, int idx)
{
    if(idx<0||idx>=pl->count) return;
    for(int i=idx;i<pl->count-1;i++) pl->items[i]=pl->items[i+1];
    pl->count--;
    if(pl->current>=pl->count) pl->current=pl->count-1;
    if(pl->shuffle) pl_rebuild_shuffle(pl);
}

void pl_clear(Playlist *pl)
{
    pl->count=0; pl->current=-1; pl->selected=0; pl->scroll_offset=0;
}

// ─── Shuffle ──────────────────────────────────────────────────
void pl_toggle_shuffle(Playlist *pl)
{
    pl->shuffle=!pl->shuffle;
    if(pl->shuffle) pl_rebuild_shuffle(pl);
}

void pl_rebuild_shuffle(Playlist *pl)
{
    for(int i=0;i<pl->count;i++) pl->shuffle_order[i]=i;
    // Fisher-Yates
    for(int i=pl->count-1;i>0;i--) {
        int j=rand()%(i+1);
        int tmp=pl->shuffle_order[i];
        pl->shuffle_order[i]=pl->shuffle_order[j];
        pl->shuffle_order[j]=tmp;
    }
}

// ─── Navigation ───────────────────────────────────────────────
int pl_next(Playlist *pl)
{
    if(pl->count<=0) return -1;
    if(pl->repeat==REPEAT_ONE) return pl->current;

    int next;
    if(pl->shuffle) {
        // find current in shuffle order, advance
        int pos=0;
        for(int i=0;i<pl->count;i++)
            if(pl->shuffle_order[i]==pl->current){pos=i;break;}
        pos=(pos+1)%pl->count;
        if(pos==0 && pl->repeat==REPEAT_NONE) return -1;
        next=pl->shuffle_order[pos];
    } else {
        next=pl->current+1;
        if(next>=pl->count) {
            if(pl->repeat==REPEAT_ALL) next=0;
            else return -1;
        }
    }
    pl->current=next;
    return next;
}

int pl_prev(Playlist *pl)
{
    if(pl->count<=0) return -1;
    if(pl->repeat==REPEAT_ONE) return pl->current;
    int prev;
    if(pl->shuffle) {
        int pos=0;
        for(int i=0;i<pl->count;i++)
            if(pl->shuffle_order[i]==pl->current){pos=i;break;}
        pos=(pos-1+pl->count)%pl->count;
        prev=pl->shuffle_order[pos];
    } else {
        prev=pl->current-1;
        if(prev<0) prev=(pl->repeat==REPEAT_ALL)?pl->count-1:0;
    }
    pl->current=prev;
    return prev;
}

int pl_jump(Playlist *pl, int idx)
{
    if(idx<0||idx>=pl->count) return -1;
    pl->current=idx;
    return idx;
}

// ─── Save / Load M3U ──────────────────────────────────────────
bool pl_save(const Playlist *pl, const char *path)
{
    FILE *f=fopen(path,"w");
    if(!f) return false;
    fprintf(f,"#EXTM3U\n");
    for(int i=0;i<pl->count;i++)
        fprintf(f,"#EXTINF:%d,%s\n%s\n",
            pl->items[i].duration_sec,
            pl->items[i].title,
            pl->items[i].path);
    fclose(f);
    return true;
}

bool pl_load(Playlist *pl, const char *path)
{
    FILE *f=fopen(path,"r");
    if(!f) return false;
    pl_clear(pl);
    char line[MAX_PATH]; char title[MAX_FILENAME]="";
    while(fgets(line,sizeof(line),f)) {
        // strip newline
        line[strcspn(line,"\r\n")]=0;
        if(!strncmp(line,"#EXTINF:",8)) {
            char *comma=strchr(line,',');
            if(comma) snprintf(title,MAX_FILENAME,"%s",comma+1);
        } else if(line[0]!='#' && line[0]!='\0') {
            pl_add(pl,line,title[0]?title:line);
            title[0]=0;
        }
    }
    fclose(f);
    return true;
}

// ─── Draw ─────────────────────────────────────────────────────
void pl_draw_top(const Playlist *pl)
{
    Theme *th=current_theme;
    draw_rect(0,0,TOP_WIDTH,26,th->bg_header);
    draw_text(8,5,0.55f,th->text_accent,"3DSoundShell  —  Playlist");

    if(pl->count==0) {
        draw_text(50,110,0.55f,th->text_disabled,"Playlist vide");
        draw_text(30,135,0.45f,th->text_disabled,"X = ajouter fichier   Y = ajouter dossier");
        return;
    }

    // Shuffle / Repeat status
    char status[80];
    const char *rep_str[]={"─","↺1","↺∞"};
    snprintf(status,80,"  %s  %s  %d pistes",
        pl->shuffle?"🔀 Aléatoire":"Ordre normal",
        rep_str[pl->repeat],
        pl->count);
    draw_text(8,30,0.45f,th->text_secondary,status);

    // Current track info
    if(pl->current>=0 && pl->current<pl->count) {
        draw_rect(0,50,TOP_WIDTH,2,th->accent);
        draw_text(10,58,0.52f,th->text_accent,"En lecture :");
        char buf[60]; snprintf(buf,60,"%s",pl->items[pl->current].title);
        draw_text(10,75,0.5f,th->text_primary,buf);
    }
}

void pl_draw_bottom(Playlist *pl)
{
    Theme *th=current_theme;
    int vis=(BOT_HEIGHT-LIST_Y)/ROW_H;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);
    draw_rect(0,0,BOT_WIDTH,LIST_Y,th->bg_header);

    char hdr[40];
    snprintf(hdr,40,"Playlist  [%d/%d]",
        pl->current>=0?pl->current+1:0, pl->count);
    draw_text(6,7,0.5f,th->text_primary,hdr);

    int end=pl->scroll_offset+vis;
    if(end>pl->count) end=pl->count;

    for(int i=pl->scroll_offset;i<end;i++) {
        float y=LIST_Y+(i-pl->scroll_offset)*ROW_H;
        u32 bg;
        if(i==pl->current)   bg=th->bg_playing;
        else if(i==pl->selected) bg=th->bg_selected;
        else bg=th->bg_primary;
        draw_rect(0,y,BOT_WIDTH,ROW_H,bg);
        if(i%2==0&&i!=pl->selected&&i!=pl->current)
            draw_rect(0,y,BOT_WIDTH,ROW_H,RGBA8(0,0,0,12));

        char num[6]; snprintf(num,6,"%d",i+1);
        draw_text(4,y+4,0.4f,th->text_disabled,num);

        char name[36]; snprintf(name,36,"%s",pl->items[i].title);
        u32 col=(i==pl->current)?th->text_accent:th->text_primary;
        draw_text(28,y+4,0.44f,col,name);

        if(i==pl->current)
            draw_text(BOT_WIDTH-18,y+4,0.44f,th->accent,"▶");

        draw_rect(0,y+ROW_H-1,BOT_WIDTH,1,th->border);
    }

    // Scrollbar
    if(pl->count>vis) {
        float sb_h=(float)(BOT_HEIGHT-LIST_Y)/pl->count*vis;
        float sb_y=LIST_Y+(float)pl->scroll_offset/pl->count*(BOT_HEIGHT-LIST_Y);
        draw_rect(BOT_WIDTH-4,LIST_Y,4,BOT_HEIGHT-LIST_Y,th->bg_secondary);
        draw_rect(BOT_WIDTH-4,sb_y,4,sb_h,th->scrollbar);
    }
}
