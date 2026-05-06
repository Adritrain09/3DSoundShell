#include "settings.h"
#include "theme.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <citro2d.h>

#define SETTINGS_PATH "sdmc:/3DSoundShell/settings.ini"

Settings g_settings;

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

// ─── Defaults ─────────────────────────────────────────────────
void settings_init(void)
{
    memset(&g_settings,0,sizeof(g_settings));
    g_settings.volume        = 0.8f;
    g_settings.resume_on_start=false;
    g_settings.theme_index   = 0;
    g_settings.viz_style     = VIZ_BARS;
    g_settings.show_cover    = true;
    g_settings.show_spectrum  = true;
    g_settings.shuffle       = false;
    g_settings.repeat        = REPEAT_NONE;
    g_settings.eq_preset_index=0;
    for(int i=0;i<8;i++) g_settings.eq_gains[i]=0;
    strncpy(g_settings.start_dir,"sdmc:/Music",511);
}

// ─── Load from INI ────────────────────────────────────────────
void settings_load(void)
{
    settings_init();
    FILE *f=fopen(SETTINGS_PATH,"r");
    if(!f) return;
    char line[256];
    while(fgets(line,sizeof(line),f)) {
        line[strcspn(line,"\r\n")]=0;
        float fval; int ival; char sval[512];
        if(sscanf(line,"volume=%f",&fval)==1)       { g_settings.volume=fval; continue; }
        if(sscanf(line,"theme=%d",&ival)==1)         { g_settings.theme_index=ival; continue; }
        if(sscanf(line,"viz=%d",&ival)==1)            { g_settings.viz_style=(VisualizerStyle)ival; continue; }
        if(sscanf(line,"shuffle=%d",&ival)==1)        { g_settings.shuffle=ival; continue; }
        if(sscanf(line,"repeat=%d",&ival)==1)         { g_settings.repeat=ival; continue; }
        if(sscanf(line,"show_cover=%d",&ival)==1)     { g_settings.show_cover=ival; continue; }
        if(sscanf(line,"resume=%d",&ival)==1)         { g_settings.resume_on_start=ival; continue; }
        if(sscanf(line,"start_dir=%511s",sval)==1)    { strncpy(g_settings.start_dir,sval,511); continue; }
        if(sscanf(line,"last_path=%511s",sval)==1)    { strncpy(g_settings.last_path,sval,511); continue; }
        if(sscanf(line,"last_track=%d",&ival)==1)     { g_settings.last_track=ival; continue; }
        if(sscanf(line,"eq_preset=%d",&ival)==1)      { g_settings.eq_preset_index=ival; continue; }
        // EQ bands: eq0= ... eq7=
        for(int i=0;i<8;i++) {
            char key[8]; snprintf(key,8,"eq%d=%%f",i);
            if(sscanf(line,key,&fval)==1) { g_settings.eq_gains[i]=fval; break; }
        }
    }
    fclose(f);
}

// ─── Save to INI ──────────────────────────────────────────────
void settings_save(void)
{
    // Create dir if needed
    mkdir("sdmc:/3DSoundShell",0777);
    FILE *f=fopen(SETTINGS_PATH,"w");
    if(!f) return;
    fprintf(f,"volume=%.2f\n",    g_settings.volume);
    fprintf(f,"theme=%d\n",       g_settings.theme_index);
    fprintf(f,"viz=%d\n",         (int)g_settings.viz_style);
    fprintf(f,"shuffle=%d\n",     g_settings.shuffle);
    fprintf(f,"repeat=%d\n",      g_settings.repeat);
    fprintf(f,"show_cover=%d\n",  g_settings.show_cover);
    fprintf(f,"resume=%d\n",      g_settings.resume_on_start);
    fprintf(f,"start_dir=%s\n",   g_settings.start_dir);
    fprintf(f,"last_path=%s\n",   g_settings.last_path);
    fprintf(f,"last_track=%d\n",  g_settings.last_track);
    fprintf(f,"eq_preset=%d\n",   g_settings.eq_preset_index);
    for(int i=0;i<8;i++)
        fprintf(f,"eq%d=%.2f\n",  i,g_settings.eq_gains[i]);
    fclose(f);
}

// ─── Settings items list ──────────────────────────────────────
#define SITEM_COUNT 10
static const char *sitem_labels[SITEM_COUNT]={
    "Volume",
    "Thème",
    "Visualiseur",
    "Pochette album",
    "Aléatoire",
    "Répétition",
    "Reprendre au démarrage",
    "Répertoire de départ",
    "EQ Préset",
    "Sauvegarder & Quitter",
};

