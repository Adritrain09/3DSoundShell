// settings.c

#include "settings.h"
#include "theme.h"
#include "power_save.h"
#include "audio.h"
#include "wifi_manager.h"
#include "sleep_timer.h"
#include "playlist.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <3ds.h>
#include <citro2d.h>
#include "main.h"
#include "i18n.h"

#define SETTINGS_PATH "sdmc:/3DSoundShell/settings.ini"

Settings g_settings;

/* V0.96 : retourne la couleur du texte eco selon le preset choisi
   0=Blanc, 1=Rouge, 2=Vert, 3=Bleu, 4=Jaune, 5=Cyan,
   6=Magenta, 7=Orange, 8=Rose, 9=Violet, 10=Theme (accent) */
u32 settings_get_power_save_color(void)
{
    int m = g_settings.power_save_color_mode;
    if (m == 10) {
        /* V0.96 : depuis theme (text_disabled) */
        return current_theme ? current_theme->text_disabled : 0xFFFFFFFF;
    }
    static const u32 preset_colors[10] = {
        0xFFFFFFFF, /* 0 Blanc */
        0xFF0000FF, /* 1 Rouge */
        0xFF00FF00, /* 2 Vert */
        0xFFFF0000, /* 3 Bleu */
        0xFF00FFFF, /* 4 Jaune */
        0xFFFFFF00, /* 5 Cyan */
        0xFFFF00FF, /* 6 Magenta */
        0xFF00A5FF, /* 7 Orange */
        0xFFB4A0FF, /* 8 Rose */
        0xFFFF00A0, /* 9 Violet */
    };
    if (m < 0 || m > 9) m = 0;
    return preset_colors[m];
}


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
        fprintf(f, "speed=%.2f\n",     s->playback_speed);
        fprintf(f, "led=%d\n",          s->led_enabled);
        fprintf(f, "splash_dur=%d\n",   s->splash_duration);
        fprintf(f, "lang=%d\n",         s->language);
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
    g_settings.led_enabled        = true;
    g_settings.splash_duration    = 5;      /* 5 = 2 secondes par defaut */
    g_settings.language           = 0;      /* 0=FR, 1=EN */
    g_settings.power_save_enabled = false;  /* V0.96 : mode eco OFF par defaut */
    g_settings.power_save_delay_sec = 5;    /* 5 secondes par defaut */
    g_settings.viz_enabled          = true;  /* V0.96 : viz ON par defaut */
    g_settings.power_save_text_color = 0xFFFFFFFF;
    g_settings.power_save_r = 255;  /* V0.96 : couleur user detaillee */
    g_settings.power_save_g = 255;
    g_settings.power_save_b = 255;
    g_settings.power_save_color_mode = 0; /* 0=Custom, 1=Theme accent, 2=Theme text */
    g_settings.wifi_off_in_app = false;
    g_settings.sleep_timer_min = 0;  /* V0.97 : sleep timer OFF par defaut */
    g_settings.sleep_shutdown = false;
    g_settings.eco_backlight_mode = 0; /* V0.97 : OFF par defaut */
    g_settings.eco_restore_brightness = 5; /* V0.97 : lum restauree par defaut = 5 */
    g_settings.settings_use_tabs = 0;
    g_settings.pro_speed = 1.0f;        /* V0.97 : vitesse pro neutre */
    g_settings.pitch_semitones = 0;     /* V0.97 : pitch pro neutre */  /* V0.96 : Wi-Fi ON par defaut (eco optionnelle) */      /* 5 = 2 secondes par defaut */   /* LED ON par defaut */
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
        if (sscanf(line,"shuffle=%d",&ival)==1) { g_settings.shuffle=ival;            continue; }
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
        if (sscanf(line,"led=%d",&ival)==1)        { g_settings.led_enabled=ival;       continue; }
        if (sscanf(line,"splash_dur=%d",&ival)==1) { g_settings.splash_duration=ival;   continue; }
        if (sscanf(line,"lang=%d",&ival)==1)       { g_settings.language=ival;          continue; }
        if (sscanf(line,"pwsave=%d",&ival)==1)     { g_settings.power_save_enabled=ival;   continue; }
        if (sscanf(line,"pwsavedelay=%d",&ival)==1){ g_settings.power_save_delay_sec=ival; continue; }
        if (sscanf(line,"vizen=%d",&ival)==1)      { g_settings.viz_enabled=ival;          continue; }
        if (sscanf(line,"pwcolor=%x",(unsigned int*)&ival)==1){ g_settings.power_save_text_color=(u32)ival; continue; }
        if (sscanf(line,"pwr=%d",&ival)==1){ g_settings.power_save_r=(u8)ival; continue; }
        if (sscanf(line,"pwg=%d",&ival)==1){ g_settings.power_save_g=(u8)ival; continue; }
        if (sscanf(line,"pwb=%d",&ival)==1){ g_settings.power_save_b=(u8)ival; continue; }
        if (sscanf(line,"pwmode=%d",&ival)==1){ g_settings.power_save_color_mode=ival; continue; }
        if (sscanf(line,"wifioff=%d",&ival)==1){ g_settings.wifi_off_in_app=ival; continue; }
        if (sscanf(line,"sleep=%d",&ival)==1){ g_settings.sleep_timer_min=ival; continue; }
        if (sscanf(line,"sleepshut=%d",&ival)==1){ g_settings.sleep_shutdown=ival; continue; }
        if (sscanf(line,"ecobackmode=%d",&ival)==1){ g_settings.eco_backlight_mode=ival; continue; }
        if (sscanf(line,"ecorestore=%d",&ival)==1){ g_settings.eco_restore_brightness=ival; continue; }
        if (sscanf(line,"settabs=%d",&ival)==1){ g_settings.settings_use_tabs=ival; continue; }
        if (sscanf(line,"prospeed=%f",&fval)==1){ g_settings.pro_speed=fval; continue; }
        if (sscanf(line,"pitch=%d",&ival)==1){ g_settings.pitch_semitones=ival; continue; }
        for (int i = 0; i < 8; i++) {
            char key[8]; snprintf(key, 8, "eq%d=%%f", i);
            if (sscanf(line, key, &fval)==1) { g_settings.eq_gains[i]=fval; break; }
        }
    }
    fclose(f);
}

