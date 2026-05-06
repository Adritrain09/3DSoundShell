#include "player_ui.h"
#include "theme.h"
#include "audio.h"
#include <string.h>
#include <math.h>
#include <citro2d.h>

// ─── Draw helpers ─────────────────────────────────────────────
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

static void draw_text_centered(float cx,float y,float sz,u32 c,const char *t)
{
    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(512);
    C2D_TextParse(&tx,tb,t);
    C2D_TextOptimize(&tx);
    float tw=0,th=0;
    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);
    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);
    C2D_TextBufDelete(tb);
}

// ─── Seconds → MM:SS ──────────────────────────────────────────
static void fmt_time(char *out, int sec)
{
    if(sec<0) sec=0;
    snprintf(out,8,"%d:%02d",sec/60,sec%60);
}

// ─── Init ─────────────────────────────────────────────────────
void player_ui_init(PlayerUI *ui)
{
    memset(ui,0,sizeof(PlayerUI));
    ui->viz_style=VIZ_BARS;
    ui->cover_tex_id=-1;
    ui->scroll_title_x=0;
    ui->last_tick=osGetTime();
}

// ─── Update (call every frame) ────────────────────────────────
void player_ui_update(PlayerUI *ui, const AudioMetadata *meta, float dt)
{
    // Fetch visualizer data
    audio_get_visualizer(ui->viz_data);
    // Smooth it
    for(int i=0;i<EQ_BANDS;i++) {
        float spd = (ui->viz_data[i] > ui->viz_smooth[i]) ? 0.3f : 0.08f;
        ui->viz_smooth[i] += (ui->viz_data[i] - ui->viz_smooth[i]) * spd;
    }
    // Scroll long titles
    ui->scroll_title_x -= 0.5f;
    float title_w = strlen(meta->title) * 8.0f * 0.55f;
    if(ui->scroll_title_x < -(title_w)) ui->scroll_title_x = TOP_WIDTH;

    (void)dt;
}

// ─── Visualizer: Bars ─────────────────────────────────────────
static void draw_viz_bars(const PlayerUI *ui, float x, float y,
                          float w, float h)
{
    Theme *th=current_theme;
    int bars=EQ_BANDS;
    float bw=w/bars - 2;
    for(int i=0;i<bars;i++) {
        float bh = ui->viz_smooth[i]*h;
        if(bh<2) bh=2;
        float bx=x+i*(bw+2);
        float by=y+h-bh;
        draw_rect(bx,by,bw,bh,th->visualizer_bars[i]);
        // Reflection (faded)
        draw_rect(bx,y+h,bw,bh*0.25f,RGBA8(
            (th->visualizer_bars[i]>>0)&0xff,
            (th->visualizer_bars[i]>>8)&0xff,
            (th->visualizer_bars[i]>>16)&0xff,
            40));
    }
}

// ─── Visualizer: Waveform ─────────────────────────────────────
static void draw_viz_wave(const PlayerUI *ui, float x, float y,
                          float w, float h)
{
    Theme *th=current_theme;
    float cy=y+h/2.f;
    float step=w/(EQ_BANDS*4);
    for(int i=0;i<EQ_BANDS-1;i++) {
        float ax=x+i*(w/(EQ_BANDS-1));
        float ay=cy - ui->viz_smooth[i]*(h/2.f);
        float bx=x+(i+1)*(w/(EQ_BANDS-1));
        float by=cy - ui->viz_smooth[i+1]*(h/2.f);
        // Draw thick line as small rects
        float dx=bx-ax, dy=by-ay;
        float len=sqrtf(dx*dx+dy*dy);
        int segs=(int)(len/2)+1;
        for(int s=0;s<segs;s++) {
            float t=(float)s/segs;
            float px=ax+dx*t, py=ay+dy*t;
            draw_rect(px-1,py-1,3,3,th->visualizer_bars[i]);
        }
        (void)step;
    }
}

// ─── Visualizer: Circle ───────────────────────────────────────
static void draw_viz_circle(const PlayerUI *ui, float cx, float cy, float r)
{
    Theme *th=current_theme;
    int segs=EQ_BANDS*4;
    for(int i=0;i<segs;i++) {
        float angle=(float)i/segs*2.f*M_PI;
        int band=i*EQ_BANDS/segs;
        float len=r*0.3f + ui->viz_smooth[band]*r*0.7f;
        float x1=cx+cosf(angle)*r*0.3f;
        float y1=cy+sinf(angle)*r*0.3f;
        float x2=cx+cosf(angle)*len;
        float y2=cy+sinf(angle)*len;
        float dx=x2-x1, dy=y2-y1;
        float dist=sqrtf(dx*dx+dy*dy);
        int steps=(int)(dist/2)+1;
        for(int s=0;s<steps;s++) {
            float t=(float)s/steps;
            draw_rect(x1+dx*t-1,y1+dy*t-1,2,2,th->visualizer_bars[band%EQ_BANDS]);
        }
    }
    // Center dot
    draw_rect(cx-3,cy-3,6,6,th->accent);
}

