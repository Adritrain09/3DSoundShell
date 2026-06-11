// equalizer.c

#include "audio.h"
#include "main.h"
#include "settings.h"
#include "theme.h"
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ── TextBuf global ── */

#define EQ_SCREEN_BANDS 8

static const char *band_labels[EQ_SCREEN_BANDS]=
    {"32","64","125","250","500","1k","2k","8k"};

/* Smooth visualiseur local */
static float s_viz_smooth[EQ_SCREEN_BANDS] = {0};

static void draw_rect(float x,float y,float w,float h,u32 c)
{ C2D_DrawRectSolid(x,y,0,w,h,c); }

static void draw_text(float x,float y,float sz,u32 c,const char *t)
{
    if(!t||!t[0]) return;
    g_textbuf_init();
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

    /* ── Parametres barres EQ ── */
    float bar_w=34, gap=12;
    float total=EQ_SCREEN_BANDS*(bar_w+gap)-gap;
    float start_x=(TOP_WIDTH-total)/2.f;
    float bar_area_h=100;
    float bar_y_base=145;
    float db_scale=bar_area_h/24.f;

    /* Fond EQ */
    draw_rect(start_x-5, bar_y_base-bar_area_h/2.f-5,
              total+10, bar_area_h+10, th->eq_bg);

    /* Lignes de grille dB */
    u32 grid_col = RGBA8(255,255,255,20);
    for(int db = -12; db <= 12; db += 6) {
        float gy = bar_y_base - (float)db * db_scale;
        draw_rect(start_x-5, gy, total+10, 1, grid_col);
    }

    /* Ligne zero plus visible */
    draw_rect(start_x-5, bar_y_base, total+10, 1, th->eq_zero_line);

    /* Labels dB a gauche */
    draw_text(start_x-30, bar_y_base-bar_area_h/2.f-6, 0.38f, th->text_disabled,"+12");
    draw_text(start_x-30, bar_y_base-bar_area_h/4.f-6, 0.38f, th->text_disabled," +6");
    draw_text(start_x-30, bar_y_base-6,                0.38f, th->text_disabled,"  0");
    draw_text(start_x-30, bar_y_base+bar_area_h/4.f-6, 0.38f, th->text_disabled," -6");
    draw_text(start_x-30, bar_y_base+bar_area_h/2.f-6, 0.38f, th->text_disabled,"-12");

    /* ── Visualiseur en temps reel sous les barres EQ ── */
    float viz_raw[EQ_SCREEN_BANDS];
    audio_get_visualizer(viz_raw);

    /* Smooth viz */
    for(int i=0;i<EQ_SCREEN_BANDS;i++) {
        float spd = (viz_raw[i] > s_viz_smooth[i]) ? 0.35f : 0.07f;
        s_viz_smooth[i] += (viz_raw[i] - s_viz_smooth[i]) * spd;
    }

    float viz_area_h = 35.f;
    float viz_y_base = bar_y_base + bar_area_h/2.f + 14.f;

    /* Fond visualiseur */
    draw_rect(start_x-5, viz_y_base, total+10, viz_area_h+2, RGBA8(0,0,0,60));

    for(int i=0;i<EQ_SCREEN_BANDS;i++) {
        float x = start_x + i*(bar_w+gap);
        float vh = s_viz_smooth[i] * viz_area_h;
        if(vh < 2) vh = 2;

        /* Couleur par bande - degrade chaud */
        u32 vcol;
        if(i < 2)      vcol = RGBA8(80, 120, 255, 200);   /* graves - bleu */
        else if(i < 4) vcol = RGBA8(80, 220, 120, 200);   /* mid-bas - vert */
        else if(i < 6) vcol = RGBA8(255, 200, 50, 200);   /* mid-haut - jaune */
        else           vcol = RGBA8(255, 80, 80, 200);    /* aigus - rouge */

        /* Barre viz qui monte depuis le bas */
        draw_rect(x, viz_y_base + viz_area_h - vh, bar_w, vh, vcol);

        /* Reflet */
        u8 r=(vcol>>0)&0xff, g2=(vcol>>8)&0xff, b2=(vcol>>16)&0xff;
        draw_rect(x, viz_y_base + viz_area_h - vh - 3, bar_w, 3,
                  RGBA8(r, g2, b2, 80));

        /* Peak indicator - petit trait en haut */
        draw_rect(x, viz_y_base + viz_area_h - vh, bar_w, 2,
                  RGBA8(255,255,255,180));
    }

    /* Label visualiseur */
    draw_text(start_x-5, viz_y_base - 12, 0.36f, th->text_disabled, "NIVEAU");

    /* ── Barres EQ ── */
    for(int i=0;i<EQ_SCREEN_BANDS;i++) {
        float x=start_x+i*(bar_w+gap);
        float gain=audio_eq_get_gain(i);
        float bh=gain*db_scale;
        float by;
        if(bh >= 0) {
            by = bar_y_base - bh;
        } else {
            by = bar_y_base;
            bh = -bh;
        }

        /* Couleur barre EQ */
        u32 bar_col;
        if(i==selected_band)
            bar_col=th->eq_bar_selected;
        else if(gain>=0)
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

        /* Barre EQ */
        draw_rect(x, by, bar_w, bh<2?2:bh, bar_col);

        /* Reflet barre EQ */
        if(bh>3){
            u8 r=(bar_col>>0)&0xff;
            u8 g2=(bar_col>>8)&0xff;
            u8 b2=(bar_col>>16)&0xff;
            draw_rect(x, by+bh, bar_w, bh*0.12f, RGBA8(r,g2,b2,50));
        }

        /* Handle barre selectionnee */
        if(i==selected_band)
            draw_rect(x-2, by-4, bar_w+4, 8, th->eq_handle);

        /* Label frequence */
        draw_text(x+2, bar_y_base+bar_area_h/2.f+4, 0.38f,
            i==selected_band?th->text_accent:th->text_disabled,
            band_labels[i]);

        /* Valeur dB au dessus/dessous */
        char db[8];
        snprintf(db,8,"%.1f",audio_eq_get_gain(i));
        float db_y = (gain >= 0) ? by-14 : by+bh+2;
        draw_text(x+2, db_y, 0.38f,
            i==selected_band?th->text_accent:th->text_secondary, db);
    }
}