/* ── Save async (thread detache) ─────────────────────────────── */
/* === DEBOUNCE : evite les freeze SD sur cartes lentes ===
   Au lieu de save a chaque modif (peut etre 10+ fois/sec en changeant volume),
   on marque comme "dirty" et on save UNE seule fois 3 sec apres le dernier
   changement. Enorme amelioration sur SD 128Go+ lentes. */
static volatile bool s_settings_dirty = false;
static volatile u64  s_dirty_since = 0;
#define SAVE_DEBOUNCE_MS 3000  /* Save 3 sec apres le dernier changement */

void settings_mark_dirty(void)
{
    s_settings_dirty = true;
    s_dirty_since = osGetTime();
}

void settings_update(void)
{
    /* Appelle a chaque frame - save seulement si dirty ET assez de temps ecoule */
    if (!s_settings_dirty) return;

    u64 now = osGetTime();
    if (now - s_dirty_since < SAVE_DEBOUNCE_MS) return;

    /* Assez de temps ecoule sans changement, on save */
    s_settings_dirty = false;
    settings_mark_dirty();
}

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
    

    /* Save direct */
    FILE *f = fopen("sdmc:/3DSoundShell/settings.ini", "w");
    if (!f) {
        
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
    fprintf(f, "led=%d\n", g_settings.led_enabled);
    fprintf(f, "splash_dur=%d\n", g_settings.splash_duration);
    fprintf(f, "lang=%d\n", g_settings.language);
    fprintf(f, "pwsave=%d\n", g_settings.power_save_enabled);
    fprintf(f, "pwsavedelay=%d\n", g_settings.power_save_delay_sec);
    fprintf(f, "vizen=%d\n", g_settings.viz_enabled);
    fprintf(f, "pwcolor=%x\n", (unsigned int)g_settings.power_save_text_color);
    fprintf(f, "pwr=%d\n", g_settings.power_save_r);
    fprintf(f, "pwg=%d\n", g_settings.power_save_g);
    fprintf(f, "pwb=%d\n", g_settings.power_save_b);
    fprintf(f, "pwmode=%d\n", g_settings.power_save_color_mode);
    fprintf(f, "wifioff=%d\n", g_settings.wifi_off_in_app);
    fprintf(f, "sleep=%d\n", g_settings.sleep_timer_min);
    fprintf(f, "sleepshut=%d\n", g_settings.sleep_shutdown);
    fprintf(f, "ecobackmode=%d\n", g_settings.eco_backlight_mode);
    fprintf(f, "ecorestore=%d\n", g_settings.eco_restore_brightness);
    fprintf(f, "settabs=%d\n", g_settings.settings_use_tabs);
    fprintf(f, "prospeed=%.2f\n", g_settings.pro_speed);
    fprintf(f, "pitch=%d\n", g_settings.pitch_semitones);
    for (int i = 0; i < 8; i++)
        fprintf(f, "eq%d=%.2f\n", i, g_settings.eq_gains[i]);
    fclose(f);

    
}