// ─── Fake album art placeholder ───────────────────────────────
static void draw_cover_placeholder(float x,float y,float sz)
{
    Theme *th=current_theme;
    draw_rect(x,y,sz,sz,th->bg_secondary);
    draw_rect(x,y,sz,2,th->border);
    draw_rect(x,y+sz-2,sz,2,th->border);
    draw_rect(x,y,2,sz,th->border);
    draw_rect(x+sz-2,y,2,sz,th->border);
    // Music note symbol
    float cx=x+sz/2.f, cy=y+sz/2.f;
    draw_rect(cx-2,cy-12,4,16,th->text_disabled);
    draw_rect(cx-2,cy-12,10,4,th->text_disabled);
    draw_rect(cx+4,cy-10,4,14,th->text_disabled);
    draw_rect(cx-4,cy+2,8,6,th->text_disabled);
    draw_rect(cx+2,cy+4,8,6,th->text_disabled);
}

// ─── Top screen: full player view ────────────────────────────
void player_ui_draw_top(const PlayerUI *ui, const AudioMetadata *meta,
                        const Playlist *pl)
{
    Theme *th=current_theme;

    // Background gradient simulation (two rects)
    draw_rect(0,0,TOP_WIDTH,TOP_HEIGHT/2,th->bg_primary);
    draw_rect(0,TOP_HEIGHT/2,TOP_WIDTH,TOP_HEIGHT/2,th->bg_secondary);

    // Header
    draw_rect(0,0,TOP_WIDTH,24,th->bg_header);
    draw_text(8,4,0.52f,th->text_accent,"3DSoundShell");

    // State badge
    AudioState st=audio_get_state();
    const char *badge = (st==AUDIO_PLAYING)?"▶ PLAY":
                        (st==AUDIO_PAUSED) ?"⏸ PAUSE":"⏹ STOP";
    draw_text(TOP_WIDTH-70,5,0.48f,
        st==AUDIO_PLAYING?th->accent:th->text_disabled, badge);

    // Shuffle / repeat icons
    if(pl->shuffle) draw_text(TOP_WIDTH-115,5,0.45f,th->accent2,"🔀");
    const char *rep_icon[]={"","↺1","↺∞"};
    if(pl->repeat!=REPEAT_NONE)
        draw_text(TOP_WIDTH-100,5,0.45f,th->accent,rep_icon[pl->repeat]);

    // ─── Album art area (left) ────────────────────────────────
    float art_x=10, art_y=32, art_sz=80;
    if(meta->has_cover && ui->cover_loaded) {
        // In full impl: render C2D_Image from cover texture
        draw_rect(art_x,art_y,art_sz,art_sz,th->bg_secondary);
        draw_text(art_x+10,art_y+30,0.4f,th->text_secondary,"[Cover]");
    } else {
        draw_cover_placeholder(art_x,art_y,art_sz);
    }

    // ─── Track info (right of cover) ─────────────────────────
    float info_x=100;

    // Title (scrolling if long)
    float title_len=strlen(meta->title)*8.f*0.55f;
    float tx = (title_len > 290.f) ? ui->scroll_title_x : info_x;
    draw_text(tx, 34, 0.55f, th->text_primary, meta->title);

    draw_text(info_x, 56, 0.46f, th->text_secondary, meta->artist);
    draw_text(info_x, 72, 0.44f, th->text_disabled,  meta->album);

    if(meta->year>0) {
        char yr[16]; snprintf(yr,16,"(%d)",meta->year);
        draw_text(info_x+180,72,0.44f,th->text_disabled,yr);
    }
    if(meta->genre[0]) draw_text(info_x,88,0.42f,th->text_disabled,meta->genre);

    // Track position in playlist
    if(pl->count>0 && pl->current>=0) {
        char trk[20]; snprintf(trk,20,"Piste %d / %d",pl->current+1,pl->count);
        draw_text(info_x,102,0.42f,th->text_secondary,trk);
    }

    // ─── Progress bar ─────────────────────────────────────────
    float pb_x=10, pb_y=122, pb_w=380, pb_h=8;
    draw_rect(pb_x,pb_y,pb_w,pb_h,th->progress_bg);
    float pct=audio_get_position_pct();
    if(pct>0) draw_rect(pb_x,pb_y,pb_w*pct,pb_h,th->progress_fill);
    // Handle dot
    float dot_x=pb_x+pb_w*pct-4;
    draw_rect(dot_x,pb_y-2,8,12,th->text_primary);

    // Time labels
    char t_cur[8],t_tot[8];
    fmt_time(t_cur,audio_get_position());
    fmt_time(t_tot,audio_get_duration());
    draw_text(pb_x,pb_y+11,0.44f,th->text_secondary,t_cur);
    float tw_tot=strlen(t_tot)*6.f*0.44f;
    draw_text(pb_x+pb_w-tw_tot,pb_y+11,0.44f,th->text_secondary,t_tot);

    // Volume bar
    draw_text(pb_x,pb_y+26,0.42f,th->text_disabled,"Vol");
    float vol=audio_get_volume();
    draw_rect(pb_x+28,pb_y+28,100,5,th->progress_bg);
    draw_rect(pb_x+28,pb_y+28,100*vol,5,th->accent2);

    // ─── Visualizer ───────────────────────────────────────────
    float vx=8, vy=162, vw=384, vh=60;
    draw_rect(vx-2,vy-2,vw+4,vh+4,th->bg_secondary);

    switch(ui->viz_style) {
        case VIZ_BARS:
            draw_viz_bars(ui,vx,vy,vw,vh);
            break;
        case VIZ_WAVE:
            draw_viz_wave(ui,vx,vy,vw,vh);
            break;
        case VIZ_CIRCLE:
            draw_viz_circle(ui,vx+vw/2.f,vy+vh/2.f,vh/2.f-2);
            break;
        default: break;
    }

    // Viz style label
    const char *viz_names[]={"Barres","Onde","Cercle"};
    draw_text(vx+vw-40,vy+vh-12,0.38f,th->text_disabled,
        viz_names[ui->viz_style]);
}

