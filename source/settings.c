#include "settings.h"
#include "theme.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <3ds.h>
#include <citro2d.h>

#define SETTINGS_PATH "sdmc:/3DSoundShell/settings.ini"

Settings g_settings;

/* ── Save thread ─────────────────────────────────────────────── */
static volatile bool s_save_pending  = false;
static volatile bool s_save_running  = false;
static Settings      s_save_copy;
static LightLock     s_save_lock;
static bool          s_lock_init = false;

static void save_thread_func(void *arg)
{
    (void)arg;
    mkdir("sdmc:/3DSoundShell", 0777);
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (f) {
        Settings *s = &s_save_copy;
        fprintf(f, "volume=%.2f\n",    s->volume);
        fprintf(f, "theme=%d\n",       s->theme_index);
        fprintf(f, "viz=%d\n",         (int)s->viz_style);
        fprintf(f, "shuffle=%d\n",     s->shuffle);
        fprintf(f, "repeat=%d\n",      s->repeat);
        fprintf(f, "show_cover=%d\n",  s->show_cover);
        fprintf(f, "resume=%d\n",      s->resume_on_start);
        fprintf(f, "start_dir=%s\n",   s->start_dir);
        fprintf(f, "last_path=%s\n",   s->last_path);
        fprintf(f, "last_track=%d\n",  s->last_track);
        fprintf(f, "eq_preset=%d\n",   s->eq_preset_index);
        for (int i = 0; i < 8; i++)
            fprintf(f, "eq%d=%.2f\n", i, s->eq_gains[i]);
        fclose(f);
    }
    s_save_running = false;
    s_save_pending = false;
}

static void draw_rect(float x,float y,float w,float h,u32 c)
{ C2D_DrawRectSolid(x,y,0,w,h,c); }

static C2D_TextBuf s_st_textbuf = NULL;

static void draw_text(float x,float y,float sz,u32 c,const char *t)
{
    if (!s_st_textbuf) s_st_textbuf = C2D_TextBufNew(4096);
    C2D_TextBufClear(s_st_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx, s_st_textbuf, t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx, C2D_AlignLeft|C2D_WithColor, x, y, 0, sz, sz, c);
}

/* ── Defaults ─────────────────────────────────────────────────── */
void settings_init(void)
{
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.volume           = 0.8f;
    g_settings.resume_on_start  = false;
    g_settings.theme_index      = 0;
    g_settings.viz_style        = VIZ_BARS;
    g_settings.show_cover       = true;
    g_settings.show_spectrum    = true;
    g_settings.shuffle          = false;
    g_settings.repeat           = REPEAT_NONE;
    g_settings.eq_preset_index  = 0;
    for (int i = 0; i < 8; i++) g_settings.eq_gains[i] = 0;
    strncpy(g_settings.start_dir, "sdmc:/Music", 511);

    if (!s_lock_init) {
        LightLock_Init(&s_save_lock);
        s_lock_init = true;
    }
}

/* ── Load ─────────────────────────────────────────────────────── */
void settings_load(void)
{
    settings_init();
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        float fval; int ival; char sval[512];
        if (sscanf(line,"volume=%f",  &fval)==1) { g_settings.volume=fval;             continue; }
        if (sscanf(line,"theme=%d",   &ival)==1) { g_settings.theme_index=ival;        continue; }
        if (sscanf(line,"viz=%d",     &ival)==1) { g_settings.viz_style=(VisualizerStyle)ival; continue; }
        if (sscanf(line,"shuffle=%d", &ival)==1) { g_settings.shuffle=ival;            continue; }
        if (sscanf(line,"repeat=%d",  &ival)==1) { g_settings.repeat=ival;             continue; }
        if (sscanf(line,"show_cover=%d",&ival)==1){ g_settings.show_cover=ival;        continue; }
        if (sscanf(line,"resume=%d",  &ival)==1) { g_settings.resume_on_start=ival;    continue; }
        if(strncmp(line,"start_dir=",10)==0) {
            strncpy(g_settings.start_dir, line+10, 511);
            g_settings.start_dir[511]=0;
            char *nl=strchr(g_settings.start_dir,'\n'); if(nl)*nl=0;
            nl=strchr(g_settings.start_dir,'\r'); if(nl)*nl=0;
            continue; }
        if(strncmp(line,"last_path=",10)==0) {
            strncpy(g_settings.last_path, line+10, 511);
            g_settings.last_path[511]=0;
            char *nl=strchr(g_settings.last_path,'\n'); if(nl)*nl=0;
            nl=strchr(g_settings.last_path,'\r'); if(nl)*nl=0;
            continue; }
        if (sscanf(line,"last_track=%d",&ival)==1){ g_settings.last_track=ival;        continue; }
        if (sscanf(line,"eq_preset=%d",&ival)==1) { g_settings.eq_preset_index=ival;   continue; }
        for (int i = 0; i < 8; i++) {
            char key[8]; snprintf(key, 8, "eq%d=%%f", i);
            if (sscanf(line, key, &fval)==1) { g_settings.eq_gains[i]=fval; break; }
        }
    }
    fclose(f);
}

