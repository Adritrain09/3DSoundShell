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
#include "app_mutex.h"

#define SETTINGS_PATH "sdmc:/3DSoundShell/settings.ini"

Settings g_settings;
static u64 s_last_settings_input_time = 0;
static int s_current_selected_item = 0;

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

/* Ecriture complete de settings.ini : utilisee par le save async (debounce)
   ET par settings_save_sync(), pour ne jamais avoir un des deux chemins qui
   ecrit moins de champs que l'autre (des reglages sauves uniquement via le
   chemin async, ex Wi-Fi off / alarme / veille, se perdaient sinon). */
static void write_settings_ini(FILE *f, const Settings *s)
{
    fprintf(f, "volume=%.2f\n",    s->volume);
    fprintf(f, "theme=%d\n",       s->theme_index);
    fprintf(f, "viz=%d\n",         (int)s->viz_style);
    fprintf(f, "shuffle=%d\n",     s->shuffle);
    fprintf(f, "nd_endless_radio=%d\n", s->nd_endless_radio);
    fprintf(f, "white_noise_keepalive=%d\n", s->white_noise_keepalive);
    fprintf(f, "distortion=%d\n", s->distortion);
    fprintf(f, "prev_seek_threshold=%d\n", s->prev_seek_threshold);
    fprintf(f, "pause_fade_sec=%.1f\n", s->pause_fade_sec);
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
    fprintf(f, "easter_eggs=%d\n", s->easter_eggs_enabled);
    fprintf(f, "seasonal_events=%d\n", s->seasonal_events_enabled);
    fprintf(f, "lid_playback=%d\n", s->lid_playback_enabled);
    fprintf(f, "pwsave=%d\n", s->power_save_enabled);
    fprintf(f, "pwsavedelay=%d\n", s->power_save_delay_sec);
    fprintf(f, "vizen=%d\n", s->viz_enabled);
    fprintf(f, "pwcolor=%x\n", (unsigned int)s->power_save_text_color);
    fprintf(f, "pwr=%d\n", s->power_save_r);
    fprintf(f, "pwg=%d\n", s->power_save_g);
    fprintf(f, "pwb=%d\n", s->power_save_b);
    fprintf(f, "pwmode=%d\n", s->power_save_color_mode);
    fprintf(f, "wifioff=%d\n", s->wifi_off_in_app);
    fprintf(f, "sleep=%d\n", s->sleep_timer_min);
    fprintf(f, "sleepshut=%d\n", s->sleep_shutdown);
    fprintf(f, "ecobackmode=%d\n", s->eco_backlight_mode);
    fprintf(f, "ecorestore=%d\n", s->eco_restore_brightness);
    fprintf(f, "settabs=%d\n", s->settings_use_tabs);
    fprintf(f, "prospeed=%.2f\n", s->pro_speed);
    fprintf(f, "alarm_en=%d\n", s->alarm_enabled);
    fprintf(f, "alarm_h=%d\n", s->alarm_hour);
    fprintf(f, "alarm_m=%d\n", s->alarm_minute);
    fprintf(f, "alarm_p=%s\n", s->alarm_path);
    fprintf(f, "pitch=%d\n", s->pitch_semitones);
    /* ND2 : compte Navidrome unique, jamais persiste sur la SD */
    for (int i = 0; i < 8; i++)
        fprintf(f, "eq%d=%.2f\n", i, s->eq_gains[i]);
    for (int p = 0; p < 7; p++)
        for (int b = 0; b < 8; b++)
            fprintf(f, "eq_p%d_b%d=%.2f\n", p, b, s->eq_gains_per_preset[p][b]);
}