/* ── Settings items ───────────────────────────────────────────── */
#define SITEM_COUNT 24
#define NUM_TABS 5

/* V0.97 : mapping item -> onglet (0=Lecteur, 1=Audio, 2=Affichage, 3=Systeme, 4=Infos) */
static const int sitem_tab[SITEM_COUNT] = {
    0,  /* 0  Volume         → Lecteur */
    2,  /* 1  Theme           → Affichage */
    2,  /* 2  Visualiseur     → Affichage */
    2,  /* 3  Pochette        → Affichage */
    0,  /* 4  Aleatoire       → Lecteur */
    0,  /* 5  Repetition      → Lecteur */
    0,  /* 6  Reprendre       → Lecteur */
    0,  /* 7  Repertoire      → Lecteur */
    1,  /* 8  Equalizer       → Audio */
    1,  /* 9  Vitesse fun     → Audio */
    3,  /* 10 LED             → Systeme */
    2,  /* 11 Splash          → Affichage */
    3,  /* 12 Langue          → Systeme */
    2,  /* 13 Ecran eco       → Affichage */
    2,  /* 14 Delai eco       → Affichage */
    2,  /* 15 Viz ON/OFF      → Affichage */
    2,  /* 16 Couleur texte   → Affichage */
    3,  /* 17 Wifi OFF        → Systeme */
    0,  /* 18 Sleep timer     → Lecteur */
    0,  /* 19 Shutdown        → Lecteur */
    3,  /* 20 Style menu      → Systeme */
    2,  /* 21 Retroeclair eco → Affichage */
    2,  /* 22 Restauration lum → Affichage */
    4,  /* 23 Version         → Infos */
};

/* Nom des onglets */
static const char *tab_names[NUM_TABS] = {
    "Lecteur", "Audio", "Affichage", "Systeme", "Infos"
};

/* Onglet actif (variable statique) */
static int s_active_tab = 0;
static const char *sitem_labels[SITEM_COUNT] = {
    "Volume",
    "Theme",
    "Visualiseur",
    "Pochette album",
    "Aleatoire",
    "Repetition",
    "Reprendre au demarrage",
    "Repertoire de depart",
    "Equalizer audio",
    "Vitesse lecture",
    "LED notification",
    "Duree splash",
    "Langue",
    "Ecran eco energie",
    "Delai ecran eco",
    "Visualiseur ON/OFF",
    "Couleur texte eco",
    "Wi-Fi OFF en app",
    "Sleep timer",
    "Shutdown fin timer",
    "Style menu params",
    "Retroeclairage eco",
    "Lum apres eco",
    "Version",
};