/* ── Save async (thread detache) ─────────────────────────────── */
void settings_save(void)
{
    /* Si un save tourne deja, on marque pending et on attend */
    if (s_save_running) {
        s_save_pending = true;
        return;
    }

    /* Copie atomique des settings */
    memcpy(&s_save_copy, &g_settings, sizeof(Settings));
    s_save_running = true;
    s_save_pending = false;

    /* Thread detache priorite basse, ne bloque pas le main */
    Thread t = threadCreate(
        save_thread_func,
        NULL,
        8 * 1024,
        0x3F,
        -1,
        true
    );

    if (!t) {
        /* Fallback: save synchrone si thread impossible */
        save_thread_func(NULL);
    }
}

void settings_save_sync(void)
{
    /* Attendre fin du save en cours */
    int timeout = 100;
    while (s_save_running && timeout-- > 0)
        svcSleepThread(10000000LL);

    /* Creer dossier */
    mkdir("sdmc:/3DSoundShell", 0777);

    /* Log debug */
    FILE *dbg = fopen("sdmc:/3DSoundShell/save_debug.log", "w");
    if (dbg) {
        fprintf(dbg, "save_sync called\n");
        fprintf(dbg, "volume=%.2f\n", g_settings.volume);
        fprintf(dbg, "last_path=%s\n", g_settings.last_path);
        fprintf(dbg, "theme=%d\n", g_settings.theme_index);
        fclose(dbg);
    }

    /* Save direct */
    FILE *f = fopen("sdmc:/3DSoundShell/settings.ini", "w");
    if (!f) {
        FILE *err = fopen("sdmc:/3DSoundShell/save_debug.log", "a");
        if (err) { fprintf(err, "ERREUR: fopen settings.ini failed!\n"); fclose(err); }
        return;
    }
    fprintf(f, "volume=%.2f\n", g_settings.volume);
    fprintf(f, "theme=%d\n",    g_settings.theme_index);
    fprintf(f, "viz=%d\n",      (int)g_settings.viz_style);
    fprintf(f, "shuffle=%d\n",  g_settings.shuffle);
    fprintf(f, "repeat=%d\n",   g_settings.repeat);
    fprintf(f, "show_cover=%d\n", g_settings.show_cover);
    fprintf(f, "resume=%d\n",   g_settings.resume_on_start);
    fprintf(f, "start_dir=%s\n", g_settings.start_dir);
    fprintf(f, "last_path=%s\n", g_settings.last_path);
    fprintf(f, "last_track=%d\n", g_settings.last_track);
    fprintf(f, "eq_preset=%d\n", g_settings.eq_preset_index);
    for (int i = 0; i < 8; i++)
        fprintf(f, "eq%d=%.2f\n", i, g_settings.eq_gains[i]);
    fclose(f);

    FILE *ok = fopen("sdmc:/3DSoundShell/save_debug.log", "a");
    if (ok) { fprintf(ok, "save OK!\n"); fclose(ok); }
}

/* ── Settings items ───────────────────────────────────────────── */
#define SITEM_COUNT 10
static const char *sitem_labels[SITEM_COUNT] = {
    "Volume",
    "Theme",
    "Visualiseur",
    "Pochette album",
    "Aleatoire",
    "Repetition",
    "Reprendre au demarrage",
    "Repertoire de depart",
    "EQ Preset",
    "Sauvegarder",
};