static void save_thread_func(void *arg)
{
    bool keep_going = true;
    while (keep_going) {
        mkdir("sdmc:/3DSoundShell", 0777);
        FILE *f = fopen(SETTINGS_PATH, "w");
        if (f) {
            if (s_lock_init) LightLock_Lock(&s_save_lock);
            write_settings_ini(f, &s_save_copy);
            if (s_lock_init) LightLock_Unlock(&s_save_lock);
            fclose(f);
        }

        if (s_lock_init) LightLock_Lock(&s_save_lock);
        if (s_save_pending) {
            s_save_pending = false; /* On prend en charge la sauvegarde en attente et on reboucle */
        } else {
            s_save_running = false;
            keep_going = false;     /* Plus rien en attente, on peut eteindre le thread */
        }
        if (s_lock_init) LightLock_Unlock(&s_save_lock);
    }
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
    g_settings.easter_eggs_enabled = true;
    g_settings.seasonal_events_enabled = true;
    g_settings.lid_playback_enabled = false;
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
    g_settings.nd_endless_radio = 0;
    g_settings.sleep_timer_min = 0;  /* V0.97 : sleep timer OFF par defaut */
    g_settings.sleep_shutdown = false;
    g_settings.eco_backlight_mode = 0; /* V0.97 : OFF par defaut */
    g_settings.eco_restore_brightness = 5; /* V0.97 : lum restauree par defaut = 5 */
    g_settings.settings_use_tabs = 0;
    g_settings.pro_speed = 1.0f;        /* V0.97 : vitesse pro neutre */
    g_settings.pitch_semitones = 0;     /* V0.97 : pitch pro neutre */  /* V0.96 : Wi-Fi ON par defaut (eco optionnelle) */      /* 5 = 2 secondes par defaut */   /* LED ON par defaut */
    for (int i = 0; i < 8; i++) g_settings.eq_gains[i] = 0;
    /* EQ per-preset storage (7 presets x 8 bands) */
    {
        static const float factory[7][8] = {
            { 0, 0, 0, 0, 0, 0, 0, 0},   /* Flat */
            { 6, 5, 4, 1, 0, 0, 0, 0},   /* Bass Boost */
            {-2,-1, 2, 4, 4, 3, 1, 0},   /* Vocal */
            { 4, 3, 0,-1, 0, 2, 4, 5},   /* Rock */
            { 3, 2, 0, 0, 0, 0, 2, 3},   /* Classical */
            { 5, 4, 0,-1, 1, 3, 4, 5},   /* Electronic */
            { 0, 0, 0, 0, 0, 0, 0, 0}    /* Custom */
        };
        for (int p = 0; p < 7; p++)
            for (int b = 0; b < 8; b++)
                g_settings.eq_gains_per_preset[p][b] = factory[p][b];
    }
    strncpy(g_settings.start_dir, "sdmc:/Music", 511);
    /* ====================================================================
     * [LOGIN NAVIDROME] Identifiants par defaut (site / identifiant / mdp).
     * -> Pour les CHANGER : modifie les 3 valeurs juste en dessous.
     * -> Ces valeurs ne servent QUE si aucun settings.ini n'existe encore
     *    ou qu'il ne contient pas nd_url=/nd_user=/nd_pass=.
     * -> Une fois l'app lancee, si l'utilisateur modifie l'URL/identifiant/
     *    mot de passe dans les parametres, les nouvelles valeurs sont
     *    sauvegardees dans settings.ini (cle "nd_url=" / "nd_user=" /
     *    "nd_pass=") et relues au demarrage ci-dessous dans settings_load()
     *    (recherche "nd_url=" dans ce fichier).
     * -> Champs declares dans include/settings.h (navidrome_url/user/pass).
     * ==================================================================== */
    strncpy(g_settings.navidrome_url,  "url server", 127);
    g_settings.navidrome_url[127] = 0;
    strncpy(g_settings.navidrome_user, "id", 63);
    g_settings.navidrome_user[63] = 0;
    strncpy(g_settings.navidrome_pass, "pass", 127);
    g_settings.navidrome_pass[127] = 0;

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
        float fval; int ival;
        if (sscanf(line,"volume=%f",  &fval)==1) { g_settings.volume=fval;             continue; }
        if (sscanf(line,"theme=%d",   &ival)==1) { g_settings.theme_index=ival;        continue; }
        if (sscanf(line,"viz=%d",     &ival)==1) { g_settings.viz_style=(VisualizerStyle)ival; continue; }
        if (sscanf(line,"shuffle=%d",&ival)==1) { g_settings.shuffle=ival;            continue; }
        if (sscanf(line,"nd_endless_radio=%d",&ival)==1) { g_settings.nd_endless_radio=ival; continue; }
        if (sscanf(line,"white_noise_keepalive=%d",&ival)==1) { g_settings.white_noise_keepalive=ival; continue; }
        if (sscanf(line,"distortion=%d",&ival)==1) { g_settings.distortion=ival; continue; }
        if (sscanf(line,"prev_seek_threshold=%d",&ival)==1) { g_settings.prev_seek_threshold=ival; continue; }
        { float fv; if (sscanf(line,"pause_fade_sec=%f",&fv)==1) { g_settings.pause_fade_sec=fv; continue; } }
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
        if (sscanf(line,"easter_eggs=%d",&ival)==1){ g_settings.easter_eggs_enabled=ival; continue; }
        if (sscanf(line,"seasonal_events=%d",&ival)==1){ g_settings.seasonal_events_enabled=ival; continue; }
        if (sscanf(line,"lid_playback=%d",&ival)==1){ g_settings.lid_playback_enabled=ival; continue; }
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
        if (sscanf(line,"alarm_en=%d",&ival)==1){ g_settings.alarm_enabled=ival; continue; }
        if (sscanf(line,"alarm_h=%d",&ival)==1){ g_settings.alarm_hour=ival; continue; }
        if (sscanf(line,"alarm_m=%d",&ival)==1){ g_settings.alarm_minute=ival; continue; }
        if (strncmp(line,"alarm_p=",8)==0){ strncpy(g_settings.alarm_path, line+8, 511); char *nl=strchr(g_settings.alarm_path,'\n'); if(nl)*nl=0; nl=strchr(g_settings.alarm_path,'\r'); if(nl)*nl=0; continue; }
        if (sscanf(line,"pitch=%d",&ival)==1){ g_settings.pitch_semitones=ival; continue; }
        /* [LOGIN NAVIDROME] Relecture des identifiants sauvegardes (voir le bloc commente dans settings_init() plus haut) */
        if (strncmp(line,"nd_url=",7)==0){ char *v=line+7; v[strcspn(v,"\r\n")]=0; strncpy(g_settings.navidrome_url, v, 127); g_settings.navidrome_url[127]=0; continue; }  /* ND1 */
        if (strncmp(line,"nd_user=",8)==0){ char *v=line+8; v[strcspn(v,"\r\n")]=0; strncpy(g_settings.navidrome_user, v, 63); g_settings.navidrome_user[63]=0; continue; }
        if (strncmp(line,"nd_pass=",8)==0){ char *v=line+8; v[strcspn(v,"\r\n")]=0; strncpy(g_settings.navidrome_pass, v, 127); g_settings.navidrome_pass[127]=0; continue; }
        for (int i = 0; i < 8; i++) {
            char key[8]; snprintf(key, 8, "eq%d=%%f", i);
            if (sscanf(line, key, &fval)==1) { g_settings.eq_gains[i]=fval; break; }
        { int pp, bb; float fv;
          if (sscanf(line, "eq_p%d_b%d=%f", &pp, &bb, &fv) == 3) {
              if (pp >= 0 && pp < 7 && bb >= 0 && bb < 8)
                  g_settings.eq_gains_per_preset[pp][bb] = fv;
              continue;
          }
        }
        }
    }
    fclose(f);
    aptSetSleepAllowed(!g_settings.lid_playback_enabled);
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

