#include "audio.h"
#include "main.h"
#include "theme.h"
#include <citro2d.h>

/* ── TextBuf global ── */

#include <string.h>
#include <stdio.h>

#define EQ_SCREEN_BANDS 8

static const char *band_labels[EQ_SCREEN_BANDS]=
    {"32","64","125","250","500","1k","2k","8k"};

static void draw_rect(float x,float y,float w,float h,u32 c)
{ C2D_DrawRectSolid(x,y,0,w,h,c); }
static void draw_text(float x,float y,float sz,u32 c,const char *t)
{
    if(!t||!t[0]) return;
    g_textbuf_init();
    C2D_TextBufClear(g_textbuf);
    C2D_TextBufClear(g_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,g_textbuf,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
}

void eq_draw_top(int selected_band)
{
    Theme *th=current_theme;
    draw_rect(0,0,TOP_WIDTH,TOP_HEIGHT,th->bg_primary);
    draw_rect(0,0,TOP_WIDTH,26,th->bg_header);
    draw_text(8,5,0.55f,th->text_accent,"3DSoundShell - Egaliseur");

    float bar_w=34, gap=12;
    float total=EQ_SCREEN_BANDS*(bar_w+gap)-gap;
    float start_x=(TOP_WIDTH-total)/2.f;
    float bar_area_h=130;
    float bar_y_base=160;
    float db_scale=bar_area_h/24.f;

    /* Fond EQ */
    draw_rect(start_x-5, bar_y_base-bar_area_h/2.f-5,
              total+10, bar_area_h+10, th->eq_bg);

    /* Ligne zero */
    draw_rect(start_x-5, bar_y_base-bar_area_h/2.f, total+10, 1, th->eq_zero_line);
    draw_text(start_x-30, bar_y_base-bar_area_h/2.f-6, 0.38f, th->text_disabled,"+12");
    draw_text(start_x-30, bar_y_base-6,                0.38f, th->text_disabled,"  0");
    draw_text(start_x-30, bar_y_base+bar_area_h/2.f-6, 0.38f, th->text_disabled,"-12");

    for(int i=0;i<EQ_SCREEN_BANDS;i++) {
        float x=start_x+i*(bar_w+gap);
        float gain=audio_eq_get_gain(i);
        float bh=gain*db_scale;
        bool positive=(gain>=0);
        float by=(bh>=0)? bar_y_base-bar_area_h/2.f+(bar_area_h/2.f-bh)
                        : bar_y_base-bar_area_h/2.f+bar_area_h/2.f;
        if(bh<0){by=bar_y_base-bar_area_h/2.f+bar_area_h/2.f;bh=-bh;}

        /* Couleur selon etat */
        u32 bar_col;
        if(i==selected_band)
            bar_col=th->eq_bar_selected;
        else if(positive)
            bar_col=th->eq_bar_positive;
        else
            bar_col=th->eq_bar_negative;

        /* Attenue si pas selectionne */
        if(i!=selected_band){
            u8 r=(bar_col>>0)&0xff;
            u8 g2=(bar_col>>8)&0xff;
            u8 b2=(bar_col>>16)&0xff;
            bar_col=RGBA8(r*3/4,g2*3/4,b2*3/4,220);
        }

        /* Barre */
        draw_rect(x,by,bar_w,bh<2?2:bh,bar_col);

        /* Reflet */
        if(bh>3){
            u8 r=(bar_col>>0)&0xff;
            u8 g2=(bar_col>>8)&0xff;
            u8 b2=(bar_col>>16)&0xff;
            draw_rect(x,by+bh,bar_w,bh*0.12f,RGBA8(r,g2,b2,50));
        }

        /* Handle */
        if(i==selected_band)
            draw_rect(x-2,by-4,bar_w+4,8,th->eq_handle);

        /* Label */
        draw_text(x+2,bar_y_base+bar_area_h/2.f+4,0.38f,
            i==selected_band?th->text_accent:th->text_disabled,
            band_labels[i]);

        /* dB */
        char db[8]; snprintf(db,8,"%.1f",audio_eq_get_gain(i));
        draw_text(x+2,by-14,0.38f,
            i==selected_band?th->text_accent:th->text_secondary,db);
    }
}

void eq_draw_bottom(int selected_band)
{
    Theme *th=current_theme;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);
    draw_rect(0,0,BOT_WIDTH,26,th->bg_header);
    draw_text(6,5,0.52f,th->text_primary,"Égaliseur");

    draw_text(8,32,0.46f,th->text_secondary,"◀▶ Changer bande");
    draw_text(8,50,0.46f,th->text_secondary,"▲▼ Modifier gain (±1dB)");
    draw_text(8,68,0.46f,th->text_secondary,"A  = Appliquer préset");
    draw_text(8,86,0.46f,th->text_secondary,"B  = Retour");
    draw_text(8,104,0.46f,th->text_secondary,"X  = Tout à zéro");

    char info[40];
    snprintf(info,40,"Bande sélect.: %sHz  Gain: %.1fdB",
        band_labels[selected_band],
        audio_eq_get_gain(selected_band));
    draw_rect(0,BOT_HEIGHT-28,BOT_WIDTH,28,th->bg_secondary);
    draw_text(6,BOT_HEIGHT-22,0.46f,th->text_accent,info);
}

void eq_handle_input(int *selected_band, u32 keys_down)
{
    if(keys_down & KEY_RIGHT) *selected_band=(*selected_band+1)%EQ_SCREEN_BANDS;
    if(keys_down & KEY_LEFT)  *selected_band=(*selected_band-1+EQ_SCREEN_BANDS)%EQ_SCREEN_BANDS;
    if(keys_down & KEY_UP) {
        float g=audio_eq_get_gain(*selected_band)+1.0f;
        if(g>12) g=12;
        audio_eq_set_gain(*selected_band,g);
    }
    if(keys_down & KEY_DOWN) {
        float g=audio_eq_get_gain(*selected_band)-1.0f;
        if(g<-12) g=-12;
        audio_eq_set_gain(*selected_band,g);
    }
    if(keys_down & KEY_X) {
        for(int i=0;i<EQ_SCREEN_BANDS;i++) audio_eq_set_gain(i,0);
    }
}