static void sitem_value(int idx, char *buf, int bufsz)
{
    switch (idx) {
        case 0: snprintf(buf,bufsz,"%.0f%%", g_settings.volume*100); break;
        case 1: snprintf(buf,bufsz,"%s", theme_name(g_settings.theme_index)); break;
        case 2: { const char *v[]={T("Barres"),T("Onde"),T("Cercle"),T("Feu"),T("Oscillo"),T("Waves"),T("Tunnel"),T("Pulse"),T("EQ")};
                  snprintf(buf,bufsz,"%s",v[g_settings.viz_style % 9]); break; }
        case 3: snprintf(buf,bufsz,"%s", g_settings.show_cover?T("ON"):T("OFF")); break;
        case 4: snprintf(buf,bufsz,"%s", g_settings.shuffle?T("ON"):T("OFF")); break;
        case 5: { const char *r[]={T("Non"),T("x1"),T("Tout")};
                  snprintf(buf,bufsz,"%s",r[g_settings.repeat]); break; }
        case 6: snprintf(buf,bufsz,"%s", g_settings.resume_on_start?T("ON"):T("OFF")); break;
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
        case 9: {
            float sp = g_settings.playback_speed;
            if (sp <= 0.01f) sp = 1.0f;
            snprintf(buf, bufsz, "%.1fx", sp);
            break;
        }
        case 10: snprintf(buf,bufsz,"%s", g_settings.led_enabled?T("ON"):T("OFF")); break;
        case 11: {
            const char *dur_names[] = {"OFF", "0.2s", "0.5s", "1s", "1.5s", "2s", "2.5s", "3s"};
            int d = g_settings.splash_duration;
            if (d < 0 || d > 7) d = 5;
            snprintf(buf, bufsz, "%s", dur_names[d]);
            break;
        }
        case 12: {
            const char *lang_names[] = {"Francais", "English"};
            int l = g_settings.language;
            if (l < 0 || l > 1) l = 0;
            snprintf(buf, bufsz, "%s", lang_names[l]);
            break;
        }
        case 13: snprintf(buf,bufsz,"%s", g_settings.power_save_enabled?T("ON"):T("OFF")); break;
        case 14: {
            int s = g_settings.power_save_delay_sec;
            if (s < 60) snprintf(buf, bufsz, "%d sec", s);
            else if (s % 60 == 0) snprintf(buf, bufsz, "%d min", s / 60);
            else snprintf(buf, bufsz, "%d min %d sec", s / 60, s % 60);
            break;
        }
        case 15: snprintf(buf,bufsz,"%s", g_settings.viz_enabled?T("ON"):T("OFF")); break;
        case 16: {
            /* V0.96 : couleurs predefinies + Theme a la fin */
            const char *names[] = {"Blanc","Rouge","Vert","Bleu","Jaune","Cyan","Magenta","Orange","Rose","Violet","Theme"};
            int m = g_settings.power_save_color_mode;
            if (m < 0 || m >= 11) m = 0;
            snprintf(buf,bufsz,"%s", names[m]);
            break;
        }
        case 17: snprintf(buf,bufsz,"%s", g_settings.wifi_off_in_app?T("ON"):T("OFF")); break;
        case 18: {
            int m = g_settings.sleep_timer_min;
            if (m == 0) snprintf(buf, bufsz, "OFF");
            else if (m < 60) snprintf(buf, bufsz, "%d min", m);
            else snprintf(buf, bufsz, "%d h %d", m/60, m%60);
            break;
        }
        case 19: snprintf(buf,bufsz,"%s", g_settings.sleep_shutdown?T("ON"):T("OFF")); break;
        case 20: snprintf(buf,bufsz,"%s", g_settings.settings_use_tabs?T("Onglets"):T("Direct")); break;
        case 21: {
            const char *modes[] = {T("OFF"), T("REDUIRE"), T("ETEINDRE")};
            int m = g_settings.eco_backlight_mode;
            if (m < 0 || m > 2) m = 0;
            snprintf(buf, bufsz, "%s", modes[m]);
            break;
        }
        case 22: {
            /* Grise si mode != REDUIRE (car pas utilise) */
            if (g_settings.eco_backlight_mode != 1) {
                snprintf(buf, bufsz, T("Non disponible"));
            } else {
                snprintf(buf, bufsz, "%d/5", g_settings.eco_restore_brightness);
            }
            break;
        }
        case 23: snprintf(buf,bufsz,"V" APP_VERSION); break;

        default: buf[0]=0; break;
    }
}

void settings_draw_top(void)
{
    Theme *th = current_theme;
    draw_rect(0,0,TOP_WIDTH,26,th->bg_header);
    draw_text(8,5,0.55f,th->text_accent,T("3DSoundShell - Parametres"));
    draw_text(10,40,0.5f,th->text_secondary,T("Utilisez < > pour modifier les valeurs."));
    draw_text(10,58,0.5f,th->text_secondary,T("A = confirmer  B = retour  Start = lecteur  L=Equalizer"));
    draw_text(10,72,0.5f,th->text_secondary,T("X = toggle style menu   R = changer onglet"));
    draw_text(10,90,0.48f,th->text_disabled,T("Commande lecteur: L+R = play/pause  < >=piste  ↓↑=volume"));
    draw_text(10,108,0.48f,th->text_disabled,T("ZL/ZR = reculer/avancer 10s (modele New)"));
    draw_text(10,149,0.42f,th->text_disabled,T("Site officiel: https://3DSoundShell.hosten.uk"));
    draw_text(10,166,0.42f,th->text_disabled,T("Credit: Dev app- *Adri / Arena.ai*"));
    draw_text(10,178,0.42f,th->text_disabled,T("        Testeur et gerant site - *Snipergeek*"));

    /* V0.96 : Apercu couleur eco en haut a droite (selon le mode) */
    u32 c = settings_get_power_save_color();
    /* Fond noir */
    draw_rect(TOP_WIDTH - 90, 32, 82, 22, RGBA8(0,0,0,255));
    /* Cadre */
    draw_rect(TOP_WIDTH - 91, 31, 84, 1, th->border);
    draw_rect(TOP_WIDTH - 91, 54, 84, 1, th->border);
    draw_rect(TOP_WIDTH - 91, 32, 1, 22, th->border);
    draw_rect(TOP_WIDTH - 8, 32, 1, 22, th->border);
    /* Texte apercu avec la vraie couleur (custom OU theme) */
    draw_text(TOP_WIDTH - 82, 38, 0.5f, c, "12:34:56");

    /* Notif MAJ permanente en bas gauche */
    if (g_server_unreachable) {
        /* V0.96 : serveur HS mais on affiche aussi la derniere version connue */
        draw_text(8, 217, 0.42f, RGBA8(255,150,50,255), T("Serveur inaccessible"));
        if (g_latest_version[0]) {
            char cache_str[48];
            snprintf(cache_str, 48, T("Derniere version connue : V%s"), g_latest_version);
            draw_text(8, 230, 0.40f, th->text_disabled, cache_str);
        }
    } else if (g_version_ahead) {
        draw_text(8, 225, 0.50f, RGBA8(100,200,255,255), T("Version en avance"));
    } else if (g_update_available) {
        char maj_str[48];
        snprintf(maj_str, 48, T("Mise a jour V%s disponible"), g_latest_version);
        draw_text(8, 225, 0.50f, th->accent2, maj_str);
    } else {
        draw_text(8, 225, 0.50f, th->text_disabled, T("Version a jour."));
    }
    draw_text(10,126,0.48f,th->text_disabled,T("R = changer visualiseur   A = play/Pause"));

    /* Indicateur save */
    if (s_save_running) {
        draw_rect(0,TOP_HEIGHT-20,TOP_WIDTH,20,th->accent);
        draw_text(8,TOP_HEIGHT-16,0.45f,th->bg_primary,T("Sauvegarde en cours..."));
    }
}