extern void settings_save(void); /* SETFIX */
void settings_update(void)
{
    /* Appelle a chaque frame - save seulement si dirty ET assez de temps ecoule */
    if (!s_settings_dirty) return;

    u64 now = osGetTime();
    if (now - s_dirty_since < SAVE_DEBOUNCE_MS) return;

    /* Assez de temps ecoule sans changement, on save */
    s_settings_dirty = false;
    settings_save(); /* SETFIX */
}

void settings_save(void)
{
    if (s_lock_init) LightLock_Lock(&s_save_lock);

    /* Copie toujours fraiche et securisee des reglages */
    memcpy(&s_save_copy, &g_settings, sizeof(Settings));

    if (s_save_running) {
        s_save_pending = true; /* Un enregistrement est deja en cours, on signale qu'il faudra reboucler */
        if (s_lock_init) LightLock_Unlock(&s_save_lock);
        return;
    }

    s_save_running = true;
    s_save_pending = false;
    if (s_lock_init) LightLock_Unlock(&s_save_lock);

    /* Thread detache basse priorite pour ne pas faire ramer l'UI */
    Thread t = threadCreate(
        save_thread_func,
        NULL,
        8 * 1024,
        0x3F,
        -1,
        true
    );

    if (!t) {
        /* Securite si la console ne peut pas creer de thread */
        save_thread_func(NULL);
    }
}


/* Vraiment synchrone et bloquant : n'appeler qu'a des points ou geler le
   thread appelant est sans consequence (extinction de l'app, avant veille).
   Ne JAMAIS l'appeler depuis la boucle de rendu/input (ex: en quittant
   l'ecran Settings) -> utiliser settings_save() (async) a la place, sinon
   l'attente ci-dessous + les ~30 ecritures SD bloquent l'UI (freeze). */
void settings_save_sync(void)
{
    /* Attendre la fin de la sauvegarde asynchrone en cours */
    int timeout = 100;
    while (s_save_running && timeout-- > 0)
        svcSleepThread(10000000LL);

    if (s_lock_init) LightLock_Lock(&s_save_lock);

    mkdir("sdmc:/3DSoundShell", 0777);

    FILE *f = fopen(SETTINGS_PATH, "w");
    if (f) {
        write_settings_ini(f, &g_settings);
        fclose(f);
    }

    if (s_lock_init) LightLock_Unlock(&s_save_lock);
}


/* ── Settings items ───────────────────────────────────────────── */
#define SITEM_COUNT 40  /* ND1 : + Navidrome */
#define NUM_TABS 7

/* V0.97 : mapping item -> onglet (0=Lecteur, 1=Audio, 2=Affichage, 3=Systeme, 4=Infos) */
static const int sitem_tab[SITEM_COUNT] = {
    0, 2, 2, 2, 0, 0, 0, 0, 1, 1,
    4, 2, 4, 2, 2, 2, 2, 4, 0, 0,
    2, 1, 1, 2, 2, 6, 3, 3, 3, 3,
    3, 0, 0, 5, 5, 1, 1, 0, 1, 2
};

/* Nom des onglets */
static const char *tab_names[NUM_TABS] = {
    "Lecteur", "Audio", "Affichage", "Reveil", "Systeme", "App", "Infos"
};