static char sitem_value(int idx, char *buf, int bufsz)
{
    switch(idx) {
        case 0: snprintf(buf,bufsz,"%.0f%%",g_settings.volume*100); break;
        case 1: snprintf(buf,bufsz,"%s",theme_name(g_settings.theme_index)); break;
        case 2: { const char*v[]={"Barres","Onde","Cercle"};
                  snprintf(buf,bufsz,"%s",v[g_settings.viz_style]); break; }
        case 3: snprintf(buf,bufsz,"%s",g_settings.show_cover?"ON":"OFF"); break;
        case 4: snprintf(buf,bufsz,"%s",g_settings.shuffle?"ON":"OFF"); break;
        case 5: { const char*r[]={"Non","×1","Tout"};
                  snprintf(buf,bufsz,"%s",r[g_settings.repeat]); break; }
        case 6: snprintf(buf,bufsz,"%s",g_settings.resume_on_start?"ON":"OFF"); break;
        case 7: snprintf(buf,bufsz,"%.20s",g_settings.start_dir); break;
        case 8: {
            EQPreset *presets[]={&eq_preset_flat,&eq_preset_bass_boost,
                &eq_preset_vocal,&eq_preset_rock,&eq_preset_classical,
                &eq_preset_electronic};
            int pi=g_settings.eq_preset_index;
            if(pi<0||pi>5) pi=0;
            snprintf(buf,bufsz,"%s",presets[pi]->name); break;
        }
        case 9: snprintf(buf,bufsz,""); break;
    }
    return 0;
}

void settings_draw_top(void)
{
    Theme *th=current_theme;
    draw_rect(0,0,TOP_WIDTH,26,th->bg_header);
    draw_text(8,5,0.55f,th->text_accent,"3DSoundShell  —  Paramètres");
    draw_text(10,40,0.5f,th->text_secondary,"Utilisez ◀▶ pour modifier les valeurs.");
    draw_text(10,58,0.5f,th->text_secondary,"A = confirmer   B = retour   Start = lecteur");
    // EQ hint
    draw_text(10,90,0.48f,th->text_disabled,"Astuce : L+R = pause, ◀▶ = piste, ▲▼ = volume");
    draw_text(10,108,0.48f,th->text_disabled,"ZL/ZR = reculer/avancer 10s (New3DS)");
    draw_text(10,126,0.48f,th->text_disabled,"R = changer visualiseur");
}

void settings_draw_bottom(int *selected_item)
{
    Theme *th=current_theme;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);
    draw_rect(0,0,BOT_WIDTH,26,th->bg_header);
    draw_text(6,5,0.52f,th->text_primary,"Paramètres");

    for(int i=0;i<SITEM_COUNT;i++) {
        float y=28+i*20;
        if(i==*selected_item) draw_rect(0,y,BOT_WIDTH,20,th->bg_selected);
        u32 col=(i==*selected_item)?th->text_primary:th->text_secondary;
        draw_text(6,y+3,0.44f,col,sitem_labels[i]);
        char vbuf[32]="";
        sitem_value(i,vbuf,32);
        draw_text(BOT_WIDTH-strlen(vbuf)*5.5f-6,y+3,0.44f,th->accent,vbuf);
        draw_rect(0,y+19,BOT_WIDTH,1,th->border);
    }
}

void settings_handle_input(int *selected_item, u32 keys_down)
{
    if(keys_down & KEY_DOWN) {
        if(*selected_item<SITEM_COUNT-1) (*selected_item)++;
    }
    if(keys_down & KEY_UP) {
        if(*selected_item>0) (*selected_item)--;
    }
    int idx=*selected_item;
    if(keys_down & KEY_RIGHT) {
        switch(idx) {
            case 0: g_settings.volume+=0.05f; if(g_settings.volume>1)g_settings.volume=1;
                    audio_set_volume(g_settings.volume); break;
            case 1: g_settings.theme_index=(g_settings.theme_index+1)%theme_count();
                    theme_set(theme_get(g_settings.theme_index)); break;
            case 2: g_settings.viz_style=(VisualizerStyle)((g_settings.viz_style+1)%VIZ_COUNT); break;
            case 3: g_settings.show_cover=!g_settings.show_cover; break;
            case 4: g_settings.shuffle=!g_settings.shuffle; break;
            case 5: g_settings.repeat=(g_settings.repeat+1)%3; break;
            case 6: g_settings.resume_on_start=!g_settings.resume_on_start; break;
            case 8: g_settings.eq_preset_index=(g_settings.eq_preset_index+1)%6; break;
        }
    }
    if(keys_down & KEY_LEFT) {
        switch(idx) {
            case 0: g_settings.volume-=0.05f; if(g_settings.volume<0)g_settings.volume=0;
                    audio_set_volume(g_settings.volume); break;
            case 1: g_settings.theme_index=(g_settings.theme_index-1+theme_count())%theme_count();
                    theme_set(theme_get(g_settings.theme_index)); break;
            case 2: g_settings.viz_style=(VisualizerStyle)((g_settings.viz_style-1+VIZ_COUNT)%VIZ_COUNT); break;
            case 4: g_settings.shuffle=!g_settings.shuffle; break;
            case 5: g_settings.repeat=(g_settings.repeat+2)%3; break;
            case 8: g_settings.eq_preset_index=(g_settings.eq_preset_index+5)%6; break;
        }
    }
    if((keys_down & KEY_A) && idx==SITEM_COUNT-1) settings_save();
}