static void sitem_value(int idx, char *buf, int bufsz)
{
    switch (idx) {
        case 0: snprintf(buf,bufsz,"%.0f%%", g_settings.volume*100); break;
        case 1: snprintf(buf,bufsz,"%s", theme_name(g_settings.theme_index)); break;
        case 2: { const char *v[]={"Barres","Onde","Cercle"};
                  snprintf(buf,bufsz,"%s",v[g_settings.viz_style]); break; }
        case 3: snprintf(buf,bufsz,"%s", g_settings.show_cover?"ON":"OFF"); break;
        case 4: snprintf(buf,bufsz,"%s", g_settings.shuffle?"ON":"OFF"); break;
        case 5: { const char *r[]={"Non","x1","Tout"};
                  snprintf(buf,bufsz,"%s",r[g_settings.repeat]); break; }
        case 6: snprintf(buf,bufsz,"%s", g_settings.resume_on_start?"ON":"OFF"); break;
        case 7: snprintf(buf,bufsz,"%.20s", g_settings.start_dir); break;
        case 8: {
            EQPreset *presets[] = {
                &eq_preset_flat, &eq_preset_bass_boost,
                &eq_preset_vocal, &eq_preset_rock,
                &eq_preset_classical, &eq_preset_electronic
            };
            int pi = g_settings.eq_preset_index;
            if (pi < 0 || pi > 5) pi = 0;
            snprintf(buf, bufsz, "%s", presets[pi]->name);
            break;
        }
        case 9: snprintf(buf,bufsz,"%s", s_save_running?"Saving...":"OK"); break;
        default: buf[0]=0; break;
    }
}

void settings_draw_top(void)
{
    Theme *th = current_theme;
    draw_rect(0,0,TOP_WIDTH,26,th->bg_header);
    draw_text(8,5,0.55f,th->text_accent,"3DSoundShell - Parametres");
    draw_text(10,40,0.5f,th->text_secondary,"Utilisez << >> pour modifier les valeurs.");
    draw_text(10,58,0.5f,th->text_secondary,"A = confirmer  B = retour  Start = lecteur");
    draw_text(10,90,0.48f,th->text_disabled,"Astuce: L+R=pause  << >>=piste  /\\=volume");
    draw_text(10,108,0.48f,th->text_disabled,"ZL/ZR = reculer/avancer 10s (New3DS)");
    draw_text(10,126,0.48f,th->text_disabled,"R = changer visualiseur");

    /* Indicateur save */
    if (s_save_running) {
        draw_rect(0,TOP_HEIGHT-20,TOP_WIDTH,20,th->accent);
        draw_text(8,TOP_HEIGHT-16,0.45f,th->bg_primary,"Sauvegarde en cours...");
    }
}

void settings_draw_bottom(int *selected_item)
{
    Theme *th = current_theme;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);
    draw_rect(0,0,BOT_WIDTH,26,th->bg_header);
    draw_text(6,5,0.52f,th->text_primary,"Parametres");

    for (int i = 0; i < SITEM_COUNT; i++) {
        float y = 28 + i * 20;
        if (i == *selected_item)
            draw_rect(0,y,BOT_WIDTH,20,th->bg_selected);
        u32 col = (i == *selected_item) ? th->text_primary : th->text_secondary;
        draw_text(6, y+3, 0.44f, col, sitem_labels[i]);
        char vbuf[32] = "";
        sitem_value(i, vbuf, 32);
        draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 6,
                  y+3, 0.44f, th->accent, vbuf);
        draw_rect(0, y+19, BOT_WIDTH, 1, th->border);
    }
}

void settings_handle_input(int *selected_item, u32 keys_down)
{
    if (keys_down & KEY_DOWN) {
        if (*selected_item < SITEM_COUNT-1) (*selected_item)++;
    }
    if (keys_down & KEY_UP) {
        if (*selected_item > 0) (*selected_item)--;
    }

    int idx = *selected_item;

    if (keys_down & KEY_RIGHT) {
        switch (idx) {
            case 0: g_settings.volume+=0.05f;
                    if(g_settings.volume>1) g_settings.volume=1;
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

    if (keys_down & KEY_LEFT) {
        switch (idx) {
            case 0: g_settings.volume-=0.05f;
                    if(g_settings.volume<0) g_settings.volume=0;
                    audio_set_volume(g_settings.volume); break;
            case 1: g_settings.theme_index=(g_settings.theme_index-1+theme_count())%theme_count();
                    theme_set(theme_get(g_settings.theme_index)); break;
            case 2: g_settings.viz_style=(VisualizerStyle)((g_settings.viz_style-1+VIZ_COUNT)%VIZ_COUNT); break;
            case 4: g_settings.shuffle=!g_settings.shuffle; break;
            case 5: g_settings.repeat=(g_settings.repeat+2)%3; break;
            case 8: g_settings.eq_preset_index=(g_settings.eq_preset_index+5)%6; break;
        }
    }

    /* A sur "Sauvegarder" = save manuel */
    if ((keys_down & KEY_A) && idx == SITEM_COUNT-1) {
        settings_save();
    }
}