/* Onglet actif (variable statique) */
static int s_active_tab = 0;
static const char *sitem_tooltips_fr[SITEM_COUNT] = {
    "Ajuste le volume sonore principal de l'application.",
    "Modifie l'apparence visuelle et les couleurs de l'interface.",
    "Choisit le style de l'animation visuelle en lecture.",
    "Affiche ou masque l'image de couverture du morceau.",
    "Lit les pistes de la liste de lecture en ordre aleatoire.",
    "Repete la piste actuelle, toute la liste ou aucune.",
    "Reprend automatiquement la derniere lecture au lancement.",
    "Dossier ouvert par defaut dans le navigateur de fichiers.",
    "Applique un reglage d'egaliseur audio (Presets).",
    "Modifie simultanement la vitesse et le pitch. (optimise)",
    "Active ou desactive la LED de notification 3DS.",
    "Duree d'affichage de l'animation de demarrage (splash).",
    "Choisit la langue de l'interface (Francais / English).",
    "Active l'ecran qui permet une meilleur duree de batterie avec une horloge.",
    "Delai d'inactivite avant l'activation de l'ecran eco.",
    "Active ou coupe le calcul des visualiseurs audio.",
    "Couleur des informations affichees en mode eco.",
    "Coupe le Wi-Fi dans l'app pour economiser la batterie.",
    "Duree de la minuterie avant la mise en pause de la musique ou l'arret de la console.",
    "Eteint completement la console a la fin du timer.",
    "Bascule entre affichage par onglets ou liste directe.",
    "Vitesse sans changer le pitch (SoundTouch). (lourd)",
    "Hauteur de voix (demi-tons) sans changer la vitesse. (lourd)",
    "Mode de retroeclairage en eco (OFF/REDUIRE/ETEINDRE).",
    "Niveau de luminosite restaure apres le mode eco. (restriction Nintendo)",
    "Version actuelle de l'app.",
    "Active ou desactive l'alarme du reveil.",
    "Heure de declenchement de l'alarme.",
    "Minute de declenchement de l'alarme.",
    "Choisir la musique a jouer lors du reveil.",
    "Lance l'ecran Noir Eco+ pour la nuit.",
    "Active ou desactive les secrets et Easter Eggs du lecteur.",
    "Lecture de la musique lorsque le clapet de la console est ferme. (Jack seul)",
    "Lance le mode Navidrome : musique depuis votre serveur.",
    "Enregistre le son du micro sur la carte SD.",
    "Bruit blanc inaudible empechant la mise en veille des enceintes Bluetooth.",
    "Wobble analogique : oscille vitesse et pitch.",
    "Seuil de temps pour revenir au debut de la piste au lieu de la piste precedente.",
    "Duree du fondu sonore lors de la pause et de la reprise.",
    "Active ou desactive les animations et evenements saisonniers (Halloween, etc.)."
};

static const char *sitem_tooltips_en[SITEM_COUNT] = {
    "Adjust the main audio volume of the application.",
    "Change the visual appearance and interface colors.",
    "Choose the visual animation style during playback.",
    "Show or hide the cover art of the current track.",
    "Play the tracks in the playlist in random order.",
    "Repeat the current track, the whole list, or none.",
    "Automatically resume the last playback on startup.",
    "Default folder opened in the file browser.",
    "Apply an audio equalizer preset.",
    "Modify speed and pitch simultaneously. (optimized)",
    "Enable or disable the 3DS notification LED.",
    "Duration of the startup animation (splash).",
    "Choose the interface language (Francais / English).",
    "Enable the battery saving screen with a clock.",
    "Inactivity delay before activating the eco screen.",
    "Enable or disable audio visualizer calculations.",
    "Color of the information displayed in eco mode.",
    "Turn off Wi-Fi in the app to save battery.",
    "Timer duration before pausing music or shutting down the console.",
    "Completely shut down the console at the end of the timer.",
    "Switch between tabbed view or direct list.",
    "Speed without changing pitch (SoundTouch). (heavy)",
    "Voice pitch (semitones) without changing speed. (heavy)",
    "Backlight mode in eco (OFF/REDUCE/TURN OFF).",
    "Brightness level restored after eco mode. (Nintendo restriction)",
    "Current version of the app.",
    "Enable or disable the alarm clock.",
    "Alarm trigger hour.",
    "Alarm trigger minute.",
    "Choose the music to play for the alarm.",
    "Launch the Eco+ Black screen for the night.",
    "Enable or disable secrets and Easter Eggs in the player.",
    "Continue audio playback when the console lid is closed. (Jack Only)",
    "Launch Navidrome mode: play music from your server.",
    "Record audio from microphone to SD card.",
    "Inaudible white noise to prevent Bluetooth speakers from turning off.",
    "Analog wobble effect.",
    "Threshold time to restart track instead of going to previous track.",
    "Duration of the audio fade when pausing and resuming.",
    "Enable or disable seasonal animations and events (Halloween, etc.)."
};

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
    "Vitesse + pitch",
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
    "Vitesse (pro)",
    "Pitch (pro)",
    "Retroeclairage eco",
    "Lum apres eco",
    "Version",
    "Reveil",
    "Heure (HH)",
    "Minute (MM)",
    "Musique (Custom)",
    "LANCER MODE NUIT",
    "Easter Eggs",
    "Lecture clapet ferme",
    "Navidrome",
    "Magnetophone",
    "Son blanc anti-veille",
    "Distorsion (fun)",
    "Retour debut",
    "Fondu pause/play",
    "Evenements"
};

/* V0.98 : Mutex vitesse simple <-> vitesse/pitch pro */
static bool s_speed_simple_active(void)
{
    float sp = g_settings.playback_speed;
    if (sp <= 0.01f) sp = 1.0f;
    return fabsf(sp - 1.0f) > 0.001f;
}