void settings_draw_bottom(int *selected_item)
{
    Theme *th = current_theme;
    draw_rect(0,0,BOT_WIDTH,BOT_HEIGHT,th->bg_primary);
    draw_rect(0,0,BOT_WIDTH,26,th->bg_header);
    draw_text(6,5,0.52f,th->text_primary,T("Parametres"));

    /* V0.97 : Mode onglets ou mode direct selon setting */
    bool use_tabs = g_settings.settings_use_tabs != 0;

    int list_y_start = 28;  /* Ou commence la liste des items */

    if (use_tabs) {
        /* V0.97 : Style menu params en premier meme en mode onglets */
        {
            float y_style = 26;
            bool is_sel = (*selected_item == 20);
            u32 bg_style = is_sel ? th->bg_selected : th->bg_secondary;
            draw_rect(0, y_style, BOT_WIDTH, 20, bg_style);
            draw_rect(0, y_style, BOT_WIDTH, 1, th->accent2);
            draw_rect(0, y_style+19, BOT_WIDTH, 1, th->accent2);
            draw_text(6, y_style+3, 0.44f, th->accent2, T("Style menu params"));
            char vbuf[16];
            snprintf(vbuf, 16, "%s (X)", g_settings.settings_use_tabs?T("Onglets"):T("Direct"));
            draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 6,
                      y_style+3, 0.44f, th->accent2, vbuf);
        }

        /* ═══ RENDU DES ONGLETS ═══ */
        float tab_w = BOT_WIDTH / (float)NUM_TABS;
        float tab_y = 46; /* decale en dessous du Style menu */
        float tab_h = 22;

        /* Auto-scroll : si item selectionne pas dans onglet actif → changer d onglet */
        int cur_tab = sitem_tab[*selected_item];
        if (cur_tab != s_active_tab) s_active_tab = cur_tab;

        for (int t = 0; t < NUM_TABS; t++) {
            float tx = t * tab_w;
            u32 bg = (t == s_active_tab) ? th->bg_selected : th->bg_secondary;
            u32 fg = (t == s_active_tab) ? th->text_accent : th->text_secondary;
            draw_rect(tx, tab_y, tab_w, tab_h, bg);
            /* Bordure entre onglets */
            if (t > 0) draw_rect(tx, tab_y, 1, tab_h, th->border);
            /* Nom onglet centre */
            float text_x = tx + (tab_w - strlen(tab_names[t]) * 5.f) / 2.f;
            draw_text(text_x, tab_y + 6, 0.40f, fg, T(tab_names[t]));
        }
        /* Ligne sous les onglets */
        draw_rect(0, tab_y + tab_h, BOT_WIDTH, 1, th->border);

        list_y_start = tab_y + tab_h + 3;

        /* ═══ FILTRER LES ITEMS DE L ONGLET ACTIF ═══ */
        int filtered_items[SITEM_COUNT];
        int filtered_count = 0;
        for (int i = 0; i < SITEM_COUNT; i++) {
            if (sitem_tab[i] == s_active_tab) {
                filtered_items[filtered_count++] = i;
            }
        }

        /* Trouver la position dans la liste filtree */
        int filtered_pos = 0;
        for (int i = 0; i < filtered_count; i++) {
            if (filtered_items[i] == *selected_item) { filtered_pos = i; break; }
        }

        /* Dessiner les items filtres avec scroll */
        int vis = (BOT_HEIGHT - list_y_start - 4) / 20;
        int scroll = 0;
        if (filtered_pos >= vis) scroll = filtered_pos - vis + 1;
        if (scroll + vis > filtered_count) scroll = filtered_count - vis;
        if (scroll < 0) scroll = 0;

        for (int ii = 0; ii < vis && (scroll + ii) < filtered_count; ii++) {
            int i = filtered_items[scroll + ii];
            float y = list_y_start + ii * 20;
            if (i == *selected_item)
                draw_rect(0, y, BOT_WIDTH, 20, th->bg_selected);
            u32 col = (i == *selected_item) ? th->text_primary : th->text_secondary;
            draw_text(6, y+3, 0.44f, col, T(sitem_labels[i]));
            char vbuf[32] = "";
            sitem_value(i, vbuf, 32);
            draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 6,
                      y+3, 0.44f, th->accent, vbuf);
            draw_rect(0, y+19, BOT_WIDTH, 1, th->border);
        }
    } else {
        /* ═══ MODE DIRECT (comme avant) ═══ */
        /* V0.97 : Style menu params en PREMIER avec couleur speciale */
        {
            float y_style = 28;
            bool is_sel = (*selected_item == 20); /* item Style menu = idx 20 */
            u32 bg_style = is_sel ? th->bg_selected : th->bg_secondary;
            draw_rect(0, y_style, BOT_WIDTH, 20, bg_style);
            /* Bordure accent2 (couleur speciale) */
            draw_rect(0, y_style, BOT_WIDTH, 1, th->accent2);
            draw_rect(0, y_style+19, BOT_WIDTH, 1, th->accent2);
            /* Label + valeur */
            draw_text(6, y_style+3, 0.44f, th->accent2, T("Style menu params"));
            char vbuf[16];
            snprintf(vbuf, 16, "%s (X)", g_settings.settings_use_tabs?T("Onglets"):T("Direct"));
            draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 6,
                      y_style+3, 0.44f, th->accent2, vbuf);
        }

        /* Liste des autres items (skip item 20 Style menu) */
        int vis = 9;
        int scroll = 0;
        if (*selected_item >= vis && *selected_item != 20)
            scroll = *selected_item - vis + 1;
        if (scroll + vis > SITEM_COUNT)
            scroll = SITEM_COUNT - vis;
        if (scroll < 0) scroll = 0;

        int y_pos = 50; /* debut liste apres Style menu (28 + 22) */
        int drawn = 0;
        for (int ii = 0; ii < vis && drawn < vis; ii++) {
            int i = scroll + ii;
            if (i >= SITEM_COUNT) break;
            if (i == 20) continue; /* skip Style menu deja affiche */

            float y = y_pos + drawn * 20;
            if (i == *selected_item)
                draw_rect(0, y, BOT_WIDTH, 20, th->bg_selected);
            u32 col = (i == *selected_item) ? th->text_primary : th->text_secondary;
            draw_text(6, y+3, 0.44f, col, T(sitem_labels[i]));
            char vbuf[32] = "";
            sitem_value(i, vbuf, 32);
            draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 6,
                      y+3, 0.44f, th->accent, vbuf);
            draw_rect(0, y+19, BOT_WIDTH, 1, th->border);
            drawn++;
        }
    }
}