void eq_draw_bottom(int selected_band)
{
    Theme *th=current_theme;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);
    draw_rect(0,0,BOT_WIDTH,26,th->bg_header);
    draw_text(6,5,0.52f,th->text_primary,"Egaliseur");

    /* Info bande selectionnee */
    draw_rect(0,28,BOT_WIDTH,30,th->bg_secondary);
    char info[48];
    float gain = audio_eq_get_gain(selected_band);
    snprintf(info,48,"Bande: %sHz   Gain: %+.1f dB",
        band_labels[selected_band], gain);
    draw_text(6,34,0.48f,th->text_accent,info);

    /* Barre de gain visuelle en bas de l'info */
    float bar_total = BOT_WIDTH - 20;
    float bar_x = 10;
    float bar_y = 54;
    float bar_h = 6;
    /* fond */
    draw_rect(bar_x, bar_y, bar_total, bar_h, RGBA8(60,60,60,255));
    /* centre */
    draw_rect(bar_x + bar_total/2.f - 1, bar_y-2, 2, bar_h+4, RGBA8(255,255,255,100));
    /* barre gain */
    float pct = (gain + 12.f) / 24.f;
    float fill_w = pct * bar_total;
    u32 gain_col = (gain >= 0) ? th->eq_bar_positive : th->eq_bar_negative;
    if(gain >= 0)
        draw_rect(bar_x + bar_total/2.f, bar_y, fill_w - bar_total/2.f, bar_h, gain_col);
    else
        draw_rect(bar_x + fill_w, bar_y, bar_total/2.f - fill_w, bar_h, gain_col);

    /* Controles */
    draw_text(8, 70,0.44f,th->text_secondary, "< >  Changer bande");
    draw_text(8, 86,0.44f,th->text_secondary, "v ^  Gain -1 / +1 dB");
    draw_text(8,102,0.44f,th->text_secondary, "A    Preset suivant");
    draw_text(8,118,0.44f,th->text_secondary, "X    Tout a zero");
    draw_text(8,134,0.44f,th->text_secondary, "B    Retour");

    /* Preset actif */
    extern EQPreset eq_preset_flat, eq_preset_bass_boost,
                    eq_preset_vocal, eq_preset_rock,
                    eq_preset_classical, eq_preset_electronic;
    const char *preset_names[] = {
        "Flat","Bass Boost","Vocal","Rock","Classical","Electronic"
    };
    draw_rect(0, BOT_HEIGHT-28, BOT_WIDTH, 28, th->bg_secondary);
    char preset_str[32];
    snprintf(preset_str, 32, "Preset: %s",
        preset_names[g_settings.eq_preset_index % 6]);
    draw_text(6, BOT_HEIGHT-22, 0.46f, th->accent, preset_str);
}

void eq_handle_input(int *selected_band, u32 keys_down)
{
    if(keys_down & KEY_RIGHT)
        *selected_band=(*selected_band+1)%EQ_SCREEN_BANDS;
    if(keys_down & KEY_LEFT)
        *selected_band=(*selected_band-1+EQ_SCREEN_BANDS)%EQ_SCREEN_BANDS;
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
        for(int i=0;i<EQ_SCREEN_BANDS;i++)
            audio_eq_set_gain(i,0.f);
    }
}