// ─── Bottom screen: controls ──────────────────────────────────
void player_ui_draw_bottom(PlayerUI *ui, const AudioMetadata *meta,
                           const Playlist *pl)
{
    Theme *th=current_theme;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);

    // Header
    draw_rect(0,0,BOT_WIDTH,26,th->bg_header);
    draw_text(8,5,0.52f,th->text_accent,"Contrôles");

    // ─── Control buttons (visual only, physical buttons used) ─
    float by=36;
    // Row 1: prev / play-pause / next
    struct { const char *lbl; float x; u32 col; } btns[]={
        {"⏮ Préc",  14, th->text_secondary},
        {"⏸/▶ L+R", 110, th->accent},
        {"⏭ Suiv",  220, th->text_secondary},
    };
    for(int i=0;i<3;i++) {
        draw_rect(btns[i].x,by,80,20,th->bg_secondary);
        draw_rect(btns[i].x,by,80,20,RGBA8(0,0,0,0));
        draw_text(btns[i].x+4,by+4,0.42f,btns[i].col,btns[i].lbl);
    }
    draw_rect(0,by+24,BOT_WIDTH,1,th->border);

    // Row 2: vol / seek / shuffle
    by+=32;
    draw_text(10,by,0.44f,th->text_secondary,"▲▼ Volume");
    draw_text(110,by,0.44f,th->text_secondary,"ZL/ZR ±10s");
    draw_text(220,by,0.44f,pl->shuffle?th->accent:th->text_disabled,
        pl->shuffle?"🔀 Aléat ON":"🔀 Aléat OFF");

    // Row 3: repeat / viz / eq
    by+=20;
    const char *rep_str[]={"Répét: Non","Répét: ×1","Répét: Tout"};
    draw_text(10,by,0.44f,
        pl->repeat!=REPEAT_NONE?th->accent:th->text_disabled,
        rep_str[pl->repeat]);
    const char *viz_names2[]={"Barres","Onde","Cercle"};
    draw_text(160,by,0.44f,th->text_secondary,viz_names2[ui->viz_style]);

    draw_rect(0,by+18,BOT_WIDTH,1,th->border);

    // Key map legend
    by+=24;
    const char *keys[]={
        "A = Entrer dossier",
        "B = Retour / Fichiers",
        "X = +Playlist",
        "Y = Aléatoire",
        "Select = Explorateur",
        "Start = Lecteur",
        "R = Prochain viz",
        "L+R = Pause/Play",
    };
    for(int i=0;i<8;i++) {
        float kx=(i%2==0)?8:164;
        float ky=by+(i/2)*16;
        draw_text(kx,ky,0.38f,th->text_disabled,keys[i]);
    }

    (void)meta;
}

void player_ui_cycle_viz(PlayerUI *ui)
{
    ui->viz_style=(VisualizerStyle)((ui->viz_style+1)%VIZ_COUNT);
}

void player_ui_load_cover(PlayerUI *ui, const AudioMetadata *meta)
{
    ui->cover_loaded = meta->has_cover && meta->cover_data!=NULL;
    // Full impl: decode JPEG/PNG cover_data → C2D_Image
}