void settings_handle_input(int *selected_item, u32 keys_down)
{
    /* V0.97 : X = toggle rapide entre Direct et Onglets */
    if (keys_down & KEY_X) {
        g_settings.settings_use_tabs = !g_settings.settings_use_tabs;
        settings_mark_dirty();
    }

    /* V0.97 : Navigation intelligente si mode onglets */
    if (g_settings.settings_use_tabs) {
        /* Construire la liste filtree de l onglet actif */
        int filtered[SITEM_COUNT];
        int fcount = 0;
        for (int i = 0; i < SITEM_COUNT; i++) {
            if (sitem_tab[i] == s_active_tab) filtered[fcount++] = i;
        }
        /* Trouver position actuelle */
        int fpos = -1;
        for (int i = 0; i < fcount; i++) {
            if (filtered[i] == *selected_item) { fpos = i; break; }
        }
        if (fpos < 0) {
            /* Pas dans onglet actif → aller au premier */
            if (fcount > 0) *selected_item = filtered[0];
        }

        /* UP/DOWN dans l onglet */
        if (keys_down & KEY_DOWN) {
            if (fpos < fcount - 1) *selected_item = filtered[fpos + 1];
        }
        if (keys_down & KEY_UP) {
            if (fpos > 0) *selected_item = filtered[fpos - 1];
        }

        /* V0.97 : R cycle entre les onglets (compatible Old 3DS) */
        if (keys_down & KEY_R) {
            s_active_tab = (s_active_tab + 1) % NUM_TABS;
            for (int i = 0; i < SITEM_COUNT; i++) {
                if (sitem_tab[i] == s_active_tab) { *selected_item = i; break; }
            }
        }
    } else {
        /* Mode direct : UP/DOWN traverse tout */
        if (keys_down & KEY_DOWN) {
            if (*selected_item < SITEM_COUNT-1) (*selected_item)++;
        }
        if (keys_down & KEY_UP) {
            if (*selected_item > 0) (*selected_item)--;
        }
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
            case 4: g_settings.shuffle=!g_settings.shuffle;
                    settings_mark_dirty(); break;
            case 5: g_settings.repeat=(g_settings.repeat+1)%3;
                    settings_mark_dirty(); break;
            case 6: g_settings.resume_on_start=!g_settings.resume_on_start; break;
            case 8: g_settings.eq_preset_index=(g_settings.eq_preset_index+1)%6;
                    { EQPreset *presets[] = {
                        &eq_preset_flat, &eq_preset_bass_boost,
                        &eq_preset_vocal, &eq_preset_rock,
                        &eq_preset_classical, &eq_preset_electronic };
                      audio_eq_apply_preset(presets[g_settings.eq_preset_index]);
                      for(int i=0;i<8;i++) g_settings.eq_gains[i]=audio_eq_get_gain(i); }
                    break;
            case 9: {
                    if (g_settings.playback_speed <= 0.01f)
                        g_settings.playback_speed = 1.0f;
                    g_settings.playback_speed += 0.25f;
                    if (g_settings.playback_speed > 2.0f)
                        g_settings.playback_speed = 2.0f;
                    audio_set_speed(g_settings.playback_speed);
                    settings_mark_dirty();
                    break;
            }
            case 10: g_settings.led_enabled = !g_settings.led_enabled;
                     settings_mark_dirty(); break;
            case 11: g_settings.splash_duration = (g_settings.splash_duration + 1) % 8;
                     settings_mark_dirty(); break;
            case 12: g_settings.language = (g_settings.language + 1) % 2;
                     settings_mark_dirty(); break;
            case 13: g_settings.power_save_enabled = !g_settings.power_save_enabled;
                     settings_mark_dirty(); break;
            case 14: {
                /* Cycle : 5, 10, 15, 30, 45, 60, 90, 120, 180, 300, 420, 600 sec */
                static const int vals[] = {5, 10, 15, 30, 45, 60, 90, 120, 180, 300, 420, 600};
                int cur = g_settings.power_save_delay_sec;
                int idx = 0;
                for (int i = 0; i < 12; i++) if (vals[i] == cur) { idx = i; break; }
                idx = (idx + 1) % 12;
                g_settings.power_save_delay_sec = vals[idx];
                settings_mark_dirty(); break;
            }
            case 15: g_settings.viz_enabled = !g_settings.viz_enabled;
                     settings_mark_dirty(); break;
            case 16: g_settings.power_save_color_mode = (g_settings.power_save_color_mode + 1) % 11;
                     settings_mark_dirty(); break;
            case 17: g_settings.wifi_off_in_app = !g_settings.wifi_off_in_app;
                     settings_mark_dirty();
                     if (g_settings.wifi_off_in_app) wifi_manager_disable();
                     else wifi_manager_enable();
                     break;
            case 18: {
                static const int vals[] = {0, 2, 5, 10, 15, 30, 45, 60, 90, 120};
                int cur = g_settings.sleep_timer_min;
                int idx = 0;
                for (int i = 0; i < 10; i++) if (vals[i] == cur) { idx = i; break; }
                idx = (idx + 1) % 10;
                g_settings.sleep_timer_min = vals[idx];
                sleep_timer_start(g_settings.sleep_timer_min);
                settings_mark_dirty();
                break;
            }
            case 19: g_settings.sleep_shutdown = !g_settings.sleep_shutdown;
                     settings_mark_dirty(); break;
            case 20: g_settings.settings_use_tabs = !g_settings.settings_use_tabs;
                     settings_mark_dirty(); break;
            case 21: g_settings.eco_backlight_mode = (g_settings.eco_backlight_mode + 1) % 3;
                     settings_mark_dirty();
                     if (power_save_is_active()) {
                         power_save_force_restore();
                         power_save_refresh();
                     }
                     break;
            case 22: /* Restauration lum : cycle 1-5, seulement si mode == REDUIRE */
                     if (g_settings.eco_backlight_mode != 1) break;
                     g_settings.eco_restore_brightness++;
                     if (g_settings.eco_restore_brightness > 5) g_settings.eco_restore_brightness = 1;
                     settings_mark_dirty();
                     break;

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
            case 9: {
                    if (g_settings.playback_speed <= 0.01f)
                        g_settings.playback_speed = 1.0f;
                    g_settings.playback_speed -= 0.25f;
                    if (g_settings.playback_speed < 0.5f)
                        g_settings.playback_speed = 0.5f;
                    audio_set_speed(g_settings.playback_speed);
                    settings_mark_dirty();
                    break;
            }
            case 4: g_settings.shuffle=!g_settings.shuffle;
                    settings_mark_dirty(); break;
            case 5: g_settings.repeat=(g_settings.repeat+2)%3;
                    settings_mark_dirty(); break;
            case 8: g_settings.eq_preset_index=(g_settings.eq_preset_index+5)%6;
                    { EQPreset *presets[] = {
                        &eq_preset_flat, &eq_preset_bass_boost,
                        &eq_preset_vocal, &eq_preset_rock,
                        &eq_preset_classical, &eq_preset_electronic };
                      audio_eq_apply_preset(presets[g_settings.eq_preset_index]);
                      for(int i=0;i<8;i++) g_settings.eq_gains[i]=audio_eq_get_gain(i); }
                    break;
            case 10: g_settings.led_enabled = !g_settings.led_enabled;
                     settings_mark_dirty(); break;
            case 11: g_settings.splash_duration = (g_settings.splash_duration + 7) % 8;
                     settings_mark_dirty(); break;
            case 12: g_settings.language = (g_settings.language + 1) % 2;
                     settings_mark_dirty(); break;
            case 13: g_settings.power_save_enabled = !g_settings.power_save_enabled;
                     settings_mark_dirty(); break;
            case 14: {
                static const int vals[] = {5, 10, 15, 30, 45, 60, 90, 120, 180, 300, 420, 600};
                int cur = g_settings.power_save_delay_sec;
                int idx = 0;
                for (int i = 0; i < 12; i++) if (vals[i] == cur) { idx = i; break; }
                idx = (idx + 11) % 12;
                g_settings.power_save_delay_sec = vals[idx];
                settings_mark_dirty(); break;
            }
            case 15: g_settings.viz_enabled = !g_settings.viz_enabled;
                     settings_mark_dirty(); break;
            case 16: g_settings.power_save_color_mode = (g_settings.power_save_color_mode + 10) % 11;
                     settings_mark_dirty(); break;
            case 17: g_settings.wifi_off_in_app = !g_settings.wifi_off_in_app;
                     settings_mark_dirty();
                     if (g_settings.wifi_off_in_app) wifi_manager_disable();
                     else wifi_manager_enable();
                     break;
            case 18: {
                static const int vals[] = {0, 2, 5, 10, 15, 30, 45, 60, 90, 120};
                int cur = g_settings.sleep_timer_min;
                int idx = 0;
                for (int i = 0; i < 10; i++) if (vals[i] == cur) { idx = i; break; }
                idx = (idx + 9) % 10;
                g_settings.sleep_timer_min = vals[idx];
                sleep_timer_start(g_settings.sleep_timer_min);
                settings_mark_dirty();
                break;
            }
            case 19: g_settings.sleep_shutdown = !g_settings.sleep_shutdown;
                     settings_mark_dirty(); break;
            case 20: g_settings.settings_use_tabs = !g_settings.settings_use_tabs;
                     settings_mark_dirty(); break;
            case 21: g_settings.eco_backlight_mode = (g_settings.eco_backlight_mode + 2) % 3;
                     settings_mark_dirty();
                     if (power_save_is_active()) {
                         power_save_force_restore();
                         power_save_refresh();
                     }
                     break;
            case 22: /* Restauration lum : cycle 1-5, seulement si mode == REDUIRE */
                     if (g_settings.eco_backlight_mode != 1) break;
                     g_settings.eco_restore_brightness--;
                     if (g_settings.eco_restore_brightness < 1) g_settings.eco_restore_brightness = 5;
                     settings_mark_dirty();
                     break;

        }
    }

    /* A sur "Sauvegarder" = save manuel */
    if ((keys_down & KEY_A) && idx == SITEM_COUNT-1) {
        settings_mark_dirty();
    }
}