static bool s_speed_pro_active(void)
{
    float ps = g_settings.pro_speed;
    if (ps <= 0.01f) ps = 1.0f;
    return fabsf(ps - 1.0f) > 0.001f || g_settings.pitch_semitones != 0;
}

static bool s_item_locked(int idx)
{
    bool dist_active = (g_settings.distortion > 0);
    if (idx == 9) return s_speed_pro_active() || dist_active;
    if (idx == 21 || idx == 22) return s_speed_simple_active();
    if (idx == 36) return s_speed_simple_active() || s_speed_pro_active();
    return false;
}

static void sitem_value(int idx, char *buf, int bufsz)
{
    switch (idx) {
        case 26: snprintf(buf, bufsz, "%s", g_settings.alarm_enabled ? T("ON") : T("OFF")); break;
        case 27: snprintf(buf, bufsz, "%02d h", g_settings.alarm_hour); break;
        case 28: snprintf(buf, bufsz, "%02d m", g_settings.alarm_minute); break;
        case 29: snprintf(buf, bufsz, "%s", g_settings.alarm_path[0] ? "Custom" : T("Piste Actuelle")); break;
        case 30: snprintf(buf, bufsz, "%s", g_settings.alarm_enabled ? T("> PRESSER A <") : T("[REVEIL INACTIF]")); break;
        case 31: snprintf(buf, bufsz, "%s", g_settings.easter_eggs_enabled ? T("ON") : T("OFF")); break;
        case 32: snprintf(buf, bufsz, "%s", g_settings.lid_playback_enabled ? T("ON") : T("OFF")); break;
        

        case 33: snprintf(buf,bufsz,"%s", g_settings.navidrome_url[0] ? "A = Lancer" : "Non configure"); break;
        case 34: snprintf(buf,bufsz,"%s", "A = Lancer"); break;  /* ND1 */
        case 0: snprintf(buf,bufsz,"%.0f%%", g_settings.volume*100); break;
        case 1: snprintf(buf,bufsz,"%s", theme_name(g_settings.theme_index)); break;
        case 2: { const char *v[]={T("Barres"),T("Onde"),T("Cercle"),T("Feu"),T("Oscillo"),T("Waves"),T("Tunnel"),T("Pulse"),T("Waveform"),T("EQ")};
                  snprintf(buf,bufsz,"%s",v[g_settings.viz_style % 10]); break; }
        case 3: snprintf(buf,bufsz,"%s", g_settings.show_cover?T("ON"):T("OFF")); break;
        case 4: snprintf(buf,bufsz,"%s", g_settings.shuffle?T("ON"):T("OFF")); break;
        case 5: { const char *r[]={T("Non"),T("x1"),T("Tout")};
                  snprintf(buf,bufsz,"%s",r[g_settings.repeat]); break; }
        case 6: snprintf(buf,bufsz,"%s", g_settings.resume_on_start?T("ON"):T("OFF")); break;
        case 7: {
            if (strcmp(g_settings.start_dir, "sdmc:/Music") == 0) {
                snprintf(buf, bufsz, "%s", T("Defaut"));
            } else {
                snprintf(buf, bufsz, "Custom: %.12s", strrchr(g_settings.start_dir, '/') ? strrchr(g_settings.start_dir, '/') + 1 : g_settings.start_dir);
            }
            break;
        }
        case 8: {
            const char *eqnames[] = {"Flat","Bass Boost","Vocal","Rock","Classical","Electronic","Custom"};
            int pi = g_settings.eq_preset_index;
            if (pi < 0 || pi > 6) pi = 0;
            snprintf(buf, bufsz, "%s", eqnames[pi]);
            break;
        }
        case 9: {
            float sp = g_settings.playback_speed;
            if (sp <= 0.01f) sp = 1.0f;
            snprintf(buf, bufsz, "%.2fx%s", sp, s_item_locked(9) ? " [LOCK]" : "");
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
                                  case 35: snprintf(buf,bufsz,"%s", g_settings.white_noise_keepalive?T("ON"):T("OFF")); break;
        case 36: if(g_settings.distortion==0) snprintf(buf,bufsz,"OFF%s", s_item_locked(36) ? " [LOCK]" : ""); else snprintf(buf,bufsz,"%d%%%s",g_settings.distortion, s_item_locked(36) ? " [LOCK]" : ""); break;
        case 37: { const char *m[] = {T("OFF"), "2 s", "5 s", "10 s"}; snprintf(buf, bufsz, "%s", m[g_settings.prev_seek_threshold % 4]); break; }
        case 38: { if (g_settings.pause_fade_sec <= 0.05f) snprintf(buf, bufsz, "%s", T("OFF")); else snprintf(buf, bufsz, "%.1f s", g_settings.pause_fade_sec); break; }
        case 39: snprintf(buf, bufsz, "%s", g_settings.seasonal_events_enabled ? T("ON") : T("OFF")); break;
        case 18: {
            int m = g_settings.sleep_timer_min;
            if (m == 0) snprintf(buf, bufsz, "OFF");
            else if (m < 60) snprintf(buf, bufsz, "%d min", m);
            else snprintf(buf, bufsz, "%d h %d", m/60, m%60);
            break;
        }
        case 19: snprintf(buf,bufsz,"%s", g_settings.sleep_shutdown?T("ON"):T("OFF")); break;
        case 20: snprintf(buf,bufsz,"%s", g_settings.settings_use_tabs?T("Onglets"):T("Direct")); break;
        case 21: snprintf(buf,bufsz,"%.2fx%s", g_settings.pro_speed, s_item_locked(21) ? " [LOCK]" : ""); break;
        case 22: snprintf(buf,bufsz,"%+d%s", g_settings.pitch_semitones, s_item_locked(22) ? " [LOCK]" : ""); break;
        case 23: {
            const char *modes[] = {T("OFF"), T("REDUIRE"), T("ETEINDRE")};
            int m = g_settings.eco_backlight_mode;
            if (m < 0 || m > 2) m = 0;
            snprintf(buf, bufsz, "%s", modes[m]);
            break;
        }
        case 24: {
            /* Grise si mode != REDUIRE (car pas utilise) */
            if (g_settings.eco_backlight_mode != 1) {
                snprintf(buf, bufsz, T("Non disponible"));
            } else {
                snprintf(buf, bufsz, "%d/5", g_settings.eco_restore_brightness);
            }
            break;
        }
        case 25: snprintf(buf,bufsz,"V" APP_VERSION); break;

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
    /* V0.98 : Tooltip d'explication sur l'ecran du HAUT */
    if (s_current_selected_item >= 0 && s_current_selected_item < SITEM_COUNT) {
        if (s_last_settings_input_time == 0) s_last_settings_input_time = osGetTime();
        if (osGetTime() - s_last_settings_input_time >= 4000) {
            int idx = s_current_selected_item;
            const char *tip = (idx >= 0 && idx < SITEM_COUNT) ? ((g_settings.language == 1) ? sitem_tooltips_en[idx] : sitem_tooltips_fr[idx]) : "";
            float tx = 10.0f, ty = TOP_HEIGHT - 48.0f, tw = TOP_WIDTH - 20.0f, th_h = 40.0f;
            u32 bg_col = RGBA8(10, 10, 20, 245);
            u32 border_col = current_theme ? current_theme->accent : RGBA8(0, 180, 255, 255);
            u32 text_col = current_theme ? current_theme->text_primary : RGBA8(255, 255, 255, 255);

            draw_rect(tx, ty, tw, th_h, bg_col);
            C2D_DrawRectSolid(tx, ty, 0, tw, 1, border_col);
            C2D_DrawRectSolid(tx, ty + th_h - 1, 0, tw, 1, border_col);
            C2D_DrawRectSolid(tx, ty, 0, 1, th_h, border_col);
            C2D_DrawRectSolid(tx + tw - 1, ty, 0, 1, th_h, border_col);

            draw_text(tx + 6, ty + 3, 0.42f, border_col, T(sitem_labels[idx]));
            draw_text(tx + 6, ty + 21, 0.38f, text_col, tip);
        }
    }
}


void settings_draw_bottom(int *selected_item)
{
    s_current_selected_item = *selected_item;

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
            draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 14, /* DECAL2 : marge droite 6->14, le % etait rogne */
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
            draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 14, /* DECAL2 : marge droite 6->14, le % etait rogne */
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
            draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 14, /* DECAL2 : marge droite 6->14, le % etait rogne */
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
            draw_text(BOT_WIDTH - (float)strlen(vbuf)*5.5f - 14, /* DECAL2 : marge droite 6->14, le % etait rogne */
                      y+3, 0.44f, th->accent, vbuf);
            draw_rect(0, y+19, BOT_WIDTH, 1, th->border);
            drawn++;
        }
    }


}

void settings_handle_input(int *selected_item, u32 keys_down)
{
    if (keys_down != 0) s_last_settings_input_time = osGetTime();

    /* REVEIL4B : maintien de LEFT/RIGHT sur Heure(27)/Minute(28) de
       l'alarme -> auto-repeat : delai 18 frames (~0.3s) puis 1 pas
       toutes les 4 frames (~15/s). Reglages : les 2 constantes 18/4. */
    {
        static u32 s_ar_last  = 0;
        static int s_ar_count = 0;
        u32 held_lr = hidKeysHeld() & (KEY_LEFT | KEY_RIGHT);
        if (held_lr != s_ar_last) {
            s_ar_last  = held_lr;
            s_ar_count = 0;
        } else if (held_lr != 0 &&
                   (*selected_item == 27 || *selected_item == 28)) {
            s_ar_count++;
            if (s_ar_count > 18 && ((s_ar_count - 18) % 4) == 0) {
                keys_down |= held_lr;
                s_last_settings_input_time = osGetTime();
            }
        }
    }

    /* V0.97 : X = toggle rapide entre Direct et Onglets */
        /* TACTILE SUR LES ONGLETS PARAMÈTRES */
    if (g_settings.settings_use_tabs && (keys_down & KEY_TOUCH)) {
        touchPosition tpos;
        hidTouchRead(&tpos);
        if (tpos.py >= 46 && tpos.py <= 68) {
            float tab_w = (float)BOT_WIDTH / (float)NUM_TABS;
            int clicked_tab = (int)((float)tpos.px / tab_w);
            if (clicked_tab >= 0 && clicked_tab < NUM_TABS) {
                s_active_tab = clicked_tab;
                for (int i = 0; i < SITEM_COUNT; i++) {
                    if (sitem_tab[i] == s_active_tab) { *selected_item = i; break; }
                }
                return;
            }
        }
    }

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
            case 36: { if (!s_item_locked(36)) { g_settings.distortion+=5; if(g_settings.distortion>100) g_settings.distortion=100; settings_mark_dirty(); } break; }
            case 37: { g_settings.prev_seek_threshold = (g_settings.prev_seek_threshold + 1) % 4; settings_mark_dirty(); break; }
            case 38: { g_settings.pause_fade_sec += 0.1f; if (g_settings.pause_fade_sec > 1.05f) g_settings.pause_fade_sec = 0.0f; settings_mark_dirty(); break; }
            case 39: g_settings.seasonal_events_enabled = !g_settings.seasonal_events_enabled; settings_mark_dirty(); break;
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
            case 7: {
                    extern void dir_picker_start(void);
                    dir_picker_start();
                    break;
                }
            case 8: g_settings.eq_preset_index=(g_settings.eq_preset_index+1)%7;
                    { for(int i=0;i<8;i++) audio_eq_set_gain(i, g_settings.eq_gains_per_preset[g_settings.eq_preset_index][i]); }
                    break;
            case 9: {
                    if (!s_item_locked(9)) {
                        if (g_settings.playback_speed <= 0.01f)
                            g_settings.playback_speed = 1.0f;
                        g_settings.playback_speed += 0.05f;
                        if (g_settings.playback_speed > 2.0f)
                            g_settings.playback_speed = 2.0f;
                        audio_set_speed(g_settings.playback_speed);
                        settings_mark_dirty();
                    }
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
            case 35: { g_settings.white_noise_keepalive = !g_settings.white_noise_keepalive; if(!g_settings.white_noise_keepalive){extern void audio_stop_white_noise(void);audio_stop_white_noise();} settings_mark_dirty(); break; }
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
            case 21: if (!s_item_locked(21)) {
                         g_settings.pro_speed += 0.05f;
                         if (g_settings.pro_speed > 2.0f) g_settings.pro_speed = 2.0f;
                         settings_mark_dirty();
                     } break;
            case 22: if (!s_item_locked(22)) {
                         g_settings.pitch_semitones++;
                         if (g_settings.pitch_semitones > 12) g_settings.pitch_semitones = 12;
                         settings_mark_dirty();
                     } break;
            case 23: g_settings.eco_backlight_mode = (g_settings.eco_backlight_mode + 1) % 3;
                     settings_mark_dirty();
                     if (power_save_is_active()) {
                         power_save_force_restore();
                         power_save_refresh();
                     }
                     break;
            case 24: /* Restauration lum : cycle 1-5, seulement si mode == REDUIRE */
                     if (g_settings.eco_backlight_mode == 1) {
                         g_settings.eco_restore_brightness++;
                         if (g_settings.eco_restore_brightness > 5) g_settings.eco_restore_brightness = 1;
                         settings_mark_dirty();
                     }
                     break;
            case 26: g_settings.alarm_enabled = !g_settings.alarm_enabled; settings_mark_dirty(); break;
            case 27: g_settings.alarm_hour = (g_settings.alarm_hour + 1) % 24; settings_mark_dirty(); break;
            case 28: g_settings.alarm_minute = (g_settings.alarm_minute + 1) % 60; settings_mark_dirty(); break;
            case 30: if (g_settings.alarm_enabled) { extern void night_mode_start(void); night_mode_start(); } break;
            case 31: g_settings.easter_eggs_enabled = !g_settings.easter_eggs_enabled; settings_mark_dirty(); break;
            case 32: g_settings.lid_playback_enabled = !g_settings.lid_playback_enabled; aptSetSleepAllowed(!g_settings.lid_playback_enabled); settings_mark_dirty(); break;
            case 29: {
                    extern void file_picker_start(void);
                    file_picker_start();
                    break;
                }
        }
    }

    if (keys_down & KEY_LEFT) {
        switch (idx) {
            case 36: { if (!s_item_locked(36)) { g_settings.distortion-=5; if(g_settings.distortion<0) g_settings.distortion=0; settings_mark_dirty(); } break; }
            case 37: { g_settings.prev_seek_threshold = (g_settings.prev_seek_threshold - 1 + 4) % 4; settings_mark_dirty(); break; }
            case 38: { g_settings.pause_fade_sec -= 0.1f; if (g_settings.pause_fade_sec < -0.05f) g_settings.pause_fade_sec = 1.0f; settings_mark_dirty(); break; }
            case 39: g_settings.seasonal_events_enabled = !g_settings.seasonal_events_enabled; settings_mark_dirty(); break;
            case 0: g_settings.volume-=0.05f;
                    if(g_settings.volume<0) g_settings.volume=0;
                    audio_set_volume(g_settings.volume); break;
            case 1: g_settings.theme_index=(g_settings.theme_index-1+theme_count())%theme_count();
                    theme_set(theme_get(g_settings.theme_index)); break;
            case 2: g_settings.viz_style=(VisualizerStyle)((g_settings.viz_style-1+VIZ_COUNT)%VIZ_COUNT); break;
            case 9: {
                    if (!s_item_locked(9)) {
                        if (g_settings.playback_speed <= 0.01f)
                            g_settings.playback_speed = 1.0f;
                        g_settings.playback_speed -= 0.05f;
                        if (g_settings.playback_speed < 0.5f)
                            g_settings.playback_speed = 0.5f;
                        audio_set_speed(g_settings.playback_speed);
                        settings_mark_dirty();
                    }
                    break;
            }
            case 4: g_settings.shuffle=!g_settings.shuffle;
                    settings_mark_dirty(); break;
            case 5: g_settings.repeat=(g_settings.repeat+2)%3;
                    settings_mark_dirty(); break;
            case 7: strncpy(g_settings.start_dir, "sdmc:/Music", 511); settings_mark_dirty(); break;
            case 8: g_settings.eq_preset_index=(g_settings.eq_preset_index+6)%7;
                    { for(int i=0;i<8;i++) audio_eq_set_gain(i, g_settings.eq_gains_per_preset[g_settings.eq_preset_index][i]); }
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
            case 21: if (!s_item_locked(21)) {
                         g_settings.pro_speed -= 0.05f;
                         if (g_settings.pro_speed < 0.5f) g_settings.pro_speed = 0.5f;
                         settings_mark_dirty();
                     } break;
            case 22: if (!s_item_locked(22)) {
                         g_settings.pitch_semitones--;
                         if (g_settings.pitch_semitones < -12) g_settings.pitch_semitones = -12;
                         settings_mark_dirty();
                     } break;
            case 23: g_settings.eco_backlight_mode = (g_settings.eco_backlight_mode + 2) % 3;
                     settings_mark_dirty();
                     if (power_save_is_active()) {
                         power_save_force_restore();
                         power_save_refresh();
                     }
                     break;
            case 24: /* Restauration lum : cycle 1-5, seulement si mode == REDUIRE */
                     if (g_settings.eco_backlight_mode == 1) {
                         g_settings.eco_restore_brightness--;
                         if (g_settings.eco_restore_brightness < 1) g_settings.eco_restore_brightness = 5;
                         settings_mark_dirty();
                     }
                     break;
            case 26: g_settings.alarm_enabled = !g_settings.alarm_enabled; settings_mark_dirty(); break;
            case 27: g_settings.alarm_hour = (g_settings.alarm_hour - 1 < 0) ? 23 : g_settings.alarm_hour - 1; settings_mark_dirty(); break;
            case 28: g_settings.alarm_minute = (g_settings.alarm_minute - 1 < 0) ? 59 : g_settings.alarm_minute - 1; settings_mark_dirty(); break;
            case 30: if (g_settings.alarm_enabled) { extern void night_mode_start(void); night_mode_start(); } break;
            case 31: g_settings.easter_eggs_enabled = !g_settings.easter_eggs_enabled; settings_mark_dirty(); break;
            case 32: g_settings.lid_playback_enabled = !g_settings.lid_playback_enabled; aptSetSleepAllowed(!g_settings.lid_playback_enabled); settings_mark_dirty(); break;
        }
    }

    if ((keys_down & KEY_A) && idx == 36) { g_settings.distortion = (g_settings.distortion > 0) ? 0 : 35; settings_mark_dirty(); }
    if ((keys_down & KEY_A) && idx == 37) { g_settings.prev_seek_threshold = (g_settings.prev_seek_threshold > 0) ? 0 : 2; settings_mark_dirty(); }
    if ((keys_down & KEY_A) && idx == 38) { g_settings.pause_fade_sec = (g_settings.pause_fade_sec > 0.05f) ? 0.0f : 0.2f; settings_mark_dirty(); }
    if ((keys_down & KEY_A) && idx == 39) { g_settings.seasonal_events_enabled = !g_settings.seasonal_events_enabled; settings_mark_dirty(); }

    /* A sur "Sauvegarder" = save manuel */
    if ((keys_down & KEY_A) && idx == SITEM_COUNT-1) {
        settings_mark_dirty();
    }
    if ((keys_down & KEY_A) && idx == 30 && g_settings.alarm_enabled) {
        extern void night_mode_start(void);
        night_mode_start();
    }

    /* A sur "Magnetophone" : app_mutex_claim() met en pause la lecture locale
       ET Navidrome avant d'entrer (sinon le micro enregistrait en tournant
       en meme temps que le decodage+lecture audio - deux threads de plus
       sur un CPU deja juste, et le micro captait la musique en train de
       jouer). Le mecanisme existait (source/app_mutex.c) mais n'etait
       jamais appele nulle part dans le projet. */
    if ((keys_down & KEY_A) && idx == 34) {
        extern void recorder_enter(void);
        extern AppState g_state;
        app_mutex_claim(APP_OWNER_RECORDER);
        recorder_enter();
        g_state = STATE_RECORDER;
        return;
    }
    /* ND1 : A sur "Navidrome" (item 33) = lancer le mode serveur */
    if ((keys_down & KEY_A) && idx == 33) {
        extern void navidrome_start(void);
        app_mutex_claim(APP_OWNER_NAVIDROME);
        navidrome_start();
    }
}
