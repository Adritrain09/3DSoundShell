// player_ui.c
// Interface graphique du lecteur audio (écran du haut + bas)
// ============================================================
// GUIDE RAPIDE POUR MODIFIER L'EFFET 3D :
//
//  d3(X) = décalage 3D, où X est la "profondeur" :
//
//   X positif (+) = l'élément SORT de l'écran (avant-plan)
//   X négatif (-) = l'élément RENTRE dans l'écran (fond)
//   X = 0         = pas d'effet 3D (plan neutre)
//
//  Exemples :
//   d3(0.5f)  = légèrement en avant
//   d3(1.0f)  = bien en avant
//   d3(2.0f)  = très en avant (peut fatiguer les yeux !)
//   d3(-1.0f) = légèrement en fond
//
//  Si l'effet 3D fatigue les yeux → réduire les valeurs
//  Si l'effet 3D est trop discret  → augmenter les valeurs
//
// PROFONDEURS ACTUELLES :
//   Header (barre du haut) ............. d3(0.8f)
//   Pochette album ...................... d3(1.2f)
//   Titre / Artiste / Album ............. d3(0.8f)
//   Barre de progression ................ d3(1.0f)
//   Curseur de progression .............. d3(1.5f)
//   Volume .............................. d3(0.0f)
//   Visualiseur EQ ...................... d3(0.6f)
// ============================================================

#include <malloc.h> /* VIZ15a */
#include "player_ui.h"
extern int nd_get_playing(void);
extern const char* nd_get_playing_title(void);
extern const char* nd_get_playing_artist(void);
extern C2D_Image* nd_get_current_cover_img(void);
#include "main.h"
#include "settings.h"
#include "theme.h"
#include "audio.h"
#include "sleep_timer.h"
#include "i18n.h"
#include "stb_image.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <citro2d.h>
#include <citro3d.h>
#include <time.h>

// ============================================================
// TEXTURE POCHETTE — fonctions internes
// ============================================================

static u32 npow2(u32 v)
{
    v--; v|=v>>1; v|=v>>2; v|=v>>4; v|=v>>8; v|=v>>16;
    return v+1;
}

static u32 morton(u32 x, u32 y)
{
    u32 r = 0;
    for (u32 i = 0; i < 4; i++)
        r |= ((y>>i&1)<<(2*i+1)) | ((x>>i&1)<<(2*i));
    return r;
}

static void upload_cover_tex(PlayerUI *ui, const u8 *rgba, int w, int h)
{
    if (ui->cover_tex_init) {
        C3D_TexDelete(&ui->cover_tex);
        ui->cover_tex_init = false;
    }
    u32 tw = npow2((u32)w);
    u32 th = npow2((u32)h);
    if (!C3D_TexInit(&ui->cover_tex, (u16)tw, (u16)th, GPU_RGBA8))
        return;
    u8 *dst = (u8*)ui->cover_tex.data;
    memset(dst, 0, tw * th * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u32 tile = ((u32)y/8)*(tw/8) + ((u32)x/8);
            u32 mi   = morton((u32)x&7, (u32)y&7);
            u32 di   = (tile*64 + mi) * 4;
            const u8 *s = rgba + (y*w + x) * 4;
            dst[di+0] = s[3];
            dst[di+1] = s[2];
            dst[di+2] = s[1];
            dst[di+3] = s[0];
        }
    }
    C3D_TexSetFilter(&ui->cover_tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&ui->cover_tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
    ui->cover_subtex.width  = (u16)w;
    ui->cover_subtex.height = (u16)h;
    ui->cover_subtex.left   = 0.f;
    ui->cover_subtex.top    = 1.f;
    ui->cover_subtex.right  = (float)w / (float)tw;
    ui->cover_subtex.bottom = 1.f - (float)h / (float)th;
    ui->cover_tex_init = true;
    ui->cover_w = w;
    ui->cover_h = h;
}

// ============================================================
// BUFFER TEXTE OPTIMISÉ
// ─────────────────────────────────────────────────────────────
// PROBLÈME PRÉCÉDENT :
//   Chaque draw_text() faisait C2D_TextBufClear() → des centaines
//   d'appels par seconde → fragmentation mémoire → micro-freezes
//
// SOLUTION :
//   1. TextBuf assez grand pour TOUS les textes d'une frame (8192)
//   2. Clear UNE SEULE FOIS par frame (player_ui_frame_begin)
//   3. Tous les draw_text() de la frame partagent le même buffer
// ============================================================
static C2D_TextBuf s_textbuf = NULL;

// ← Appelé UNE FOIS au début de chaque frame (dans draw_top ET draw_bottom)
static void frame_begin(void)
{
    if (!s_textbuf)
        s_textbuf = C2D_TextBufNew(8192); // ← assez grand pour tous les textes
    C2D_TextBufClear(s_textbuf);          // ← UN SEUL clear par frame
}

// Affiche du texte SANS clear (le clear est fait par frame_begin)
// x, y = position   sz = taille (0.4=petit, 0.5=moyen, 0.6=grand)
// c = couleur   t = texte à afficher
static void draw_text(float x, float y, float sz, u32 c, const char *t)
{
    if (!s_textbuf || !t || !t[0]) return;
    C2D_Text tx;
    C2D_TextParse(&tx, s_textbuf, t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx, C2D_AlignLeft|C2D_WithColor, x, y, 0, sz, sz, c);
}

// ============================================================
// HORLOGE MISE EN CACHE
// ─────────────────────────────────────────────────────────────
// PROBLÈME PRÉCÉDENT :
//   localtime() appelé 60 fois/sec → inutile (secondes changent 1x/sec)
//
// SOLUTION :
//   On ne recalcule l'heure qu'une fois par seconde
// ============================================================
static char  s_clock_str[12]  = "00:00:00";
static u64   s_clock_last_sec = 0;

// ← Appelé une fois par frame, met à jour s_clock_str si nécessaire
static void clock_update(void)
{
    // osGetTime() retourne les millisecondes
    u64 now_sec = osGetTime() / 1000ULL;
    if (now_sec == s_clock_last_sec) return; // pas de changement cette frame
    s_clock_last_sec = now_sec;

    time_t t = (time_t)now_sec;
    struct tm *tm_info = localtime(&t);
    if (tm_info)
        snprintf(s_clock_str, 12, "%02d:%02d:%02d",
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
}

// ============================================================
// FONCTION D3() — calcul du décalage 3D stéréoscopique
//
//   g_3d_iod = intensité du slider 3D (0.0 à 6.0)
//   g_3d_eye = 0 pour oeil gauche, 1 pour oeil droit
//   depth    = profondeur voulue (voir guide en haut)
//
//   * 0.5f = facteur d'échelle global
//            → augmenter pour plus d'effet (ex: 0.7f)
//            → diminuer pour moins d'effet (ex: 0.3f)
// ============================================================
extern float g_3d_iod;
extern int   g_3d_eye;

static float d3(float depth)
{
    if (g_3d_iod <= 0.f) return 0.f;
    float shift = depth * g_3d_iod * 0.5f;
    return (g_3d_eye == 0) ? shift : -shift;
}

// ============================================================
// HELPERS DESSIN
// ============================================================

// Dessine un rectangle plein
static void draw_rect(float x, float y, float w, float h, u32 c)
{
    C2D_DrawRectSolid(x, y, 0, w, h, c);
}

// Formate un temps en secondes → "m:ss"
static void fmt_time(char *out, int sec)
{
    if (sec < 0) sec = 0;
    snprintf(out, 12, "%d:%02d", sec/60, sec%60);
}


// ============================================================
// VISUALISEUR MODE "FEU" — effet flamme avec couleurs du theme
//
// TOUTE la barre change de couleur selon sa hauteur (heat)
// Utilise les 8 couleurs du theme (visualizer_bars[0..7])
//
// REGLAGES :
//   FIRE_BOOST   = sensibilite (2.5 = bien reactif)
//   FIRE_MAX_HEAT = hauteur a laquelle vis_end est atteint
//     1.0  = vis_end atteint a 100% (saturation max)
//     0.75 = vis_end atteint a 75% (atteint plus tot)
//     0.6  = vis_end atteint a 60% (encore plus tot)
// ============================================================
#define FIRE_COLS     16
#define FIRE_BOOST    1.8f
#define FIRE_MAX_HEAT 0.90f  /* vis_end atteint a 90% de hauteur */

static float s_fire_h[FIRE_COLS]    = {0};
static float s_fire_peak[FIRE_COLS] = {0};

/* Recupere couleur interpolee dans le degrade du theme
   pos = 0.0 (vis_start) a 1.0 (vis_end) */
static void fire_get_color(const Theme *th, float pos,
                           u8 *R, u8 *G, u8 *B)
{
    if (pos < 0.f) pos = 0.f;
    if (pos > 1.f) pos = 1.f;

    float fi = pos * 7.f;
    int   i0 = (int)fi;
    if (i0 >= 7) i0 = 6;
    int   i1 = i0 + 1;
    float t  = fi - i0;

    u32 c0 = th->visualizer_bars[i0];
    u32 c1 = th->visualizer_bars[i1];
    u8 r0 = (c0 >> 0)  & 0xff;
    u8 g0 = (c0 >> 8)  & 0xff;
    u8 b0 = (c0 >> 16) & 0xff;
    u8 r1 = (c1 >> 0)  & 0xff;
    u8 g1 = (c1 >> 8)  & 0xff;
    u8 b1 = (c1 >> 16) & 0xff;

    *R = (u8)(r0 + (r1 - r0) * t);
    *G = (u8)(g0 + (g1 - g0) * t);
    *B = (u8)(b0 + (b1 - b0) * t);
}

static void draw_viz_fire(const PlayerUI *ui, float x, float y,
                          float w, float h)
{
    Theme *th = current_theme;
    float bw = w / FIRE_COLS - 1.f;

    for (int c = 0; c < FIRE_COLS; c++) {
        int band = c * EQ_BANDS / FIRE_COLS;
        float energy = ui->viz_smooth[band] * FIRE_BOOST;
        if (energy > 1.f) energy = 1.f;

        /* Update UNIQUEMENT sur oeil gauche en 3D (2 passes/frame sinon -> 2x trop vite) */
        if (g_3d_eye == 0) {
            /* Lissage : monte vite, descend lentement */
            float spd = (energy > s_fire_h[c]) ? 0.6f : 0.05f;
            s_fire_h[c] += (energy - s_fire_h[c]) * spd;

            /* Peak */
            if (s_fire_h[c] > s_fire_peak[c])
                s_fire_peak[c] = s_fire_h[c];
            else
                s_fire_peak[c] -= 0.003f;
            if (s_fire_peak[c] < 0.f) s_fire_peak[c] = 0.f;
        }

        float heat = s_fire_h[c];
        float bh = heat * h;
        if (bh < 2.f) bh = 2.f;
        float bx = x + c * (bw + 1.f);
        float by = y + h - bh;

        /* Position dans le degrade : heat / FIRE_MAX_HEAT
           Ainsi vis_end (pos=1.0) est atteint quand heat = FIRE_MAX_HEAT */
        float color_pos = heat / FIRE_MAX_HEAT;
        if (color_pos > 1.f) color_pos = 1.f;

        u8 R, G, B;
        fire_get_color(th, color_pos, &R, &G, &B);

        /* TOUTE la barre dans la meme couleur (effet feu classique) */
        draw_rect(bx, by, bw, bh, RGBA8(R, G, B, 240));

        /* Peak indicator blanc */
        if (s_fire_peak[c] > 0.05f) {
            float py = y + h - s_fire_peak[c] * h;
            draw_rect(bx, py, bw, 2.f, RGBA8(255, 255, 255, 220));
        }

        /* Reflet sol */
        draw_rect(bx, y + h, bw, bh * 0.12f, RGBA8(R, G, B, 60));
    }
    (void)th;
}

// ============================================================
// VISUALISEUR MODE "EQ" — 16 barres avec peak indicator
//
// Pour modifier :
//   VIZ_EQ_BARS         = nombre de barres
//   s_viz_eq_peak_vel  += X → vitesse chute peak (augmenter = tombe vite)
//   0.4f / 0.08f        = vitesse montée / descente barres
// ============================================================
#define VIZ_EQ_BARS 16

static float s_viz_eq_peak[VIZ_EQ_BARS]     = {0};
static float s_viz_eq_peak_vel[VIZ_EQ_BARS] = {0};
static float s_viz_eq_smooth[VIZ_EQ_BARS]   = {0};

static const char *s_viz_eq_labels[VIZ_EQ_BARS] = {
    "20","32","50","80","125","200","315","500",
    "800","1k","2k","3k","5k","8k","13k","20k"
};

static void draw_viz_eq(const PlayerUI *ui, float x, float y,
                        float w, float h)
{
    Theme *th = current_theme;

    // Interpolation 8 bandes → 16 barres
    // Update UNIQUEMENT sur oeil gauche en 3D (2 passes/frame sinon -> 2x trop vite)
    if (g_3d_eye == 0) {
        for (int i = 0; i < VIZ_EQ_BARS; i++) {
            float fi   = (float)i / (VIZ_EQ_BARS - 1) * (EQ_BANDS - 1);
            int   lo   = (int)fi;
            float frac = fi - lo;
            if (lo >= EQ_BANDS - 1) lo = EQ_BANDS - 2;
            float val  = ui->viz_smooth[lo] * (1.f - frac)
                       + ui->viz_smooth[lo+1] * frac;

            // Lissage : 0.4f = montée rapide, 0.08f = descente lente
            float spd = (val > s_viz_eq_smooth[i]) ? 0.4f : 0.08f;
            s_viz_eq_smooth[i] += (val - s_viz_eq_smooth[i]) * spd;

            // Peak indicator
            if (s_viz_eq_smooth[i] >= s_viz_eq_peak[i]) {
                s_viz_eq_peak[i]     = s_viz_eq_smooth[i];
                s_viz_eq_peak_vel[i] = 0.f;
            } else {
                s_viz_eq_peak_vel[i] += 0.002f; // ← vitesse de chute
                s_viz_eq_peak[i]     -= s_viz_eq_peak_vel[i];
                if (s_viz_eq_peak[i] < 0.f) s_viz_eq_peak[i] = 0.f;
            }
        }
    }

    float label_h = 10.f;
    float draw_h  = h - label_h;
    float bw      = w / VIZ_EQ_BARS - 1.f;

    for (int i = 0; i < VIZ_EQ_BARS; i++) {
        float bx = x + i * (bw + 1.f);

        // Couleur interpolée depuis le thème
        float ci    = (float)i / (VIZ_EQ_BARS - 1) * (EQ_BANDS - 1);
        int   clo   = (int)ci;
        float cfrac = ci - clo;
        if (clo >= EQ_BANDS - 1) clo = EQ_BANDS - 2;

        u32 c0 = th->visualizer_bars[clo];
        u32 c1 = th->visualizer_bars[clo + 1];
        u8 r = (u8)(((c0>>0)&0xff)*(1.f-cfrac) + ((c1>>0)&0xff)*cfrac);
        u8 g = (u8)(((c0>>8)&0xff)*(1.f-cfrac) + ((c1>>8)&0xff)*cfrac);
        u8 b = (u8)(((c0>>16)&0xff)*(1.f-cfrac) + ((c1>>16)&0xff)*cfrac);
        u32 vcol = RGBA8(r, g, b, 220);

        // Ligne de base
        draw_rect(bx, y + draw_h, bw + 1.f, 1.f, RGBA8(255,255,255,60));

        // Barre principale
        float vh = s_viz_eq_smooth[i] * draw_h;
        if (vh < 2.f) vh = 2.f;
        float vy = y + draw_h - vh;
        draw_rect(bx, vy, bw, vh, vcol);

        // Reflet sol (12% hauteur, très transparent)
        draw_rect(bx, y + draw_h, bw, vh * 0.12f, RGBA8(r, g, b, 50));

        // Peak indicator
        float pk_y = y + draw_h - s_viz_eq_peak[i] * draw_h;
        if (pk_y < y) pk_y = y;
        if (s_viz_eq_peak[i] > 0.05f && pk_y < vy - 2.f)
            draw_rect(bx, pk_y, bw, 2.f, RGBA8(255,255,255,140));

        // Label fréquence (1 sur 2)
        if (i % 2 == 0)
            draw_text(bx, y + draw_h + 1.f, 0.30f,
                      th->text_disabled, s_viz_eq_labels[i]);
    }
}


// ============================================================
// VISUALISEUR MODE "WAVEFORM" — Style Magnetophone / Dictaphone
// Les barres avancent de droite a gauche avec le volume general (RMS)
// ============================================================
// VISUALISEUR MODE "WAVEFORM" — Style Magnetophone & Degrade Theme
// ============================================================
#define WAVEFORM_BARS 90
/* VITESSE du defilement : nombre de barres decalees par mise a jour.
   1 = vitesse d origine.  2 = 2x plus vite.  3 = 3x plus vite.
   barres/seconde = WAVEFORM_BARS_SHIFT x (60 / WAVEFORM_SHIFT_INTERVAL)
   L amplitude reste celle du signal reel : seule la fenetre se raccourcit. */
#define WAVEFORM_BARS_SHIFT  2

static float s_waveform_history[WAVEFORM_BARS] = {0};

static void draw_viz_waveform(const PlayerUI *ui, float x, float y,
                              float w, float h)
{
    Theme *th = current_theme;
    (void)ui;
    
    /* 1. Ne decaler l'historique QUE si la musique tourne ! (Pause si pas en PLAYING) */
    /* Synchronisation fixe : 1 shift toutes les 2 frames (30Hz) ou 4 frames (60Hz en 3D) */
    static u32 s_wf_frame_counter = 0;
    s_wf_frame_counter++;
    
    bool do_shift = false;
    /* En 3D, cette fonction est appelee 2x/frame (oeil gauche + droit) : ne
       decaler que sur l'oeil gauche, sinon la fenetre defile 2x trop vite. */
    if (audio_get_state() == AUDIO_PLAYING && g_3d_eye == 0) {
        /* New 3DS = 60 FPS, Old 3DS = 30 FPS, 3D = 30 FPS par oeil */
        /* On shift a 15 Hz (toutes les 4 frames @ 60Hz, ou 2 frames @ 30Hz) */
        #define WAVEFORM_SHIFT_INTERVAL 1

        if (s_wf_frame_counter >= WAVEFORM_SHIFT_INTERVAL) {
            s_wf_frame_counter = 0;
            do_shift = true;
        }
    }
    if (do_shift) {
        float samples[256];
        int n = audio_get_oscillo(samples);
        
        float rms = 0.0f;
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                rms += samples[i] * samples[i];
            }
            rms = sqrtf(rms / n);
        }
        
        rms *= 1.65f;
        if (rms > 1.0f) rms = 1.0f;

        /* Decalage vers la gauche */
        for (int i = 0; i < WAVEFORM_BARS - WAVEFORM_BARS_SHIFT; i++) {
            s_waveform_history[i] = s_waveform_history[i + WAVEFORM_BARS_SHIFT];
        }
        /* garde : WAVEFORM_BARS_SHIFT ne doit pas depasser le tableau */
        #if (WAVEFORM_BARS_SHIFT < 1) || (WAVEFORM_BARS_SHIFT > (WAVEFORM_BARS / 2))
        #error "WAVEFORM_BARS_SHIFT doit etre entre 1 et WAVEFORM_BARS/2"
        #endif
        for (int k = 0; k < WAVEFORM_BARS_SHIFT; k++) {
            s_waveform_history[WAVEFORM_BARS - 1 - k] = rms;
        }
    }

    /* 2. Dessin des barres avec le degrade de couleurs du Theme */
    float bar_step = w / (float)WAVEFORM_BARS;
    float bar_w = bar_step - 1.0f;
    if (bar_w < 1.0f) bar_w = 1.0f;
    float cy = y + h / 2.0f;

    for (int i = 0; i < WAVEFORM_BARS; i++) {
        float val = s_waveform_history[i];
        
        float bar_h = val * (h * 0.82f);
        if (bar_h < 2.0f) bar_h = 2.0f; /* Hauteur mini (silence) */

        float bx = x + i * bar_step;
        float by = cy - bar_h / 2.0f;

        /* Calcul de la couleur du degrade selon la position de la barre (0 a 7) */
        int color_idx = (i * 8) / WAVEFORM_BARS;
        if (color_idx > 7) color_idx = 7;
        u32 bar_color = th ? th->visualizer_bars[color_idx] : RGBA8(0, 200, 255, 255);

        draw_rect(bx, by, bar_w, bar_h, bar_color);
    }
}

// ============================================================
// VISUALISEUR MODE "OSCILLOSCOPE" — style vrai oscillo scientifique
// V0.97 : ligne verte fine sur grille avec zero-crossing trigger
// ============================================================
static void draw_viz_oscillo(const PlayerUI *ui, float x, float y,
                              float w, float h)
{
    Theme *th = current_theme;
    (void)ui;
    (void)th;

    /* V0.97 v2 : version SAFE avec bornes strictes anti-freeze */
    float samples[256];
    int n = audio_get_oscillo(samples);
    if (n < 2 || n > 256) return;

    /* Grille en fond */
    u32 grid_col = RGBA8(40, 40, 60, 100);
    for (int i = 0; i <= 4; i++) {
        float ly = y + (h * i / 4.f);
        C2D_DrawRectSolid(x, ly, 0, w, 1, grid_col);
    }
    for (int i = 0; i <= 8; i++) {
        float lx = x + (w * i / 8.f);
        C2D_DrawRectSolid(lx, y, 0, 1, h, grid_col);
    }

    /* Ligne centrale */
    float cy = y + h / 2.f;
    C2D_DrawRectSolid(x, cy, 0, w, 1, RGBA8(80, 80, 100, 180));

    /* Zero-crossing trigger safe */
    int start_idx = 0;
    for (int i = 1; i < n - 1; i++) {
        if (samples[i-1] < 0.f && samples[i] >= 0.f) {
            start_idx = i;
            break;
        }
    }

    int display_n = n - start_idx;
    if (display_n < 32) {
        display_n = n;
        start_idx = 0;
    }
    if (display_n > 256) display_n = 256;
    if (start_idx < 0 || start_idx >= n) start_idx = 0;

    /* Couleur VIVE et VISIBLE : blanc/cyan flashy */
    u32 line_col = RGBA8(0, 255, 200, 255); /* cyan neon */

    /* Dessin trace : lignes reliees SAFE avec limite anti-freeze */
    float prev_px = -1.f, prev_py = -1.f;

    for (int i = 0; i < display_n; i++) {
        int si = start_idx + i;
        if (si >= n) break;

        float sample = samples[si];
        if (sample > 1.f) sample = 1.f;
        if (sample < -1.f) sample = -1.f;

        float px = x + (float)i * w / (float)display_n;
        float py = cy - sample * (h * 0.45f);

        if (py < y) py = y;
        if (py > y + h - 2) py = y + h - 2;
        if (px < x) px = x;
        if (px > x + w - 2) px = x + w - 2;

        /* Relier au point precedent avec une ligne SAFE */
        if (prev_px >= 0.f) {
            float dx = px - prev_px;
            float dy = py - prev_py;
            /* LIMITE ABSOLUE : max 20 sous-points par segment (anti-freeze) */
            int steps = (int)(fabsf(dy) / 2.f) + 1;
            if (steps > 20) steps = 20;
            if (steps < 1) steps = 1;

            for (int s = 0; s <= steps; s++) {
                float t = (float)s / (float)steps;
                float lx = prev_px + dx * t;
                float ly = prev_py + dy * t;
                C2D_DrawRectSolid(lx, ly, 0, 2, 2, line_col);
            }
        } else {
            C2D_DrawRectSolid(px, py, 0, 2, 2, line_col);
        }

        prev_px = px;
        prev_py = py;
    }
}

// ============================================================
// VISUALISEUR MODE "WAVES" — vagues (V4 lookup table sin, ultra rapide)
// ============================================================
#define WAVES_LUT_SIZE 128
static float s_waves_sin_lut[WAVES_LUT_SIZE];
static bool  s_waves_lut_init = false;

static void draw_viz_waves(const PlayerUI *ui, float x, float y,
                            float w, float h)
{
    Theme *th = current_theme;

    /* Init lookup table sin (calcule 1 seule fois) */
    if (!s_waves_lut_init) {
        for (int i = 0; i < WAVES_LUT_SIZE; i++)
            s_waves_sin_lut[i] = sinf((float)i * 2.f * M_PI / WAVES_LUT_SIZE);
        s_waves_lut_init = true;
    }

    /* V0.97 : Fond transparent avec trainee (comme Pulse) */
    draw_rect(x, y, w, h, RGBA8(0, 0, 15, 60));

    float cy = y + h / 2.f;

    /* Phase globale - vitesse variable selon volume */
    static int phase = 0;
    extern int g_3d_eye;
    if (g_3d_eye == 0) {
        /* Volume global moyen */
        float vol_g = 0.f;
        for (int b = 0; b < EQ_BANDS; b++) vol_g += ui->viz_smooth[b];
        vol_g /= EQ_BANDS;
        /* Vitesse : 2 (calme) a 6 (fort) */
        int step = 2 + (int)(vol_g * 4.f);
        phase = (phase + step) % WAVES_LUT_SIZE;
    }

    /* 4 vagues seulement */
    for (int layer = 0; layer < 4; layer++) {
        float energy = (ui->viz_smooth[layer*2] + ui->viz_smooth[layer*2+1]) * 0.5f;
        /* V0.97 : amplitude plus reactive (0.35 -> 0.6 + boost minimum) */
        float amp = h * 0.6f * energy;
        if (amp < 3.f) continue;

        int freq_step = 1 + layer * 2; /* pas dans la LUT */

        u32 col = th->visualizer_bars[layer * 2];
        u8 r = (col >> 0)  & 0xff;
        u8 g = (col >> 8)  & 0xff;
        u8 b = (col >> 16) & 0xff;
        u8 a = (u8)(100 + energy * 155);

        /* Draw tous les 6 px (encore moins) + LUT au lieu de sinf */
        int lut_pos = phase;
        for (int i = 0; i < (int)w; i += 6) {
            float wy = cy + s_waves_sin_lut[lut_pos % WAVES_LUT_SIZE] * amp;
            if (wy < y) wy = y;
            if (wy > y + h - 2) wy = y + h - 2;
            draw_rect(x + i, wy, 6, 3, RGBA8(r, g, b, a));
            lut_pos += freq_step;
        }
    }
}

// ============================================================
// VISUALISEUR MODE "TUNNEL" — V5 utilise la largeur (plus grande)
// ============================================================
static void draw_viz_tunnel(const PlayerUI *ui, float x, float y,
                             float w, float h)
{
    Theme *th = current_theme;

    /* V0.97 : Fond transparent avec trainee (comme Pulse) */
    draw_rect(x, y, w, h, RGBA8(0, 0, 15, 60));

    float cx = x + w/2.f;
    float cy = y + h/2.f;

    /* Rayon max : utilise la LARGEUR (plus grande) mais clamp Y au bord */
    float max_r = w / 2.f - 2;

    /* Volume moyen */
    float vol = 0.f;
    for (int i = 0; i < EQ_BANDS; i++) vol += ui->viz_smooth[i];
    vol /= EQ_BANDS;

    static float zoom = 0.f;
    extern int g_3d_eye;
    if (g_3d_eye == 0) {
        zoom += 0.03f + vol * 0.2f;
        if (zoom > 1.f) zoom -= 1.f;
    }

    /* 10 anneaux (plus rapproches = effet tunnel plus prononce) */
    for (int ring = 0; ring < 10; ring++) {
        float t = ((float)ring / 10.f + zoom);
        if (t > 1.f) t -= 1.f;

        float r = t * max_r;
        if (r < 8.f) continue;

        int band = ring % EQ_BANDS;
        float band_energy = ui->viz_smooth[band];

        u32 col = th->visualizer_bars[band];
        u8 rc = (col >> 0)  & 0xff;
        u8 gc = (col >> 8)  & 0xff;
        u8 bc = (col >> 16) & 0xff;

        float fade = 1.f - fabsf(t - 0.5f) * 1.5f;
        if (fade < 0.f) fade = 0.f;
        u8 a = (u8)(255 * fade * (0.5f + band_energy));

        /* Ratio pour aplatir en Y (evite depassement vertical) */
        float y_scale = h / w;

        int segs = (int)(r * 0.4f);
        if (segs < 20) segs = 20;
        if (segs > 60) segs = 60;

        float size = 3.f + (1.f - t) * 4.f + band_energy * 3.f;

        for (int s = 0; s < segs; s++) {
            float ang = (float)s * 2.f * M_PI / segs;
            float px = cx + cosf(ang) * r;
            float py = cy + sinf(ang) * r * y_scale; /* aplati verticalement */

            if (px < x || px > x + w - size
                || py < y || py > y + h - size) continue;

            draw_rect(px - size/2, py - size/2, size, size,
                      RGBA8(rc, gc, bc, a));
        }
    }

    /* Point central */
    float ch_sz = 4.f + vol * 8.f;
    draw_rect(cx - ch_sz/2, cy - ch_sz/2, ch_sz, ch_sz,
              RGBA8(255, 255, 255, 255));
}

// ============================================================
// VISUALISEUR MODE "PULSE" — V5 detection beat sur samples bruts
// (utilise le buffer oscillo qui est mis a jour a chaque frame audio)
// ============================================================
#define PULSE_PARTICLES 60
static struct {
    float x, y, vx, vy, life, size;
    u8 r, g, b;
} s_pulse_p[PULSE_PARTICLES];
static bool  s_pulse_init = false;

/* Detection beat sur RMS des samples bruts (60Hz -> vraie detection) */
static float s_pulse_rms_history[32] = {0};  /* 32 dernieres frames */
static int   s_pulse_rms_pos = 0;
static int   s_pulse_cooldown = 0;

static void draw_viz_pulse(const PlayerUI *ui, float x, float y,
                            float w, float h)
{
    Theme *th = current_theme;

    if (!s_pulse_init) {
        for (int i = 0; i < PULSE_PARTICLES; i++) s_pulse_p[i].life = 0.f;
        for (int i = 0; i < 32; i++) s_pulse_rms_history[i] = 0.f;
        s_pulse_init = true;
    }

    /* V0.97 : Fond noir tres leger pour trainee (mais quasi transparent) */
    draw_rect(x, y, w, h, RGBA8(0, 0, 15, 25));

    float cx = x + w/2.f;
    float cy = y + h/2.f;

    /* ═══ VRAIE DETECTION BEAT sur samples bruts (60fps) ═══ */
    float oscillo[256];
    int n = audio_get_oscillo(oscillo);

    /* Calculer RMS uniquement sur les basses (filtre passe-bas simplifie) */
    float rms = 0.f;
    if (n > 4) {
        /* Moyenne mobile 4 samples = filtre passe-bas -> extrait les basses */
        for (int i = 0; i < n - 4; i++) {
            float low = (oscillo[i] + oscillo[i+1] + oscillo[i+2] + oscillo[i+3]) * 0.25f;
            rms += low * low;
        }
        rms = sqrtf(rms / (n - 4));
    }

    /* Historique RMS pour detecter les pics (oeil gauche seulement en 3D,
       sinon la fenetre de ~0.5s est divisee par 2) */
    if (g_3d_eye == 0) {
        s_pulse_rms_history[s_pulse_rms_pos] = rms;
        s_pulse_rms_pos = (s_pulse_rms_pos + 1) % 32;
    }

    /* Moyenne sur derniers 32 frames (~0.5 sec) */
    float rms_avg = 0.f;
    for (int i = 0; i < 32; i++) rms_avg += s_pulse_rms_history[i];
    rms_avg /= 32.f;

    /* Beat = RMS actuel > 1.3 x moyenne */
    bool is_beat = false;
    if (g_3d_eye == 0 && s_pulse_cooldown > 0) s_pulse_cooldown--;
    if (g_3d_eye == 0 && rms > rms_avg * 1.22f && rms > 0.035f && s_pulse_cooldown == 0) {
        is_beat = true;
        s_pulse_cooldown = 6; /* min 100ms entre beats */
    }

    /* SPAWN particules sur beat */
    if (is_beat) {
        int spawned = 0;
        for (int i = 0; i < PULSE_PARTICLES && spawned < 20; i++) {
            if (s_pulse_p[i].life <= 0.f) {
                float ang = (float)(rand() % 3600) * M_PI / 1800.f;
                float speed = 3.f + (float)(rand() % 50) / 10.f;
                s_pulse_p[i].x = cx;
                s_pulse_p[i].y = cy;
                s_pulse_p[i].vx = cosf(ang) * speed;
                s_pulse_p[i].vy = sinf(ang) * speed;
                s_pulse_p[i].life = 1.f;
                s_pulse_p[i].size = 5.f + (float)(rand() % 40) / 10.f;
                if (rms > rms_avg * 1.65f) {
                    /* Gros beat = rouge/orange */
                    s_pulse_p[i].r = 255;
                    s_pulse_p[i].g = 100 + rand() % 155;
                    s_pulse_p[i].b = 30 + rand() % 50;
                } else {
                    u32 col = th->visualizer_bars[rand() % EQ_BANDS];
                    s_pulse_p[i].r = (col >> 0)  & 0xff;
                    s_pulse_p[i].g = (col >> 8)  & 0xff;
                    s_pulse_p[i].b = (col >> 16) & 0xff;
                }
                spawned++;
            }
        }
    }

    /* Update particules SEULEMENT sur oeil gauche pour 3D */
    extern int g_3d_eye;
    bool do_update = (g_3d_eye == 0);
    for (int i = 0; i < PULSE_PARTICLES; i++) {
        if (s_pulse_p[i].life <= 0.f) continue;
        if (do_update) {
            s_pulse_p[i].x += s_pulse_p[i].vx;
            s_pulse_p[i].y += s_pulse_p[i].vy;
            s_pulse_p[i].vx *= 0.96f;
            s_pulse_p[i].vy *= 0.96f;
            s_pulse_p[i].life -= 0.02f;
        }

        if (s_pulse_p[i].x < x || s_pulse_p[i].x > x + w
            || s_pulse_p[i].y < y || s_pulse_p[i].y > y + h
            || s_pulse_p[i].life <= 0.f) {
            s_pulse_p[i].life = 0;
            continue;
        }

        float sz = s_pulse_p[i].size * s_pulse_p[i].life;
        u8 a = (u8)(s_pulse_p[i].life * 255);
        draw_rect(s_pulse_p[i].x - sz/2, s_pulse_p[i].y - sz/2, sz, sz,
                  RGBA8(s_pulse_p[i].r, s_pulse_p[i].g, s_pulse_p[i].b, a));
    }

    /* Coeur central toujours visible - pulse avec RMS */
    float heart_sz = 12.f + rms * 60.f;
    if (heart_sz > 50.f) heart_sz = 50.f;
    u32 heart_col = is_beat ? RGBA8(255,255,255,255)
                            : (rms > rms_avg * 1.1f ? th->accent2 : th->accent);
    draw_rect(cx - heart_sz/2, cy - heart_sz/2, heart_sz, heart_sz, heart_col);

    /* Halo permanent autour */
    float halo_sz = heart_sz + 10.f;
    u8 halo_a = (u8)(60 + rms * 200);
    if (halo_a > 200) halo_a = 200;
    draw_rect(cx - halo_sz/2, cy - halo_sz/2, halo_sz, halo_sz,
              RGBA8((heart_col>>0)&0xff, (heart_col>>8)&0xff,
                    (heart_col>>16)&0xff, halo_a));
}

// ============================================================
// VISUALISEUR MODE "BARRES" — 8 barres simples
// ============================================================
static void draw_viz_bars(const PlayerUI *ui, float x, float y,
                          float w, float h)
{
    Theme *th = current_theme;
    float bw  = w / EQ_BANDS - 2;

    for (int i = 0; i < EQ_BANDS; i++) {
        float bh = ui->viz_smooth[i] * h;
        if (bh < 2) bh = 2;
        float bx = x + i * (bw + 2);
        float by = y + h - bh;
        draw_rect(bx, by, bw, bh, th->visualizer_bars[i]);
        // Reflet sol (30% hauteur, transparent)
        draw_rect(bx, y+h, bw, bh*0.3f,
            RGBA8((th->visualizer_bars[i]>>0)&0xff,
                  (th->visualizer_bars[i]>>8)&0xff,
                  (th->visualizer_bars[i]>>16)&0xff, 40));
    }
}

// ============================================================
// VISUALISEUR MODE "ONDE"
// ============================================================
static void draw_viz_wave(const PlayerUI *ui, float x, float y,
                          float w, float h)
{
    Theme *th = current_theme;
    float cy = y + h/2.f;

    for (int i = 0; i < EQ_BANDS-1; i++) {
        float ax  = x + i     * (w/(EQ_BANDS-1));
        float ay  = cy - ui->viz_smooth[i]   * (h/2.f);
        float bx  = x + (i+1) * (w/(EQ_BANDS-1));
        float by2 = cy - ui->viz_smooth[i+1] * (h/2.f);
        float dx  = bx-ax, dy2 = by2-ay;
        float len = sqrtf(dx*dx + dy2*dy2);
        int segs  = (int)(len/2)+1;
        for (int s = 0; s < segs; s++) {
            float t  = (float)s/segs;
            float px = ax + dx*t;
            float py = ay + dy2*t;
            draw_rect(px-1, py-1, 3, 3, th->visualizer_bars[i]);
        }
    }
}

// ============================================================
// VISUALISEUR MODE "CERCLE"
// ============================================================
static void draw_viz_circle(const PlayerUI *ui, float cx, float cy, float r)
{
    Theme *th = current_theme;
    int segs  = EQ_BANDS * 4;

    for (int i = 0; i < segs; i++) {
        float angle = (float)i / segs * 2.f * M_PI;
        int   band  = i * EQ_BANDS / segs;
        // 0.5f = rayon minimum, 0.8f = amplitude max
        float len = r*0.5f + ui->viz_smooth[band] * r*0.8f;
        float x1 = cx + cosf(angle)*r*0.3f;
        float y1 = cy + sinf(angle)*r*0.3f;
        float x2 = cx + cosf(angle)*len;
        float y2 = cy + sinf(angle)*len;
        float dx = x2-x1, dy = y2-y1;
        float dist = sqrtf(dx*dx+dy*dy);
        int steps = (int)(dist/2)+1;
        for (int s = 0; s < steps; s++) {
            float t = (float)s/steps;
            draw_rect(x1+dx*t-1, y1+dy*t-1, 2, 2,
                th->visualizer_bars[band % EQ_BANDS]);
        }
    }
    draw_rect(cx-3, cy-3, 6, 6, th->accent);
}

// ============================================================
// POCHETTE PLACEHOLDER
// ============================================================
static void draw_cover_placeholder(float x, float y, float sz)
{
    Theme *th = current_theme;
    draw_rect(x, y, sz, sz, th->bg_secondary);
    draw_rect(x,      y,      sz, 2,  th->border);
    draw_rect(x,      y+sz-2, sz, 2,  th->border);
    draw_rect(x,      y,      2,  sz, th->border);
    draw_rect(x+sz-2, y,      2,  sz, th->border);
    float cx = x+sz/2.f, cy = y+sz/2.f;
    u32 nc = th->text_disabled;

    /* Barre du haut : relie les 2 tiges sur toute la largeur */
    draw_rect(cx-6,  cy-12, 16, 4,  nc);
    /* Tige gauche */
    draw_rect(cx-6,  cy-12, 4,  20, nc);
    /* Tige droite */
    draw_rect(cx+6,  cy-12, 4,  20, nc);
    /* Tête gauche (à gauche de sa tige) */
    draw_rect(cx-10, cy+4,  8,  6,  nc);
    /* Tête droite (à gauche de sa tige) */
    draw_rect(cx+2,  cy+4,  8,  6,  nc);
}

// ============================================================
// INIT / LOAD / UPDATE / EXIT
// ============================================================

#include <dirent.h>
#include <3ds/srv.h> /* VIZ15d */

/* ============================================================================
 * 🛠️ UTILS EASTER EGGS (Recherche texte & Chargement fichier)
 * ============================================================================ */
static bool contains_icase(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return false;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, nlen) == 0) return true;
    }
    return false;
}

static u8* load_file_bytes(const char *path, size_t *out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    u8 *buf = (u8*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (out_sz) *out_sz = (size_t)sz;
    return buf;
}

/* ============================================================================
 * 💀 1. EASTER EGG: SANS (UNDERTALE)
 * ============================================================================ */
static C3D_Tex           s_sans_tex;
static Tex3DS_SubTexture s_sans_subtex;
static C2D_Image         s_sans_img;
static bool              s_sans_tex_inited  = false;
static bool              s_sans_load_failed = false;

/* Détection de la présence du mot "Undertale" */

#include <ctype.h>

static bool contains_ic_str(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    char h[256], n[64]; int i = 0;
    for (; haystack[i] && i < 255; i++) h[i] = (char)tolower((unsigned char)haystack[i]);
    h[i] = 0;
    for (i = 0; needle[i] && i < 63; i++) n[i] = (char)tolower((unsigned char)needle[i]);
    n[i] = 0;
    return strstr(h, n) != NULL;
}

/* Chargeur Simple */
static bool load_single_png_to_img(const char *path, C3D_Tex *tex, C2D_Image *img, Tex3DS_SubTexture *subtex) {
    size_t fsz = 0;
    u8 *fbuf = load_file_bytes(path, &fsz);
    if (!fbuf) return false;
    int w=0, h=0, ch=0;
    u8 *px = stbi_load_from_memory((const stbi_uc*)fbuf, (int)fsz, &w, &h, &ch, 4);
    free(fbuf);
    if (!px || w <= 0 || h <= 0) return false;

    u32 tw = npow2((u32)w), th = npow2((u32)h);
    if (!C3D_TexInit(tex, (u16)tw, (u16)th, GPU_RGBA8)) {
        stbi_image_free(px);
        return false;
    }
    u8 *dst = (u8*)tex->data;
    memset(dst, 0, tw * th * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u32 tile = ((u32)y/8)*(tw/8) + ((u32)x/8);
            u32 mi   = morton((u32)x&7, (u32)y&7);
            u32 di   = (tile*64 + mi) * 4;
            const u8 *s = px + (y*w + x) * 4;
            dst[di+0] = s[3]; dst[di+1] = s[2];
            dst[di+2] = s[1]; dst[di+3] = s[0];
        }
    }
    stbi_image_free(px);
    GSPGPU_FlushDataCache(dst, tw * th * 4);
    C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
    subtex->width  = (u16)w;  subtex->height = (u16)h;
    subtex->left   = 0.f;     subtex->top    = 1.f;
    subtex->right  = (float)w / (float)tw;
    subtex->bottom = 1.f - (float)h / (float)th;
    img->tex = tex; img->subtex = subtex;
    return true;
}

/* Chargeur Double (Normale + Miroir Inversé en RAM) */
static bool load_single_png_pair(const char *path, 
                                 C3D_Tex *tex, C2D_Image *img, Tex3DS_SubTexture *subtex,
                                 C3D_Tex *tex_f, C2D_Image *img_f, Tex3DS_SubTexture *subtex_f) {
    size_t fsz = 0;
    u8 *fbuf = load_file_bytes(path, &fsz);
    if (!fbuf) return false;
    int w=0, h=0, ch=0;
    u8 *px = stbi_load_from_memory((const stbi_uc*)fbuf, (int)fsz, &w, &h, &ch, 4);
    free(fbuf);
    if (!px || w <= 0 || h <= 0) return false;

    u8 *px_flip = (u8*)malloc(w * h * 4);
    if (!px_flip) { stbi_image_free(px); return false; }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int src = (y * w + (w - 1 - x)) * 4;
            int dst = (y * w + x) * 4;
            px_flip[dst+0] = px[src+0];
            px_flip[dst+1] = px[src+1];
            px_flip[dst+2] = px[src+2];
            px_flip[dst+3] = px[src+3];
        }
    }

    u32 tw = npow2((u32)w), th = npow2((u32)h);

    /* 1. Texture Normale */
    if (!C3D_TexInit(tex, (u16)tw, (u16)th, GPU_RGBA8)) {
        stbi_image_free(px); free(px_flip); return false;
    }
    u8 *dst = (u8*)tex->data;
    memset(dst, 0, tw * th * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u32 tile = ((u32)y/8)*(tw/8) + ((u32)x/8);
            u32 mi   = morton((u32)x&7, (u32)y&7);
            u32 di   = (tile*64 + mi) * 4;
            const u8 *s = px + (y*w + x) * 4;
            dst[di+0] = s[3]; dst[di+1] = s[2];
            dst[di+2] = s[1]; dst[di+3] = s[0];
        }
    }
    GSPGPU_FlushDataCache(dst, tw * th * 4);
    C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
    subtex->width  = (u16)w;  subtex->height = (u16)h;
    subtex->left   = 0.f;     subtex->top    = 1.f;
    subtex->right  = (float)w / (float)tw;
    subtex->bottom = 1.f - (float)h / (float)th;
    img->tex = tex; img->subtex = subtex;

    /* 2. Texture Miroir RAM */
    if (!C3D_TexInit(tex_f, (u16)tw, (u16)th, GPU_RGBA8)) {
        /* La 1ere texture (tex) a deja ete initialisee avec succes juste
           au-dessus : sans ce C3D_TexDelete, elle restait allouee pour rien
           a chaque echec de la 2e (ex: VRAM/lineaire tres contrainte). */
        C3D_TexDelete(tex);
        stbi_image_free(px); free(px_flip); return false;
    }
    u8 *dst_f = (u8*)tex_f->data;
    memset(dst_f, 0, tw * th * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u32 tile = ((u32)y/8)*(tw/8) + ((u32)x/8);
            u32 mi   = morton((u32)x&7, (u32)y&7);
            u32 di   = (tile*64 + mi) * 4;
            const u8 *s = px_flip + (y*w + x) * 4;
            dst_f[di+0] = s[3]; dst_f[di+1] = s[2];
            dst_f[di+2] = s[1]; dst_f[di+3] = s[0];
        }
    }
    GSPGPU_FlushDataCache(dst_f, tw * th * 4);
    C3D_TexSetFilter(tex_f, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(tex_f, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
    subtex_f->width  = (u16)w;  subtex_f->height = (u16)h;
    subtex_f->left   = 0.f;     subtex_f->top    = 1.f;
    subtex_f->right  = (float)w / (float)tw;
    subtex_f->bottom = 1.f - (float)h / (float)th;
    img_f->tex = tex_f; img_f->subtex = subtex_f;

    stbi_image_free(px);
    free(px_flip);
    return true;
}

/* ============================================================ */
/* 💡 EASTER EGG : ONESHOT (L'AMPOULE DE NIKO)                 */
/* ============================================================ */
static C3D_Tex s_os_on_tex, s_os_off_tex;
static Tex3DS_SubTexture s_os_on_subtex, s_os_off_subtex;
static C2D_Image s_os_on_img, s_os_off_img;
static bool s_os_inited = false, s_os_failed = false;

static bool is_oneshot_track(const AudioMetadata *meta) {
    if (!g_settings.easter_eggs_enabled || !meta) return false;
    if (strstr(meta->title, "OneShot") || strstr(meta->artist, "OneShot") || strstr(meta->album, "OneShot")) return true;
    return false;
}

static void load_oneshot_pngs(void) {
    if (s_os_inited || s_os_failed) return;
    bool ok1 = load_single_png_to_img("romfs:/esteregg/oneshot/lampe_alume.png", &s_os_on_tex, &s_os_on_img, &s_os_on_subtex);
    if (!ok1) ok1 = load_single_png_to_img("romfs:/esteregg/oneshot/lampe_allume.png", &s_os_on_tex, &s_os_on_img, &s_os_on_subtex);

    bool ok2 = load_single_png_to_img("romfs:/esteregg/oneshot/lampe_eteinte.png", &s_os_off_tex, &s_os_off_img, &s_os_off_subtex);
    if (!ok2) ok2 = load_single_png_to_img("romfs:/esteregg/oneshot/lampe_etein te.png", &s_os_off_tex, &s_os_off_img, &s_os_off_subtex);

    if (ok1 && ok2) s_os_inited = true;
    else s_os_failed = true;
}

static void draw_oneshot_bulb(float x, float y) {
    load_oneshot_pngs();
    if (!s_os_inited) return;

    bool playing = (audio_get_state() == AUDIO_PLAYING);
    C2D_Image *img = playing ? &s_os_on_img : &s_os_off_img;
    C2D_DrawImageAt(*img, x, y, 0.95f, NULL, 0.55f, 0.55f);
}

/* ============================================================ */
/* 🎀 EASTER EGG : DOKI DOKI LITERATURE CLUB (CHIBIS & MONIKA) */
/* ============================================================ */
static C3D_Tex s_ddlc_tex[4], s_ddlc_tex_flip[4];
static Tex3DS_SubTexture s_ddlc_subtex[4], s_ddlc_subtex_flip[4];
static C2D_Image s_ddlc_img[4], s_ddlc_img_flip[4];

/* Texture spéciale Monika Tête à l'envers (pour le bord Haut) */
static C3D_Tex s_monika_tex_180;
static Tex3DS_SubTexture s_monika_subtex_180;
static C2D_Image s_monika_img_180;

static bool s_ddlc_inited = false, s_ddlc_failed = false;
static bool s_ddlc_current_track_active = false;

static bool is_ddlc_track(const AudioMetadata *meta) {
    if (!g_settings.easter_eggs_enabled || !meta) {
        s_ddlc_current_track_active = false;
        return false;
    }
    const char *kw[] = { "dokidoki", "doki doki", "ddlc", "doki" };
    for (int i = 0; i < 4; i++) {
        if (contains_ic_str(meta->title, kw[i]) || contains_ic_str(meta->artist, kw[i]) || contains_ic_str(meta->album, kw[i])) {
            s_ddlc_current_track_active = true;
            return true;
        }
    }
    s_ddlc_current_track_active = false;
    return false;
}

static void load_ddlc_pngs(void) {
    if (s_ddlc_inited || s_ddlc_failed) return;
    const char *paths[4] = {
        "romfs:/esteregg/doki_doki/sayori.png",
        "romfs:/esteregg/doki_doki/natsuki.png",
        "romfs:/esteregg/doki_doki/yuri.png",
        "romfs:/esteregg/doki_doki/monika.png"
    };
    int loaded = 0;
    for (int i = 0; i < 4; i++) {
        if (load_single_png_pair(paths[i], 
                                 &s_ddlc_tex[i], &s_ddlc_img[i], &s_ddlc_subtex[i],
                                 &s_ddlc_tex_flip[i], &s_ddlc_img_flip[i], &s_ddlc_subtex_flip[i])) {
            loaded++;
        }
    }

    /* Génération de Monika 180° (Tête à l'envers pour le haut de l'écran) */
    if (loaded == 4) {
        size_t fsz = 0;
        u8 *fbuf = load_file_bytes("romfs:/esteregg/doki_doki/monika.png", &fsz);
        if (fbuf) {
            int w=0, h=0, ch=0;
            u8 *px = stbi_load_from_memory((const stbi_uc*)fbuf, (int)fsz, &w, &h, &ch, 4);
            free(fbuf);
            if (px && w > 0 && h > 0) {
                u8 *px_180 = (u8*)malloc(w * h * 4);
                if (px_180) {
                    for (int y = 0; y < h; y++) {
                        for (int x = 0; x < w; x++) {
                            int src = (y * w + x) * 4;
                            int dst = ((h - 1 - y) * w + (w - 1 - x)) * 4;
                            px_180[dst+0] = px[src+0];
                            px_180[dst+1] = px[src+1];
                            px_180[dst+2] = px[src+2];
                            px_180[dst+3] = px[src+3];
                        }
                    }
                    u32 tw = npow2((u32)w), th = npow2((u32)h);
                    if (C3D_TexInit(&s_monika_tex_180, (u16)tw, (u16)th, GPU_RGBA8)) {
                        u8 *dst_180 = (u8*)s_monika_tex_180.data;
                        memset(dst_180, 0, tw * th * 4);
                        for (int y = 0; y < h; y++) {
                            for (int x = 0; x < w; x++) {
                                u32 tile = ((u32)y/8)*(tw/8) + ((u32)x/8);
                                u32 mi   = morton((u32)x&7, (u32)y&7);
                                u32 di   = (tile*64 + mi) * 4;
                                const u8 *s = px_180 + (y*w + x) * 4;
                                dst_180[di+0] = s[3]; dst_180[di+1] = s[2];
                                dst_180[di+2] = s[1]; dst_180[di+3] = s[0];
                            }
                        }
                        GSPGPU_FlushDataCache(dst_180, tw * th * 4);
                        C3D_TexSetFilter(&s_monika_tex_180, GPU_NEAREST, GPU_NEAREST);
                        C3D_TexSetWrap(&s_monika_tex_180, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
                        s_monika_subtex_180.width  = (u16)w;  s_monika_subtex_180.height = (u16)h;
                        s_monika_subtex_180.left   = 0.f;     s_monika_subtex_180.top    = 1.f;
                        s_monika_subtex_180.right  = (float)w / (float)tw;
                        s_monika_subtex_180.bottom = 1.f - (float)h / (float)th;
                        s_monika_img_180.tex = &s_monika_tex_180; s_monika_img_180.subtex = &s_monika_subtex_180;
                    }
                    free(px_180);
                }
                stbi_image_free(px);
            }
        }
        s_ddlc_inited = true;
    } else {
        s_ddlc_failed = true;
    }
}

static void draw_ddlc_chibis_top(float base_x, float base_y) {
    load_ddlc_pngs();
    if (!s_ddlc_inited) return;

    static int jumping_chibi = -1;
    static u64 jump_start_time = 0;
    static u64 next_jump_time = 0;
    static bool chibi_flip[4] = {false, false, false, false};

    u64 now = osGetTime();
    /* g_3d_eye : en 3D cette fonction est appelee 2x/image (oeil gauche puis
       droit, voir main.c). Sans ce garde, le choix du chibi qui saute
       (rand()) et la fin du saut pouvaient se decider une 2e fois sur le
       meme instant si les deux passes tombaient juste autour d une
       transition, menant a un chibi different entre l oeil gauche et le
       droit (desynchronisation 3D) - meme classe de bug deja corrigee
       ailleurs dans ce fichier pour les autres visualiseurs. */
    if (g_3d_eye == 0) {
        if (next_jump_time == 0) next_jump_time = now + 1000ULL;

        if (jumping_chibi == -1 && now >= next_jump_time) {
            jumping_chibi = rand() % 4;
            jump_start_time = now;
            chibi_flip[jumping_chibi] = !chibi_flip[jumping_chibi]; /* Inversion demi-tour */
            next_jump_time = now + 800ULL + (u64)(rand() % 1200);
        }
    }

    /* Coordonnées X FIXES (espacement parfait de 36px) */
    float fixed_x[4] = { base_x, base_x + 36.f, base_x + 72.f, base_x + 108.f };

    for (int i = 0; i < 4; i++) {
        if (s_ddlc_subtex[i].height == 0) continue;

        float target_h = 38.0f;
        float scale = target_h / (float)s_ddlc_subtex[i].height;
        float disp_h = target_h;

        float dy = 0.f;
        if (jumping_chibi == i) {
            float elapsed = (float)(now - jump_start_time) / 1000.f;
            float duration = 0.35f;
            if (elapsed <= duration) {
                float progress = elapsed / duration;
                dy = sinf(progress * 3.14159265f) * 16.0f; /* Saut 16px */
            } else if (g_3d_eye == 0) {
                jumping_chibi = -1;
            }
        }

        float cx = fixed_x[i];
        float cy = base_y - disp_h - dy;

        /* Choix direct entre l'image normale ou l'image inversée en RAM (position cx identique !) */
        C2D_Image *img_to_draw = chibi_flip[i] ? &s_ddlc_img_flip[i] : &s_ddlc_img[i];

        float ddlc_3d = d3(1.5f);
        C2D_DrawImageAt(*img_to_draw, cx + ddlc_3d, cy, 0.98f, NULL, scale, scale);
    }
}

/* 👁️ JUST MONIKA STALKER (4 Bords + Mode Tête à l'Envers) */
void ddlc_draw_monika_stalker(void) {
    if (!g_settings.easter_eggs_enabled || !s_ddlc_current_track_active) return;
    load_ddlc_pngs();
    if (!s_ddlc_inited) return;

    static u64 next_stalk_time = 0;
    static u64 stalk_start_time = 0;
    static bool is_stalking = false;
    static bool is_scared = false;
    static float scared_start_time = 0.f;
    static float scared_x = 0.f, scared_y = 0.f;
    static int side = 0;
    static float start_x = 0.f, start_y = 0.f;
    static float target_x = 0.f, target_y = 0.f;

    u64 now = osGetTime();
    if (next_stalk_time == 0) next_stalk_time = now + 3000ULL;

    if (!is_stalking && now >= next_stalk_time) {
        is_stalking = true;
        is_scared = false;
        stalk_start_time = now;
        next_stalk_time = now + 12000ULL + (u64)(rand() % 15000);

        side = rand() % 4;
        float scale = 0.16f;
        float disp_w = (float)s_ddlc_subtex[3].width * scale;
        float disp_h = (float)s_ddlc_subtex[3].height * scale;

        switch (side) {
            case 0:
                start_x  = 10.f + (float)(rand() % (int)(300.f - disp_w));
                target_x = start_x;
                start_y  = 240.f;
                target_y = 240.f - disp_h + 10.f;
                break;
            case 1:
                start_x  = 10.f + (float)(rand() % (int)(300.f - disp_w));
                target_x = start_x;
                start_y  = -disp_h;
                target_y = -10.f;
                break;
            case 2:
                target_y = 20.f + (float)(rand() % (int)(200.f - disp_h));
                start_y  = target_y;
                start_x  = -disp_w;
                target_x = -10.f;
                break;
            default:
                target_y = 20.f + (float)(rand() % (int)(200.f - disp_h));
                start_y  = target_y;
                start_x  = 320.f;
                target_x = 320.f - disp_w + 10.f;
                break;
        }
    }

    if (is_stalking) {
        float elapsed = (float)(now - stalk_start_time) / 1000.f;
        float t_in   = 0.35f;
        float t_stay = 2.00f;
        float t_out  = 0.35f;
        float total  = t_in + t_stay + t_out;
        float scale  = 0.16f;

        float disp_w = (float)s_ddlc_subtex[3].width * scale;
        float disp_h = (float)s_ddlc_subtex[3].height * scale;

        /* DÉTECTION TACTILE : Toucher Monika avec le stylet / doigt ! */
        touchPosition touch;
        hidTouchRead(&touch);
        if (!is_scared && touch.px > 0 && touch.py > 0) {
            /* Position actuelle de Monika */
            float cur_x = target_x, cur_y = target_y;
            if (touch.px >= (cur_x - 10.f) && touch.px <= (cur_x + disp_w + 10.f) &&
                touch.py >= (cur_y - 10.f) && touch.py <= (cur_y + disp_h + 10.f)) {
                is_scared = true;
                scared_start_time = elapsed;
                scared_x = cur_x;
                scared_y = cur_y;
            }
        }

        if (is_scared) {
            float scared_elapsed = elapsed - scared_start_time;
            float scare_duration = 0.15f; /* Fuite éclair en 150ms ! */
            if (scared_elapsed <= scare_duration) {
                float p = scared_elapsed / scare_duration;
                float x = scared_x + (start_x - scared_x) * p;
                float y = scared_y + (start_y - scared_y) * p;
                C2D_Image *img = (side == 1) ? &s_monika_img_180 : ((side == 3) ? &s_ddlc_img_flip[3] : &s_ddlc_img[3]);
                C2D_DrawImageAt(*img, x, y, 0.99f, NULL, scale, scale);
            } else {
                is_stalking = false;
            }
            return;
        }

        if (elapsed <= total) {
            float x, y;
            if (elapsed < t_in) {
                float p = elapsed / t_in;
                p = 1.f - (1.f - p) * (1.f - p);
                x = start_x + (target_x - start_x) * p;
                y = start_y + (target_y - start_y) * p;
            } else if (elapsed < t_in + t_stay) {
                x = target_x;
                y = target_y;
            } else {
                float p = (elapsed - t_in - t_stay) / t_out;
                p = p * p;
                x = target_x + (start_x - target_x) * p;
                y = target_y + (start_y - target_y) * p;
            }

            C2D_Image *img = (side == 1) ? &s_monika_img_180 : ((side == 3) ? &s_ddlc_img_flip[3] : &s_ddlc_img[3]);
            C2D_DrawImageAt(*img, x, y, 0.99f, NULL, scale, scale);
        } else {
            is_stalking = false;
        }
    }
}


static bool is_undertale_track(const AudioMetadata *meta) {
    if (!g_settings.easter_eggs_enabled || !meta) return false;
    if (meta->title[0]  && contains_icase(meta->title,  "undertale")) return true;
    if (meta->artist[0] && contains_icase(meta->artist, "undertale")) return true;
    if (meta->album[0]  && contains_icase(meta->album,  "undertale")) return true;
    return false;
}

/* Chargement GPU de l'image de Sans */
static void load_sans_png(void) {
    if (s_sans_tex_inited || s_sans_load_failed) return;
    size_t fsz = 0;
    u8 *fbuf = load_file_bytes("romfs:/esteregg/sans.png", &fsz);
    if (!fbuf) { s_sans_load_failed = true; return; }
    int w=0,h=0,ch=0;
    u8 *px = stbi_load_from_memory(fbuf,(int)fsz,&w,&h,&ch,4);
    free(fbuf);
    if (!px) { s_sans_load_failed = true; return; }
    u32 tw = npow2((u32)w), th = npow2((u32)h);
    if (!C3D_TexInit(&s_sans_tex,(u16)tw,(u16)th,GPU_RGBA8)) { stbi_image_free(px); s_sans_load_failed=true; return; }
    u8 *dst = (u8*)s_sans_tex.data; memset(dst,0,tw*th*4);
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        u32 tile=((u32)y/8)*(tw/8)+((u32)x/8), mi=morton((u32)x&7,(u32)y&7), di=(tile*64+mi)*4;
        const u8 *s = px+(y*w+x)*4;
        dst[di]=s[3]; dst[di+1]=s[2]; dst[di+2]=s[1]; dst[di+3]=s[0];
    }
    stbi_image_free(px);
    GSPGPU_FlushDataCache(dst, tw*th*4);
    C3D_TexSetFilter(&s_sans_tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&s_sans_tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
    s_sans_subtex.width=(u16)w; s_sans_subtex.height=(u16)h;
    s_sans_subtex.left=0; s_sans_subtex.top=1; s_sans_subtex.right=(float)w/tw; s_sans_subtex.bottom=1.f-(float)h/th;
    s_sans_img.tex=&s_sans_tex; s_sans_img.subtex=&s_sans_subtex; s_sans_tex_inited=true;
}

/* Dessin et rotation de Sans */
static void draw_sans_png(float cx, float cy, float angle_rad) {
    load_sans_png();
    if (!s_sans_tex_inited) return;
    C2D_DrawParams p; memset(&p,0,sizeof(p));
    p.pos.x=cx; p.pos.y=cy;
    p.pos.w=16.0f; /* 📐 Largeur de Sans */
    p.pos.h=16.0f; /* 📐 Hauteur de Sans */
    p.center.x=8.0f; p.center.y=8.0f; p.angle=angle_rad; p.depth=0.5f;
    C2D_DrawImage(s_sans_img, &p, NULL);
}

/* ============================================================================
 * ඞ 2. EASTER EGG: AMONG US & FNF (MOTEUR PNG & COULEURS)
 * ============================================================================ */
#define MAX_PNG_FRAMES 32   /* Max images clés par animation */
#define MAX_TEX_DIM    96   /* Taille max texture GPU */
#define NUM_DANCE_CLONES 5 /* Nombre de Crewmates sur l'écran du bas */

typedef struct {
    C3D_Tex frames[MAX_PNG_FRAMES];
    Tex3DS_SubTexture subtex[MAX_PNG_FRAMES];
    int delays_ms[MAX_PNG_FRAMES];
    int frame_count, w, h, current_frame;
    u64 last_time;
    bool loaded, failed;
    char loaded_dir[80];
} PngSequence;

static PngSequence g_seq_imp={0}, g_seq_imp_c={0}, g_seq_imp_fin={0}, g_seq_imp_finc={0};
static char g_last_amogus_title[128]="";

/* 🎨 Palette des 5 couleurs de Crewmates pour l'écran du bas (RGB) */
static const u8 k_crew_colors[NUM_DANCE_CLONES][3] = {
    { 220,  20,  20 }, /* 1. Rouge */
    {  20,  80, 230 }, /* 2. Bleu */
    {  20, 160,  50 }, /* 3. Vert */
    { 245,  90, 190 }, /* 4. Rose */
    { 250, 220,  30 }, /* 5. Jaune */
};

/* 🥽 Filtre de Recolorisation : isole la combinaison orange sans toucher à la visière */
static int is_suit_pixel(u8 r, u8 g, u8 b) {
    /* 1. Exclure contours noirs */
    if (r < 40 && g < 40 && b < 40) return 0;
    /* 2. Exclure reflets blancs */
    if (r > 220 && g > 220 && b > 220) return 0;
    /* 3. Exclure visiere cyan/bleue (Bleu ou Vert superieur au Rouge) */
    if (b >= r || g > r) return 0;
    /* 4. Couvrir 100% du costume orange (ombres et reflets) */
    if ((r - b) < 25) return 0;
    if (r >= 65 && g >= 15) return 1;
    return 0;
}

static void free_png_sequence(PngSequence *seq) {
    if (!seq || !seq->loaded) return;
    for (int i=0;i<seq->frame_count;i++)
        if (seq->frames[i].data) C3D_TexDelete(&seq->frames[i]);
    seq->frame_count=0; seq->loaded=false; seq->failed=false; seq->loaded_dir[0]=0;
}

static void free_all_amogus(void) {
    free_png_sequence(&g_seq_imp); free_png_sequence(&g_seq_imp_c);
    free_png_sequence(&g_seq_imp_fin); free_png_sequence(&g_seq_imp_finc);
    for (int c=0;c<NUM_DANCE_CLONES;c++) {
        extern PngSequence g_seq_dance_c[NUM_DANCE_CLONES];
        free_png_sequence(&g_seq_dance_c[c]);
    }
    g_last_amogus_title[0]=0;
}

static bool is_amogus_track(const AudioMetadata *meta) {
    if (!g_settings.easter_eggs_enabled || !meta) return false;
    const char *kws[] = {"among us","amongus","amogus","impostor","imposteur"};
    for (int i=0;i<5;i++) {
        if (meta->title[0]  && contains_icase(meta->title,  kws[i])) return true;
        if (meta->artist[0] && contains_icase(meta->artist, kws[i])) return true;
        if (meta->album[0]  && contains_icase(meta->album,  kws[i])) return true;
    }
    return false;
}

/* Lecture du délai d'affichage écrit dans le nom du PNG (ex: delay-0.04s) */
static int parse_delay_ms(const char *fn) {
    const char *p = strstr(fn, "delay");
    if (!p) return 60;
    while (*p && (*p < '0' || *p > '9') && *p != '.') p++;
    if (!*p) return 60;
    float sec = 0.f;
    if (sscanf(p, "%f", &sec) == 1 && sec > 0.001f && sec < 5.f)
        return (int)(sec * 1000.f + 0.5f);
    return 60;
}

/* Charge les PNGs d'un dossier */
static int cmp_filenames(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static void load_png_sequence_dir(const char *dir_path, const char *alt_path, PngSequence *seq, int recolor_idx, int max_limit) {
    if (seq->loaded && (strcmp(seq->loaded_dir, dir_path)==0 || (alt_path && strcmp(seq->loaded_dir, alt_path)==0)))
        return;
    free_png_sequence(seq);

    DIR *d = opendir(dir_path);
    const char *used = dir_path;
    if (!d && alt_path) { d = opendir(alt_path); used = alt_path; }
    if (!d) { seq->failed = true; return; }
    strncpy(seq->loaded_dir, used, 79);

    char names[180][64];
    int delays_raw[180];
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < 180) {
        if (ent->d_name[0]=='.') continue;
        if (!strstr(ent->d_name,".png") && !strstr(ent->d_name,".PNG") && !strstr(ent->d_name,".gif") && !strstr(ent->d_name,".GIF")) continue;
        strncpy(names[n], ent->d_name, 63); names[n][63]=0;
        delays_raw[n] = parse_delay_ms(ent->d_name);
        n++;
    }
    closedir(d);
    if (n == 0) { seq->failed = true; return; }

    /* TRI ALPHABÉTIQUE OBLIGATOIRE (frame_01 -> frame_27) */
    qsort(names, n, sizeof(names[0]), cmp_filenames);
    for (int i = 0; i < n; i++) {
        delays_raw[i] = parse_delay_ms(names[i]);
    }

    /* Tri alphabétique pour garder l'ordre des images */
    for (int i=0;i<n-1;i++) for (int j=i+1;j<n;j++)
        if (strcmp(names[i], names[j]) > 0) {
            char t[64]; strcpy(t,names[i]); strcpy(names[i],names[j]); strcpy(names[j],t);
            int td=delays_raw[i]; delays_raw[i]=delays_raw[j]; delays_raw[j]=td;
        }

    int limit = (max_limit > 0 && max_limit < MAX_PNG_FRAMES) ? max_limit : MAX_PNG_FRAMES;
    int step = (n > limit) ? ((n + limit - 1) / limit) : 1;

    for (int i = 0; i < n && seq->frame_count < limit; i += step) {
        int sum_delay = 0;
        for (int k = i; k < i + step && k < n; k++) sum_delay += delays_raw[k];
        if (sum_delay < 20) sum_delay = 40;

        char path[160];
        snprintf(path, sizeof(path), "%s/%s", used, names[i]);
        size_t fsz=0; u8 *fbuf = load_file_bytes(path, &fsz);
        if (!fbuf) continue;
        int w=0,h=0,ch=0;
        u8 *px = stbi_load_from_memory(fbuf,(int)fsz,&w,&h,&ch,4);
        free(fbuf);
        if (!px||w<=0||h<=0) continue;

        int ow = (w > MAX_TEX_DIM) ? MAX_TEX_DIM : w;
        int oh = (h > MAX_TEX_DIM) ? MAX_TEX_DIM : h;
        u32 tw = npow2((u32)ow), th = npow2((u32)oh);
        int idx = seq->frame_count;
        if (!C3D_TexInit(&seq->frames[idx], (u16)tw, (u16)th, GPU_RGBA8)) { stbi_image_free(px); break; }
        u8 *dst = (u8*)seq->frames[idx].data;
        if (!dst) { C3D_TexDelete(&seq->frames[idx]); stbi_image_free(px); break; }
        memset(dst, 0, tw*th*4);

        for (int y=0;y<oh;y++) for (int x=0;x<ow;x++) {
            int sx = x * w / ow, sy = y * h / oh;
            const u8 *s = px + (sy*w+sx)*4;
            u8 r=s[0], g=s[1], b=s[2], a=s[3];
            if (recolor_idx >= 0 && a > 80 && is_suit_pixel(r,g,b)) {
                float lum = (r*0.35f + g*0.45f + b*0.2f) / 255.f;
                if (lum < 0.2f) lum = 0.2f;
                if (lum > 1.0f) lum = 1.0f;
                r = (u8)(k_crew_colors[recolor_idx][0] * lum);
                g = (u8)(k_crew_colors[recolor_idx][1] * lum);
                b = (u8)(k_crew_colors[recolor_idx][2] * lum);
            }
            u32 tile=((u32)y/8)*(tw/8)+((u32)x/8);
            u32 mi=morton((u32)x&7,(u32)y&7);
            u32 di=(tile*64+mi)*4;
            dst[di]=a; dst[di+1]=b; dst[di+2]=g; dst[di+3]=r;
        }
        stbi_image_free(px);
        GSPGPU_FlushDataCache(dst, tw*th*4);
        C3D_TexSetFilter(&seq->frames[idx], GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&seq->frames[idx], GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
        seq->subtex[idx].width=(u16)ow; seq->subtex[idx].height=(u16)oh;
        seq->subtex[idx].left=0; seq->subtex[idx].top=1;
        seq->subtex[idx].right=(float)ow/tw; seq->subtex[idx].bottom=1.f-(float)oh/th;
        seq->delays_ms[idx] = sum_delay;
        seq->w=ow; seq->h=oh;
        seq->frame_count++;
    }
    seq->loaded = (seq->frame_count > 0);
    seq->failed = !seq->loaded;
    seq->current_frame = 0;
    seq->last_time = 0;
}

PngSequence g_seq_dance_c[NUM_DANCE_CLONES] = {{0}};

/* 📁 PRÉCHARGEMENT UNIQUE (S'exécute 1 fois au lancement du morceau) */
static void preload_amogus_assets_logic(const AudioMetadata *meta) {
    if (!is_amogus_track(meta)) {
        if (g_last_amogus_title[0]) free_all_amogus();
        return;
    }
    if (meta->title[0] && strcmp(g_last_amogus_title, meta->title) == 0) return;
    strncpy(g_last_amogus_title, meta->title[0] ? meta->title : "amogus", 127);

    for (int c = 0; c < NUM_DANCE_CLONES; c++)
        load_png_sequence_dir("romfs:/esteregg/among-us_dance", "romfs:/esteregg/among_us_dance", &g_seq_dance_c[c], c, 12);
    
    load_png_sequence_dir("romfs:/esteregg/impostor", NULL, &g_seq_imp, -1, 10);
    load_png_sequence_dir("romfs:/esteregg/impostor_chante", NULL, &g_seq_imp_c, -1, 12);
    load_png_sequence_dir("romfs:/esteregg/impostor_fin", NULL, &g_seq_imp_fin, -1, 10);
    load_png_sequence_dir("romfs:/esteregg/impostor_fin_chante", "romfs:/esteregg/imposteur_fin_chante", &g_seq_imp_finc, -1, 12);
}

/* 🎬 Rendu GPU de la séquence de PNG */
static void draw_png_sequence(PngSequence *seq, float x, float y, float scale, bool flip_x) {
    if (!seq || !seq->loaded || seq->frame_count <= 0) return;
    u64 now = osGetTime();
    if (seq->last_time == 0) seq->last_time = now;
    int dly = seq->delays_ms[seq->current_frame];
    if (dly < 20) dly = 40;
    if (now - seq->last_time >= (u64)dly) {
        seq->last_time = now;
        seq->current_frame = (seq->current_frame + 1) % seq->frame_count;
    }
    C2D_Image img; img.tex=&seq->frames[seq->current_frame]; img.subtex=&seq->subtex[seq->current_frame];
    C2D_DrawParams p; memset(&p,0,sizeof(p));
    float sw = (float)seq->w * scale, sh = (float)seq->h * scale;
    if (flip_x) { p.pos.x = x + sw; p.pos.w = -sw; } else { p.pos.x = x; p.pos.w = sw; }
    p.pos.y = y; p.pos.h = sh; p.depth = 0.55f;
    C2D_DrawImage(img, &p, NULL);
}

typedef struct { float x,y,vx; int color; bool active; } DanceClone;
static DanceClone s_clones[NUM_DANCE_CLONES];
static bool s_clones_init = false;

/* ============================================================================
 * 📱 DESSIN ÉCRAN DU BAS (5 CREWMATES DANSANTS)
 * ============================================================================ */
static void handle_amogus_bottom(void) {
    if (!s_clones_init) {
        for (int i = 0; i < NUM_DANCE_CLONES; i++) {
            bool from_left = (i % 2 == 0);
            s_clones[i].x = from_left ? (float)(-80 - i * 40) : (float)(360 + i * 40); /* 🚀 Zone Spawn Hors-Écran */
            s_clones[i].y = (float)(25 + (i * 30) % 120);                              /* ↕️ Position Y */
            s_clones[i].vx = from_left ? (0.50f + i * 0.08f) : -(0.50f + i * 0.08f);   /* 🏃 Vitesse de marche */
            s_clones[i].color = i;
            s_clones[i].active = true;
        }
        s_clones_init = true;
    }
    bool playing = (audio_get_state() == AUDIO_PLAYING);
    for (int i = 0; i < NUM_DANCE_CLONES; i++) {
        if (!s_clones[i].active) continue;
        if (playing) {
            s_clones[i].x += s_clones[i].vx;
            if (s_clones[i].x > 360) { s_clones[i].x = -80; s_clones[i].y = (float)(25 + rand() % 120); }
            if (s_clones[i].x < -80) { s_clones[i].x = 360; s_clones[i].y = (float)(25 + rand() % 120); }
        }
        PngSequence *sq = &g_seq_dance_c[s_clones[i].color];
        if (sq->loaded)
            draw_png_sequence(sq, s_clones[i].x, s_clones[i].y, 0.70f /* 📐 Échelle Crewmates */, s_clones[i].vx > 0);
    }
}

/* ============================================================================
 * 🖥️ DESSIN ÉCRAN DU HAUT (IMPOSTEUR FNF)
 * ============================================================================ */
static void handle_amogus_top(void) {
    bool playing = (audio_get_state() == AUDIO_PLAYING);
    int dur = audio_get_duration();
    int pos = audio_get_position();
    bool is_fin = (dur > 0 && (dur - pos) <= 30);

    PngSequence *active = NULL;
    float ix    = 300.0f;
    float y_off = -4.0f;
    float sc    = 0.85f;

    if (is_fin) {
        if (playing) {
            /* 1. FIN - LECTURE (impostor_fin_chante) */
            active = &g_seq_imp_finc;
            ix    = 313.0f;  /* ↔️ Position X */
            y_off = 2.0f;   /* ↕️ Ajustement Y */
            sc    = 0.95f;   /* 📐 Taille */
        } else {
            /* 2. FIN - PAUSE (impostor_fin) */
            active = &g_seq_imp_fin;
            ix    = 316.0f;  /* ↔️ Position X */
            y_off = 3.0f;   /* ↕️ Ajustement Y */
            sc    = 0.8f;   /* 📐 Taille */
        }
    } else {
        if (playing) {
            /* 3. NORMAL - LECTURE (impostor_chante) */
            active = &g_seq_imp_c;
            ix    = 322.0f;  /* ↔️ Position X */
            y_off = 10.0f;   /* ↕️ Ajustement Y */
            sc    = 1.15f;   /* 📐 Taille */
        } else {
            /* 4. NORMAL - PAUSE (impostor) */
            active = &g_seq_imp;
            ix    = 320.0f;  /* ↔️ Position X */
            y_off = -4.0f;   /* ↕️ Ajustement Y */
            sc    = 0.85f;   /* 📐 Taille */
        }
    }

    if (active && active->loaded) {
        float iy = 128.0f - ((float)active->h * sc) + y_off;
        draw_png_sequence(active, ix, iy, sc, false);
    }
}


/* ── EASTER EGG: FNAF - IT'S ME (DETOURE, SANS FLASH NOIR) ──── */
static C3D_Tex           s_fnaf_tex;
static Tex3DS_SubTexture s_fnaf_subtex;
static C2D_Image         s_fnaf_img;
static bool              s_fnaf_tex_inited  = false;
static bool              s_fnaf_load_failed = false;

static bool              s_fnaf_flash_active = false;
static u64               s_fnaf_flash_end    = 0;
static u64               s_fnaf_next_impulse = 0;
static float             s_fnaf_x = 100.f, s_fnaf_y = 80.f;
static float             s_fnaf_prev_rms = 0.f;
static char              s_fnaf_last_title[128] = "";











/* ==== FNF:DEBUT ==== */
/* Easter egg FNF v4 : mini highway Friday Night Funkin.
 * Les 4 recepteurs gris sont fixes en haut de la zone FNFA_ZONE_* ;
 * des fleches colorees naissent en bas de la zone quand leur zone de
 * frequence tape (onset relatif a la moyenne recente, anti-rebond),
 * montent en FNFA_NOTE_TRAVEL_MS et s'enfoncent dans leur recepteur
 * (pop + flash court a l'impact). Le vrai feel FNF, en miniature.
 * - 8 bandes lissees (viz_smooth) -> 4 voies (gauche/bas/haut/droite)
 * - onset : ecart fast/slow : fast > slow + max(FNFA_ONSET_DELTA,
 *   slow * FNFA_ONSET_REL) ET fast > FNFA_ONSET_FLOOR => attrape les
 *   coups meme sur energie continue (basse tenue) ; anti-rebond
 *   FNFA_REFRACT_MS / voie, max 2 notes actives / voie
 * - gate : easter_eggs_enabled + FNFA_FNF_ONLY=1 (defaut) => seulement les
 *   morceaux FNF (fnf/friday night/funkin/f.n.f/friday/kawai sprite dans
 *   titre/artiste/album). FNFA_FNF_ONLY=0 => tous les morceaux.
 *   (fnf / friday night / funkin / f.n.f / kawai sprite)
 * - le visualiseur normal reste actif (pas de remplacement)
 * Chargement texture : meme pattern que load_sans_png / load_fnaf_png. */
#define FNFA_ZONE_X           204.0f
#define FNFA_ZONE_Y           44.0f
#define FNFA_ZONE_W           190.0f
#define FNFA_ZONE_H           62.0f
#define FNFA_FNF_ONLY         1   /* 1 = easter egg : morceaux FNF seulement ; 0 = tous les morceaux */
#define FNFA_RECEPTORS_ALWAYS 1
#define FNFA_ONSET_FLOOR      0.03f
#define FNFA_ONSET_DELTA      0.010f
#define FNFA_ONSET_REL        0.10f
#define FNFA_REFRACT_MS       90
#define FNFA_VOL_DELTA        0.012f
#define FNFA_VOL_REL          0.12f
#define FNFA_MAX_GAP_MS       600
#define FNFA_NOTE_TRAVEL_MS   200
#define FNFA_MAX_NOTES        12
#define FNFA_TEX_MAX          512
#define FNFA_LANES            4

static const char *fnfa_paths[FNFA_LANES + 1] = {
    "romfs:/esteregg/fleche fnf.png",      /* bandeau 4 recepteurs gris */
    "romfs:/esteregg/fleche violet.png",   /* gauche  = graves         */
    "romfs:/esteregg/fleche bleu.png",     /* bas     = bas-mediums    */
    "romfs:/esteregg/fleche vert.png",     /* haut    = hauts-mediums  */
    "romfs:/esteregg/fleche rouge.png",    /* droite  = aigus          */
};
static C3D_Tex           fnfa_tex[FNFA_LANES + 1];
static Tex3DS_SubTexture fnfa_subtex[FNFA_LANES + 1];
static C2D_Image         fnfa_img[FNFA_LANES + 1];
static bool              fnfa_inited[FNFA_LANES + 1];
static bool              fnfa_failed[FNFA_LANES + 1];
static float             s_fnfa_fast[FNFA_LANES];
static float             s_fnfa_slow[FNFA_LANES];
static float             s_fnfa_pf[FNFA_LANES];
static float             s_fnfa_tick[FNFA_LANES];
static float             s_fnfa_vfast, s_fnfa_vslow, s_fnfa_vpf;
static u64               s_fnfa_last_global;
static u64               s_fnfa_last_any;
static bool              s_fnfa_scanned[FNFA_LANES + 1];
static Tex3DS_SubTexture s_fnfa_rec_sub[FNFA_LANES];
static float             s_fnfa_pop[FNFA_LANES];
static u64               s_fnfa_last_spawn[FNFA_LANES];
typedef struct { bool active; int lane; u64 spawn_ms; } FnfaNote;
static FnfaNote          s_fnfa_notes[FNFA_MAX_NOTES];

static bool is_fnfa_track(const AudioMetadata *meta) {
    if (!g_settings.easter_eggs_enabled || !meta) return false;
    static const char *kw[] = { "fnf", "friday night", "funkin", "f.n.f", "friday", "kawai sprite" };
    for (unsigned k = 0; k < sizeof(kw) / sizeof(kw[0]); k++) {
        if ((meta->title[0]  && contains_icase(meta->title,  kw[k])) ||
            (meta->artist[0] && contains_icase(meta->artist, kw[k])) ||
            (meta->album[0]  && contains_icase(meta->album,  kw[k])))
            return true;
    }
    return false;
}

static bool fnfa_active(const AudioMetadata *meta) {
    if (!g_settings.easter_eggs_enabled) return false;
    if (FNFA_FNF_ONLY) return is_fnfa_track(meta);
    (void)meta;
    return true;
}

/* choisit le chemin : exact d'abord, sinon SCAN du dossier esteregg
   avec mot-cle insensible a la casse/espaces/tirets (le FNAF du projet
   essaie deja plusieurs noms ; ici c'est encore plus robuste) */
static bool fnfa_pick_path(int idx, char *out, size_t outsz) {
    size_t fsz = 0;
    u8 *t = load_file_bytes(fnfa_paths[idx], &fsz);
    if (t) { free(t); snprintf(out, outsz, "%s", fnfa_paths[idx]); return true; }
    static const char *kw[FNFA_LANES + 1] = { "fnf", "viol", "bleu", "vert", "roug" };
    DIR *d = opendir("romfs:/esteregg");
    if (!d) return false;
    char best[160];
    best[0] = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char low[128];
        size_t j = 0;
        for (const char *c = e->d_name; *c && j < 127; c++) {
            char ch = *c;
            if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
            if (ch == '_' || ch == '-' || ch == ' ') continue;
            low[j++] = ch;
        }
        low[j] = 0;
        if (!strstr(low, kw[idx])) continue;
        snprintf(best, sizeof(best), "romfs:/esteregg/%s", e->d_name);
        if (strstr(low, "fleche")) break;   /* priorite aux noms "fleche ..." */
    }
    closedir(d);
    if (!best[0]) return false;
    snprintf(out, outsz, "%s", best);
    return true;
}

static void fnfa_load_one(int idx) {
    if (fnfa_inited[idx] || fnfa_failed[idx]) return;
    if (s_fnfa_scanned[idx]) { fnfa_failed[idx] = true; return; }
    s_fnfa_scanned[idx] = true;
    char path[160];
    if (!fnfa_pick_path(idx, path, sizeof(path))) { fnfa_failed[idx] = true; return; }
    size_t fsz = 0;
    u8 *fbuf = load_file_bytes(path, &fsz);
    if (!fbuf) { fnfa_failed[idx] = true; return; }
    int w = 0, h = 0, ch = 0;
    u8 *px = stbi_load_from_memory(fbuf, (int)fsz, &w, &h, &ch, 4);
    free(fbuf);
    if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); fnfa_failed[idx] = true; return; }
    /* decimation si trop grand (echantillonnage voisin, comme le moteur PNG) */
    int ow = (w > FNFA_TEX_MAX) ? FNFA_TEX_MAX : w;
    int oh = (h > FNFA_TEX_MAX) ? FNFA_TEX_MAX : h;
    u32 tw = npow2((u32)ow), th = npow2((u32)oh);
    if (!C3D_TexInit(&fnfa_tex[idx], (u16)tw, (u16)th, GPU_RGBA8)) {
        stbi_image_free(px); fnfa_failed[idx] = true; return;
    }
    u8 *dst = (u8*)fnfa_tex[idx].data;
    if (!dst) { C3D_TexDelete(&fnfa_tex[idx]); stbi_image_free(px); fnfa_failed[idx] = true; return; }
    memset(dst, 0, (size_t)tw * th * 4);
    for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) {
        int sx = x * w / ow, sy = y * h / oh;
        const u8 *s = px + ((size_t)sy * w + sx) * 4;
        u32 tile = ((u32)y / 8) * (tw / 8) + ((u32)x / 8);
        u32 mi = morton((u32)x & 7, (u32)y & 7), di = (tile * 64 + mi) * 4;
        dst[di] = s[3]; dst[di+1] = s[2]; dst[di+2] = s[1]; dst[di+3] = s[0];
    }
    stbi_image_free(px);
    GSPGPU_FlushDataCache(dst, (size_t)tw * th * 4);
    C3D_TexSetFilter(&fnfa_tex[idx], GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&fnfa_tex[idx], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    fnfa_subtex[idx].width  = (u16)ow;
    fnfa_subtex[idx].height = (u16)oh;
    fnfa_subtex[idx].left   = 0;
    fnfa_subtex[idx].top    = 1;
    fnfa_subtex[idx].right  = (float)ow / tw;
    fnfa_subtex[idx].bottom = 1.f - (float)oh / th;
    fnfa_img[idx].tex    = &fnfa_tex[idx];
    fnfa_img[idx].subtex = &fnfa_subtex[idx];
    if (idx == 0) {
        /* 4 tranches du bandeau = 4 recepteurs individuels */
        for (int k = 0; k < FNFA_LANES; k++) {
            s_fnfa_rec_sub[k] = fnfa_subtex[0];
            s_fnfa_rec_sub[k].left  = fnfa_subtex[0].right * (float)k / FNFA_LANES;
            s_fnfa_rec_sub[k].right = fnfa_subtex[0].right * (float)(k + 1) / FNFA_LANES;
        }
    }
    fnfa_inited[idx] = true;
}

static bool fnfa_spawn(int lane, u64 now) {
    int deja = 0;
    for (int n = 0; n < FNFA_MAX_NOTES; n++)
        if (s_fnfa_notes[n].active && s_fnfa_notes[n].lane == lane) deja++;
    if (deja >= 2) return false;
    for (int n = 0; n < FNFA_MAX_NOTES; n++) {
        if (!s_fnfa_notes[n].active) {
            s_fnfa_notes[n].active = true;
            s_fnfa_notes[n].lane = lane;
            s_fnfa_notes[n].spawn_ms = now;
            s_fnfa_last_spawn[lane] = now;
            s_fnfa_tick[lane] = 1.0f;
            s_fnfa_last_any = now;
            return true;
        }
    }
    return false;
}

static void fnfa_draw(C2D_Image img, float x, float y, float w, float h, float depth) {
    C2D_DrawParams p; memset(&p, 0, sizeof(p));
    p.pos.x = x; p.pos.y = y; p.pos.w = w; p.pos.h = h; p.depth = depth;
    C2D_DrawImage(img, &p, NULL);
}

static void draw_fnfa_arrows(const PlayerUI *ui, float zx, float zy, float zw, float zh) {
    u64 now = osGetTime();
    /* pause/stop : aucun spawn (les bandes du visualiseur restent
       figees en pause, ce qui tromperait l'anti-blanc) ; les notes
       deja en vol atterrissent, les moyennes continuent de vivre
       pour une reprise propre au unpause */
    bool playing = (audio_get_state() == AUDIO_PLAYING);

    /* energie par voie : max de 3 bandes chevauchees (8 bandes -> 4 voies,
       la voie grave en prend 3 pour etre plus reactive) */
    float lane[FNFA_LANES];
    for (int i = 0; i < FNFA_LANES; i++) {
        int b0 = i * 2;
        float m = 0.0f;
        for (int k = 0; k < 3; k++) {
            int b = b0 + k;
            if (b >= EQ_BANDS) b = EQ_BANDS - 1;
            if (ui->viz_smooth[b] > m) m = ui->viz_smooth[b];
        }
        lane[i] = m;
    }

    /* double moyenne (rapide/lente) + onset par ecart => spawn.
       L'eccart absolu+relatif attrape les coups meme quand la bande
       est tenue haute (basse continue), la ou un ratio seul echoue. */
    for (int i = 0; i < FNFA_LANES; i++) {
        s_fnfa_fast[i] += (lane[i] - s_fnfa_fast[i]) * 0.80f;
        s_fnfa_slow[i] += (lane[i] > s_fnfa_slow[i])
                        ? (lane[i] - s_fnfa_slow[i]) * 0.010f
                        : (lane[i] - s_fnfa_slow[i]) * 0.050f;
        s_fnfa_pop[i] *= 0.85f;
        if (s_fnfa_pop[i] < 0.001f) s_fnfa_pop[i] = 0.0f;
        s_fnfa_tick[i] *= 0.75f;
        if (s_fnfa_tick[i] < 0.001f) s_fnfa_tick[i] = 0.0f;
        float delta = FNFA_ONSET_DELTA > s_fnfa_slow[i] * FNFA_ONSET_REL
                    ? FNFA_ONSET_DELTA : s_fnfa_slow[i] * FNFA_ONSET_REL;
        bool onset = (s_fnfa_fast[i] > FNFA_ONSET_FLOOR) &&
                     (s_fnfa_fast[i] > s_fnfa_slow[i] + delta) &&
                     (s_fnfa_fast[i] > s_fnfa_pf[i] + 0.01f) &&
                     ((now - s_fnfa_last_spawn[i]) > (u64)FNFA_REFRACT_MS);
        s_fnfa_pf[i] = s_fnfa_fast[i];
        if (!onset || !playing) continue;
        fnfa_spawn(i, now);
    }

    /* PIC GLOBAL de volume (demande user : moyenne de volume + pics).
       La moyenne globale monte lentement / redescend vite ; chaque pic
       declenche la voie dominante meme si son onset perso a rate =>
       plus jamais de passage ou "il se passe rien". */
    float vol = 0.0f;
    for (int i = 0; i < FNFA_LANES; i++) vol += lane[i];
    vol /= (float)FNFA_LANES;
    s_fnfa_vfast += (vol - s_fnfa_vfast) * 0.80f;
    s_fnfa_vslow += (vol > s_fnfa_vslow) ? (vol - s_fnfa_vslow) * 0.008f
                                        : (vol - s_fnfa_vslow) * 0.050f;
    float vdelta = FNFA_VOL_DELTA > s_fnfa_vslow * FNFA_VOL_REL
                 ? FNFA_VOL_DELTA : s_fnfa_vslow * FNFA_VOL_REL;
    bool gpeak = (s_fnfa_vfast > FNFA_ONSET_FLOOR) &&
                 (s_fnfa_vfast > s_fnfa_vslow + vdelta) &&
                 (s_fnfa_vfast > s_fnfa_vpf + 0.005f) &&
                 ((now - s_fnfa_last_global) > (u64)FNFA_REFRACT_MS);
    s_fnfa_vpf = s_fnfa_vfast;
    if (gpeak && playing) {
        s_fnfa_last_global = now;
        int best = -1;
        float bs = 0.0f;
        for (int i = 0; i < FNFA_LANES; i++) {
            float g = s_fnfa_fast[i] - s_fnfa_slow[i];
            if (g > bs) { bs = g; best = i; }
        }
        if (best >= 0 && bs > 0.0f &&
            (now - s_fnfa_last_spawn[best]) > (u64)FNFA_REFRACT_MS)
            fnfa_spawn(best, now);
    }

    /* FILET ANTI-BLANC : aucune note depuis FNFA_MAX_GAP_MS alors que le
       son est actif => note sur la voie la plus presente (essaie les
       voies dans l'ordre decroissant si la premiere est saturee).
       Coupe sous le plancher de volume : le silence reste silencieux. */
    if (playing &&
        (now - s_fnfa_last_any) > (u64)FNFA_MAX_GAP_MS &&
        s_fnfa_vfast > FNFA_ONSET_FLOOR) {
        int ordre[FNFA_LANES];
        for (int i = 0; i < FNFA_LANES; i++) ordre[i] = i;
        for (int i = 0; i < FNFA_LANES - 1; i++)
            for (int j = i + 1; j < FNFA_LANES; j++)
                if (s_fnfa_fast[ordre[j]] > s_fnfa_fast[ordre[i]]) {
                    int t = ordre[i]; ordre[i] = ordre[j]; ordre[j] = t;
                }
        for (int k = 0; k < FNFA_LANES; k++) {
            if (fnfa_spawn(ordre[k], now)) break;
        }
    }

    /* geometrie : recepteurs en haut de zone, aspect corrige, centres */
    fnfa_load_one(0);
    if (!fnfa_inited[0]) return;
    float iw = (float)fnfa_subtex[0].width, ih = (float)fnfa_subtex[0].height;
    float rh = zh * 0.42f;
    float scale = rh / ih;
    float strip_w = iw * scale;
    if (strip_w > zw) { strip_w = zw; scale = zw / iw; }
    float strip_h = ih * scale;
    float sx = zx + (zw - strip_w) * 0.5f, sy = zy;
    float slot = strip_w / FNFA_LANES;
    float rec_cy = sy + strip_h * 0.5f;
    float spawn_y = zy + zh + 4.0f;

    if (FNFA_RECEPTORS_ALWAYS) {
        for (int i = 0; i < FNFA_LANES; i++) {
            float ts = 1.0f + 0.15f * s_fnfa_tick[i];
            float rw = slot * ts, rh2 = strip_h * ts;
            C2D_Image rim = fnfa_img[0];
            rim.subtex = &s_fnfa_rec_sub[i];
            float cx = sx + slot * i + slot * 0.5f;
            fnfa_draw(rim, cx - rw * 0.5f, rec_cy - rh2 * 0.5f, rw, rh2, 0.50f);
        }
    }

    /* notes en vol : montent de spawn_y vers le centre du recepteur */
    for (int n = 0; n < FNFA_MAX_NOTES; n++) {
        FnfaNote *no = &s_fnfa_notes[n];
        if (!no->active) continue;
        u64 age = now - no->spawn_ms;
        if (age >= (u64)FNFA_NOTE_TRAVEL_MS) {
            no->active = false;
            s_fnfa_pop[no->lane] = 1.0f;   /* impact => pop du recepteur */
            continue;
        }
        fnfa_load_one(no->lane + 1);
        if (!fnfa_inited[no->lane + 1]) { no->active = false; continue; }
        float t = (float)age / (float)FNFA_NOTE_TRAVEL_MS;
        float ny = spawn_y - t * (spawn_y - rec_cy);
        float aw = slot * 0.85f, ah = aw;
        float ar = (float)fnfa_subtex[no->lane+1].width / (float)fnfa_subtex[no->lane+1].height;
        if (ar > 1.0f) ah = aw / ar; else aw = ah * ar;
        float cx = sx + slot * no->lane + slot * 0.5f;
        /* trainee : copie plus petite derriere la note */
        fnfa_draw(fnfa_img[no->lane + 1], cx - aw * 0.375f, ny - ah * 0.375f + 5.0f,
                  aw * 0.75f, ah * 0.75f, 0.58f);
        fnfa_draw(fnfa_img[no->lane + 1], cx - aw * 0.5f, ny - ah * 0.5f, aw, ah, 0.60f);
    }

    /* flash d'impact sur le recepteur (court, pop decroissant) */
    for (int i = 0; i < FNFA_LANES; i++) {
        float e = s_fnfa_pop[i];
        if (e <= 0.10f) continue;
        fnfa_load_one(i + 1);
        if (!fnfa_inited[i + 1]) continue;
        float aw = slot * 1.05f, ah = aw;
        float ar = (float)fnfa_subtex[i+1].width / (float)fnfa_subtex[i+1].height;
        if (ar > 1.0f) ah = aw / ar; else aw = ah * ar;
        float f = 1.0f + 0.35f * e;
        float dw = aw * f, dh = ah * f;
        float cx = sx + slot * i + slot * 0.5f;
        fnfa_draw(fnfa_img[i + 1], cx - dw * 0.5f, rec_cy - dh * 0.5f, dw, dh, 0.65f);
    }
}
/* ==== FNF:FIN ==== */

static bool is_fnaf_track(const AudioMetadata *meta) {
    if (!g_settings.easter_eggs_enabled || !meta) return false;
    const char *kws[] = {
        "fnaf", "five night", "five nights", "freddy",
        "golden freddy", "at freddy", "f.n.a.f"
    };
    for (int i = 0; i < 7; i++) {
        if (meta->title[0]  && contains_icase(meta->title,  kws[i])) return true;
        if (meta->artist[0] && contains_icase(meta->artist, kws[i])) return true;
        if (meta->album[0]  && contains_icase(meta->album,  kws[i])) return true;
    }
    return false;
}

static void load_fnaf_png(void) {
    if (s_fnaf_tex_inited || s_fnaf_load_failed) return;
    size_t fsz = 0;
    u8 *fbuf = load_file_bytes("romfs:/esteregg/IT'S ME.png", &fsz);
    if (!fbuf) fbuf = load_file_bytes("romfs:/esteregg/ITS ME.png", &fsz);
    if (!fbuf) fbuf = load_file_bytes("romfs:/esteregg/IT_S_ME.png", &fsz);
    if (!fbuf) fbuf = load_file_bytes("romfs:/esteregg/its_me.png", &fsz);
    if (!fbuf) { s_fnaf_load_failed = true; return; }

    int w=0,h=0,ch=0;
    u8 *px = stbi_load_from_memory((const stbi_uc*)fbuf, (int)fsz, &w, &h, &ch, 4);
    free(fbuf);
    if (!px || w <= 0 || h <= 0) { s_fnaf_load_failed = true; return; }

    u32 tw = npow2((u32)w), th = npow2((u32)h);
    if (!C3D_TexInit(&s_fnaf_tex, (u16)tw, (u16)th, GPU_RGBA8)) {
        stbi_image_free(px); s_fnaf_load_failed = true; return;
    }
    u8 *dst = (u8*)s_fnaf_tex.data;
    memset(dst, 0, tw * th * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u32 tile = ((u32)y/8)*(tw/8) + ((u32)x/8);
            u32 mi   = morton((u32)x&7, (u32)y&7);
            u32 di   = (tile*64 + mi) * 4;
            const u8 *s = px + (y*w + x) * 4;
            dst[di+0] = s[3]; dst[di+1] = s[2];
            dst[di+2] = s[1]; dst[di+3] = s[0];
        }
    }
    stbi_image_free(px);
    GSPGPU_FlushDataCache(dst, tw * th * 4);
    C3D_TexSetFilter(&s_fnaf_tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&s_fnaf_tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
    s_fnaf_subtex.width  = (u16)w;
    s_fnaf_subtex.height = (u16)h;
    s_fnaf_subtex.left   = 0.f;
    s_fnaf_subtex.top    = 1.f;
    s_fnaf_subtex.right  = (float)w / (float)tw;
    s_fnaf_subtex.bottom = 1.f - (float)h / (float)th;
    s_fnaf_img.tex = &s_fnaf_tex;
    s_fnaf_img.subtex = &s_fnaf_subtex;
    s_fnaf_tex_inited = true;
}

static void fnaf_trigger_flash(void) {
    load_fnaf_png();
    if (!s_fnaf_tex_inited) return;
    s_fnaf_flash_active = true;
    /* Visible mais court : ~0.18s */
    s_fnaf_flash_end = osGetTime() + 180ULL;

    float sc = 0.42f + (float)(rand() % 12) / 100.f; /* 0.42 - 0.54 : texte entier lisible */
    float iw = (float)s_fnaf_subtex.width  * sc;
    float ih = (float)s_fnaf_subtex.height * sc;
    if (iw < 60.f) iw = 90.f;
    if (ih < 20.f) ih = 28.f;
    /* ne depasse jamais l ecran */
    if (iw > 360.f) { float k = 360.f / iw; iw *= k; ih *= k; }
    if (ih > 200.f) { float k = 200.f / ih; iw *= k; ih *= k; }

    int max_x = (int)(400.f - iw);
    int max_y = (int)(240.f - ih);
    if (max_x < 1) max_x = 1;
    if (max_y < 1) max_y = 1;
    s_fnaf_x = (float)(rand() % max_x);
    s_fnaf_y = (float)(rand() % max_y);
}

static void fnaf_update_impulses(const AudioMetadata *meta) {
    if (audio_get_state() != AUDIO_PLAYING) {
        s_fnaf_flash_active = false;
        return;
    }
    u64 now = osGetTime();

    /* Nouveau morceau : attendre un peu avant la 1ere apparition */
    if (meta && meta->title[0] && strcmp(s_fnaf_last_title, meta->title) != 0) {
        strncpy(s_fnaf_last_title, meta->title, 127);
        s_fnaf_flash_active = false;
        s_fnaf_next_impulse = now + 6000ULL + (u64)(rand() % 6000); /* 6-12s */
        s_fnaf_prev_rms = 0.f;
        return;
    }

    if (s_fnaf_flash_active && now >= s_fnaf_flash_end)
        s_fnaf_flash_active = false;

    /* Pic audio (rythme) */
    float samples[256];
    int n = audio_get_oscillo(samples);
    float rms = 0.f;
    if (n > 0) {
        for (int i = 0; i < n; i++) rms += samples[i] * samples[i];
        rms = sqrtf(rms / (float)n);
    }
    bool beat = (rms > 0.32f && rms > s_fnaf_prev_rms * 1.55f && (rms - s_fnaf_prev_rms) > 0.10f);
    s_fnaf_prev_rms = rms * 0.82f + s_fnaf_prev_rms * 0.18f;

    if (s_fnaf_next_impulse == 0)
        s_fnaf_next_impulse = now + 7000ULL + (u64)(rand() % 8000);

    bool timed = (now >= s_fnaf_next_impulse);

    /* Rare : beat fort OU timer long */
    if (!s_fnaf_flash_active && (beat || timed)) {
        /* Si c'est un beat, on demande quand meme un cooldown mini */
        if (beat && now + 500ULL < s_fnaf_next_impulse && !timed) {
            /* ignore petits beats trop rapproches */
        } else {
            fnaf_trigger_flash();
            s_fnaf_next_impulse = now + 7000ULL + (u64)(rand() % 14000); /* 7-21s rare */
        }
    }
}

static void handle_fnaf_top(const AudioMetadata *meta) {
    fnaf_update_impulses(meta);
    if (!s_fnaf_flash_active || !s_fnaf_tex_inited) return;

    /* PNG detoure uniquement, taille moderee pour lire TOUT le texte */
    float base_w = (float)s_fnaf_subtex.width;
    float base_h = (float)s_fnaf_subtex.height;
    float sc = 0.30f; /* echelle reduite pour voir IT'S ME complet */
    float w = base_w * sc;
    float h = base_h * sc;
    if (w > 340.f) { float k = 340.f / w; w *= k; h *= k; }
    if (h > 90.f)  { float k = 90.f / h;  w *= k; h *= k; }

    /* petit tremblement */
    float jx = (float)((rand() % 3) - 1);
    float jy = (float)((rand() % 3) - 1);

    C2D_DrawParams p;
    memset(&p, 0, sizeof(p));
    p.pos.x = s_fnaf_x + jx;
    p.pos.y = s_fnaf_y + jy;
    p.pos.w = w;
    p.pos.h = h;
    p.depth = 0.92f;
    C2D_DrawImage(s_fnaf_img, &p, NULL);
}


void player_ui_init(PlayerUI *ui)
{
    memset(ui, 0, sizeof(PlayerUI));
    ui->viz_style      = VIZ_BARS;
    ui->cover_tex_id   = -1;
    ui->cover_tex_init = false;
    ui->cover_loaded   = false;
    ui->scroll_title_x = (float)TOP_WIDTH;
    ui->last_tick      = osGetTime();
}

void player_ui_load_cover(PlayerUI *ui, const AudioMetadata *meta)
{
    ui->cover_loaded = false;
    if (ui->cover_tex_init) {
        C3D_TexDelete(&ui->cover_tex);
        ui->cover_tex_init = false;
    }
    if (!meta || !meta->has_cover || !meta->cover_data || meta->cover_size == 0) {
        if (ui->cover_tex_init) {
            C3D_TexDelete(&ui->cover_tex);
            ui->cover_tex_init = false;
        }
        ui->cover_loaded = false;
        return;
    }
    int w, h, ch;
    u8 *px = stbi_load_from_memory(
        (const stbi_uc*)meta->cover_data,
        (int)meta->cover_size,
        &w, &h, &ch, 4);
    if (!px) return;
    int tw = w, th_i = h;
    u8 *scaled = NULL;
    if (w > 128 || h > 128) {
        tw = 128; th_i = 128;
        scaled = (u8*)malloc(128 * 128 * 4);
        if (!scaled) { stbi_image_free(px); return; }
        for (int dy = 0; dy < 128; dy++) {
            for (int dx = 0; dx < 128; dx++) {
                int sx = dx * w / 128;
                int sy = dy * h / 128;
                if (sx >= w) sx = w-1;
                if (sy >= h) sy = h-1;
                memcpy(scaled + (dy*128+dx)*4,
                       px    + (sy*w+sx)*4, 4);
            }
        }
    }
    upload_cover_tex(ui, scaled ? scaled : px, tw, th_i);
    stbi_image_free(px);
    if (scaled) free(scaled);
    ui->cover_loaded = ui->cover_tex_init;
}

void player_ui_update(PlayerUI *ui, const AudioMetadata *meta, float dt)
{
    preload_amogus_assets_logic(meta);

    // Mise à jour horloge (1x/sec seulement, pas 60x/sec !)
    clock_update();

    // Données visualiseur
    if (ui->viz_style == VIZ_EQ)
        audio_get_visualizer_fft(ui->viz_data);
    else
        audio_get_visualizer(ui->viz_data);

    // Lissage : 0.3f montée, 0.08f descente
    for (int i = 0; i < EQ_BANDS; i++) {
        float spd = (ui->viz_data[i] > ui->viz_smooth[i]) ? 0.3f : 0.08f;
        ui->viz_smooth[i] += (ui->viz_data[i] - ui->viz_smooth[i]) * spd;
    }

    // Scroll titre
    // title_zone_w = largeur disponible pour le titre en pixels
    float title_zone_w = 200.f;
    float title_px_w   = meta ? (strlen(meta->title) * 8.0f * 0.52f) : 0.f;

    if (meta && title_px_w > title_zone_w) {
        if (ui->scroll_pause_timer > 0) {
            ui->scroll_pause_timer--;
        } else {
        ui->scroll_title_x -= 1.2f;
            if (ui->scroll_title_x < -(title_px_w + 160.f)) {
                ui->scroll_title_x    = 104.f; /* DECAL1 : aligne sur info_x=104, hors masque (102+d3) */
                ui->scroll_pause_timer = 120; // ← 2sec pause à 60fps
            }
        }
    } else {
        ui->scroll_title_x    = 104.f; /* DECAL1 : aligne sur info_x=104, hors masque (102+d3) */
        ui->scroll_pause_timer = 0;
    }
    (void)dt;
}

// ============================================================
// DRAW TOP — écran du haut avec effet 3D
//
// STRUCTURE :
//  Y=0   Header (barre titre)          d3(0.8f)
//  Y=24  Pochette 84x84                d3(1.2f)
//        Titre/Artiste/Album            d3(0.8f)
//  Y=128 Barre progression             d3(1.0f)
//        Curseur                        d3(1.5f)
//        Volume                         d3(0.0f)
//  Y=163 Visualiseur                   d3(0.6f)
//  Y=223 Fin écran
// ============================================================

/* ============================================================ */
/* ❄️🎃 MOTEUR D'ÉVÉNEMENTS SAISONNIERS (Halloween, Noël...)    */
/* ============================================================ */
#include <math.h>

#define NUM_EVENT_PARTICLES 15

typedef enum {
    EVENT_AUTO = 0,
    EVENT_HALLOWEEN,
    EVENT_CHRISTMAS,
    EVENT_NEWYEAR,
    EVENT_EASTER,
    EVENT_MODE_COUNT
} EventMode;

static EventMode s_dev_event_mode = EVENT_AUTO;
static EventMode s_current_active_event = EVENT_AUTO;

typedef struct {
    float x;          
    float y;          
    float speed_y;    
    float speed_x;    
    float angle;      
    float rot_speed;  
    float size;       
    float depth_3d;   
    int tex_idx;      
    bool flip_h;      
    float float_offset;
} EventParticle;

static EventParticle s_particles[NUM_EVENT_PARTICLES];
static u32 s_event_last_tick = 0;

/* ASSETS HALLOWEEN */
static bool s_hw_inited = false, s_hw_failed = false;
static C3D_Tex s_hw_tex[3]; static C2D_Image s_hw_img[3], s_hw_img_flip[3];
static Tex3DS_SubTexture s_hw_subtex[3], s_hw_subtex_flip[3];

/* ASSETS NOUVEL AN (Feux d'artifice avec Montée & Animation Indépendante) */
static PngSequence s_ny_seq[3] = {{0}};
static bool s_ny_inited = false, s_ny_failed = false;

#define NUM_FIREWORKS 5
typedef struct {
    float x, y;
    float speed_y;
    int feu_idx;
    int frame_idx;
    u64 last_time;
    float scale;
    bool active;
    float depth_3d;
} FireworkInstance;

static FireworkInstance s_fireworks[NUM_FIREWORKS];

static bool is_newyear_date(void) {
    time_t t = time(NULL); struct tm *tm_info = localtime(&t);
    if (!tm_info) return false;
    if (tm_info->tm_mon == 11 && tm_info->tm_mday == 31 && tm_info->tm_hour == 23 && tm_info->tm_min >= 50) return true;
    if (tm_info->tm_mon == 0 && tm_info->tm_mday == 1 && tm_info->tm_hour == 0 && tm_info->tm_min <= 10) return true;
    return false;
}

static void respawn_firework(FireworkInstance *fw, bool initial) {
    fw->x = (float)(rand() % 300 + 50);
    // Vitesse de montee plus lente et douce (-1.1f a -2.1f au lieu de -2.5f+)
    fw->speed_y = -1.1f - ((float)(rand() % 10) / 10.0f);
    // Depart etale sur la hauteur au lancement initial
    fw->y = initial ? (float)(rand() % 420 + 50) : (float)(rand() % 60 + 480);
    fw->feu_idx = rand() % 3;
    fw->scale = 0.55f + (float)(rand() % 35) / 100.0f;
    fw->frame_idx = rand() % 8; // Decalage de frame pour variete
    fw->last_time = osGetTime();
    fw->active = true;
    fw->depth_3d = 0.3f + (float)(rand() % 12) / 10.0f;
}

/* ASSETS PÂQUES */
static bool s_ea_inited = false, s_ea_failed = false;
static C3D_Tex s_ea_tex[3]; static C2D_Image s_ea_img[3], s_ea_img_flip[3];
static Tex3DS_SubTexture s_ea_subtex[3], s_ea_subtex_flip[3];

/* ASSETS NOËL */
static bool s_xm_inited = false, s_xm_failed = false;
static C3D_Tex s_xm_tex[3]; static C2D_Image s_xm_img[3], s_xm_img_flip[3];
static Tex3DS_SubTexture s_xm_subtex[3], s_xm_subtex_flip[3];

static bool is_halloween_date(void) {
    time_t t = time(NULL); struct tm *tm_info = localtime(&t);
    if (!tm_info) return false;
    return (tm_info->tm_mon == 9 && tm_info->tm_mday >= 10 && tm_info->tm_mday <= 31);
}



static bool is_christmas_date(void) {
    time_t t = time(NULL); struct tm *tm_info = localtime(&t);
    if (!tm_info) return false;
    return (tm_info->tm_mon == 11);
}

static bool is_easter_date(void) {
    time_t t = time(NULL); struct tm *tm_info = localtime(&t);
    if (!tm_info) return false;

    int year = tm_info->tm_year + 1900;
    int a = year % 19;
    int b = year / 100;
    int c = year % 100;
    int d = b / 4;
    int e = b % 4;
    int f = (b + 8) / 25;
    int g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4;
    int k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    int month = (h + l - 7 * m + 114) / 31 - 1;
    int day = ((h + l - 7 * m + 114) % 31) + 1;

    if (tm_info->tm_mon == month && abs(tm_info->tm_mday - day) <= 3) return true;
    return false;
}

static bool is_any_event_day(void) {
    return is_halloween_date() || is_christmas_date() || is_newyear_date() || is_easter_date();
}


static EventMode get_active_event(void) {
    if (!g_settings.seasonal_events_enabled) return EVENT_AUTO;
    if (is_newyear_date()) return EVENT_NEWYEAR;
    if (is_christmas_date()) return EVENT_CHRISTMAS;
    if (is_halloween_date()) return EVENT_HALLOWEEN;
    if (is_easter_date()) return EVENT_EASTER;
    return EVENT_AUTO;
}

void cycle_dev_event_mode(void) {
    if (is_any_event_day()) {
        g_settings.seasonal_events_enabled = !g_settings.seasonal_events_enabled;
    }
}

static void respawn_particle(EventParticle *p, bool initial, EventMode mode) {
    p->x = (float)(rand() % 380 + 10);
    p->y = initial ? (float)(rand() % 500 - 30) : (float)(-(rand() % 40 + 20));
    p->speed_x = -0.2f + (float)(rand() % 100) / 250.0f;
    p->angle = (float)(rand() % 628) / 100.0f;
    p->depth_3d = 0.4f + (float)(rand() % 12) / 10.0f;
    p->tex_idx = rand() % 3;
    p->flip_h = (rand() % 2 == 0);
    p->float_offset = (float)(rand() % 628) / 100.0f;

    if (mode == EVENT_CHRISTMAS) {
        p->speed_y = 0.3f + (float)(rand() % 100) / 200.0f;
        p->rot_speed = -0.01f + (float)(rand() % 100) / 5000.0f;
        p->size = 12.0f + (float)(rand() % 12);
    } else if (mode == EVENT_EASTER) {
        p->speed_y = 0.4f + (float)(rand() % 100) / 180.0f;
        p->rot_speed = -0.02f + (float)(rand() % 100) / 3000.0f;
        p->size = 18.0f + (float)(rand() % 14);
    } else {
        p->speed_y = 0.5f + (float)(rand() % 100) / 100.0f;
        p->rot_speed = -0.025f + (float)(rand() % 100) / 2000.0f;
        p->size = 22.0f + (float)(rand() % 16);
    }
}

static void load_event_assets(EventMode mode) {
    if (mode == EVENT_NEWYEAR && !s_ny_inited && !s_ny_failed) {
        const char *dirs[3] = { "romfs:/event/nouvel an/feu1", "romfs:/event/nouvel an/feu2", "romfs:/event/nouvel an/feu3" };
        int loaded = 0;
        for (int i = 0; i < 3; i++) {
            load_png_sequence_dir(dirs[i], NULL, &s_ny_seq[i], -1, 32);
            if (s_ny_seq[i].loaded && s_ny_seq[i].frame_count > 0) loaded++;
        }
        if (loaded > 0) {
            s_ny_inited = true;
            for (int i = 0; i < NUM_FIREWORKS; i++) respawn_firework(&s_fireworks[i], true);
        } else s_ny_failed = true;
    }
    if (mode == EVENT_HALLOWEEN && !s_hw_inited && !s_hw_failed) {
        const char *paths[3] = { "romfs:/event/halloween/citrouille1.png", "romfs:/event/halloween/citrouille2.png", "romfs:/event/halloween/citrouille3.png" };
        int loaded = 0;
        for (int i = 0; i < 3; i++) {
            if (load_single_png_to_img(paths[i], &s_hw_tex[i], &s_hw_img[i], &s_hw_subtex[i])) {
                s_hw_subtex_flip[i] = s_hw_subtex[i];
                s_hw_subtex_flip[i].left  = s_hw_subtex[i].right; s_hw_subtex_flip[i].right = s_hw_subtex[i].left;
                s_hw_img_flip[i].tex = &s_hw_tex[i]; s_hw_img_flip[i].subtex = &s_hw_subtex_flip[i];
                loaded++;
            }
        }
        if (loaded > 0) s_hw_inited = true; else s_hw_failed = true;
    }
    
    if (mode == EVENT_CHRISTMAS && !s_xm_inited && !s_xm_failed) {
        const char *paths[3] = { "romfs:/event/noel/flocon1.png", "romfs:/event/noel/flocon2.png", "romfs:/event/noel/flocon3.png" };
        int loaded = 0;
        for (int i = 0; i < 3; i++) {
            if (load_single_png_to_img(paths[i], &s_xm_tex[i], &s_xm_img[i], &s_xm_subtex[i])) {
                s_xm_subtex_flip[i] = s_xm_subtex[i];
                s_xm_subtex_flip[i].left  = s_xm_subtex[i].right; s_xm_subtex_flip[i].right = s_xm_subtex[i].left;
                s_xm_img_flip[i].tex = &s_xm_tex[i]; s_xm_img_flip[i].subtex = &s_xm_subtex_flip[i];
                loaded++;
            }
        }
        if (loaded > 0) s_xm_inited = true; else s_xm_failed = true;
    }

    /* Ce bloc Paques etait colle A L INTERIEUR de la boucle de Noel ci-dessus
       (accolade manquante) : sa propre condition (mode==EVENT_EASTER) ne
       pouvait donc jamais etre vraie puisqu on n y entrait que si
       mode==EVENT_CHRISTMAS - l oeuf de Paques ne se chargeait jamais. */
    if (mode == EVENT_EASTER && !s_ea_inited && !s_ea_failed) {
        const char *paths[3] = { "romfs:/event/paque/oeuf1.png", "romfs:/event/paque/oeuf2.png", "romfs:/event/paque/oeuf3.png" };
        int loaded = 0;
        for (int i = 0; i < 3; i++) {
            if (load_single_png_to_img(paths[i], &s_ea_tex[i], &s_ea_img[i], &s_ea_subtex[i])) {
                s_ea_subtex_flip[i] = s_ea_subtex[i];
                s_ea_subtex_flip[i].left  = s_ea_subtex[i].right; s_ea_subtex_flip[i].right = s_ea_subtex[i].left;
                s_ea_img_flip[i].tex = &s_ea_tex[i]; s_ea_img_flip[i].subtex = &s_ea_subtex_flip[i];
                loaded++;
            }
        }
        if (loaded > 0) s_ea_inited = true; else s_ea_failed = true;
    }
}

static void update_event_particles(EventMode mode) {
    u32 now = osGetTime();
    if (now == s_event_last_tick) return;
    s_event_last_tick = now;

    if (mode == EVENT_NEWYEAR) {
        if (!s_ny_inited) return;
        u64 now_ms = osGetTime();
        for (int i = 0; i < NUM_FIREWORKS; i++) {
            FireworkInstance *fw = &s_fireworks[i];
            if (!fw->active) continue;

            // MONTEE (speed_y est negatif e.g. -2.5f, donc Y diminue vers l'ecran du haut)
            fw->y += fw->speed_y;
            // ZIGZAG LEGER
            fw->x += sinf(fw->y * 0.025f + (float)i * 1.7f) * 0.45f;

            // Animation d'explosion
            if (now_ms - fw->last_time >= 40) {
                fw->last_time = now_ms;
                PngSequence *seq = &s_ny_seq[fw->feu_idx];
                if (seq->loaded && seq->frame_count > 0) {
                    fw->frame_idx = (fw->frame_idx + 1) % seq->frame_count;
                }
            }

            // Reapparition sous l'ecran du bas quand depasse le haut de l'ecran haut (Y < -60)
            if (fw->y < -60.0f) {
                respawn_firework(fw, false);
            }
        }
        return;
    }

    if (s_current_active_event != mode) {
        s_current_active_event = mode;
        for (int i = 0; i < NUM_EVENT_PARTICLES; i++) respawn_particle(&s_particles[i], true, mode);
    }

    for (int i = 0; i < NUM_EVENT_PARTICLES; i++) {
        EventParticle *p = &s_particles[i];
        p->y += p->speed_y;
        p->angle += p->rot_speed;

        if (mode == EVENT_CHRISTMAS) {
        p->speed_y = 0.3f + (float)(rand() % 100) / 200.0f;
        p->rot_speed = -0.01f + (float)(rand() % 100) / 5000.0f;
        p->size = 12.0f + (float)(rand() % 12);
    } else if (mode == EVENT_EASTER) {
        p->speed_y = 0.4f + (float)(rand() % 100) / 180.0f;
        p->rot_speed = -0.02f + (float)(rand() % 100) / 3000.0f;
        p->size = 18.0f + (float)(rand() % 14);
    } else {
            p->x += p->speed_x;
        }

        if (p->y > 520.0f || p->x < -40.0f || p->x > 440.0f) {
            respawn_particle(p, false, mode);
        }
    }
}

static void event_draw_top(void) {
    EventMode mode = get_active_event();
    if (mode == EVENT_AUTO) return;

    load_event_assets(mode);

    if (mode == EVENT_HALLOWEEN && !s_hw_inited) return;
    if (mode == EVENT_CHRISTMAS && !s_xm_inited) return;
    if (mode == EVENT_EASTER && !s_ea_inited) return;
    if (mode == EVENT_NEWYEAR && !s_ny_inited) return;

    if (g_3d_eye == 0) update_event_particles(mode);

    if (mode == EVENT_NEWYEAR) {
        for (int i = 0; i < NUM_FIREWORKS; i++) {
            FireworkInstance *fw = &s_fireworks[i];
            if (!fw->active || fw->y < -60.0f || fw->y > 260.0f) continue;
            PngSequence *seq = &s_ny_seq[fw->feu_idx];
            if (!seq->loaded || seq->frame_count <= 0) continue;

            int f = fw->frame_idx % seq->frame_count;
            C2D_Image img;
            img.tex = &seq->frames[f];
            img.subtex = &seq->subtex[f];

            float shift_3d = d3(fw->depth_3d);
            float draw_x = fw->x + shift_3d - ((float)seq->w * fw->scale * 0.5f);
            float draw_y = fw->y - ((float)seq->h * fw->scale * 0.5f);
            C2D_DrawImageAt(img, draw_x, draw_y, 0.95f, NULL, fw->scale, fw->scale);
        }
        /* Un 2e copier-coller du chargement des oeufs de Paques etait ici,
           imbrique dans ce bloc EVENT_NEWYEAR : sa condition
           (mode==EVENT_EASTER) ne pouvait donc jamais etre vraie, et la
           ligne 2778 ci-dessus sortait de toute facon avant d y arriver si
           mode valait reellement EVENT_EASTER. Du code de CHARGEMENT dans
           une fonction de DESSIN etait de toute facon la mauvaise place :
           load_event_assets() (desormais corrige) s en occupe deja. */
        return;
    }

    for (int i = 0; i < NUM_EVENT_PARTICLES; i++) {
        EventParticle *p = &s_particles[i];
        if (p->y >= -30.0f && p->y <= 250.0f) {
            float shift_3d = d3(p->depth_3d);

            C2D_Image *img = NULL;
            if (mode == EVENT_HALLOWEEN) img = &s_hw_img[p->tex_idx % 3];
            if (mode == EVENT_CHRISTMAS) img = &s_xm_img[p->tex_idx % 3];
            if (mode == EVENT_EASTER)    img = &s_ea_img[p->tex_idx % 3];
            if (!img || !img->tex) continue;

            C2D_DrawParams params;
            memset(&params, 0, sizeof(params));
            params.pos.x = p->x + shift_3d;
            params.pos.y = p->y;
            params.pos.w = p->size;
            params.pos.h = p->size;
            params.center.x = p->size * 0.5f;
            params.center.y = p->size * 0.5f;
            params.angle = p->angle;
            params.depth = 0.96f;

            C2D_DrawImage(*img, &params, NULL);
        }
    }
}

static void event_draw_bottom(void) {
    EventMode mode = get_active_event();
    if (mode == EVENT_AUTO) return;

    if (mode == EVENT_NEWYEAR) {
        if (!s_ny_inited) return;
        for (int i = 0; i < NUM_FIREWORKS; i++) {
            FireworkInstance *fw = &s_fireworks[i];
            if (!fw->active || fw->y < 180.0f || fw->y > 520.0f) continue;
            PngSequence *seq = &s_ny_seq[fw->feu_idx];
            if (!seq->loaded || seq->frame_count <= 0) continue;

            int f = fw->frame_idx % seq->frame_count;
            C2D_Image img;
            img.tex = &seq->frames[f];
            img.subtex = &seq->subtex[f];

            float draw_x = fw->x - 40.0f - ((float)seq->w * fw->scale * 0.5f);
            float draw_y = fw->y - 240.0f - ((float)seq->h * fw->scale * 0.5f);
            C2D_DrawImageAt(img, draw_x, draw_y, 0.95f, NULL, fw->scale, fw->scale);
        }
        return;
    }

    if (mode == EVENT_HALLOWEEN && !s_hw_inited) return;
    if (mode == EVENT_CHRISTMAS && !s_xm_inited) return;
    if (mode == EVENT_EASTER && !s_ea_inited) return;

    for (int i = 0; i < NUM_EVENT_PARTICLES; i++) {
        EventParticle *p = &s_particles[i];
        if (p->y >= 220.0f && p->y <= 520.0f) {
            C2D_Image *img = NULL;
            if (mode == EVENT_HALLOWEEN) img = &s_hw_img[p->tex_idx % 3];
            if (mode == EVENT_CHRISTMAS) img = &s_xm_img[p->tex_idx % 3];
            if (mode == EVENT_EASTER)    img = &s_ea_img[p->tex_idx % 3];
            if (!img || !img->tex) continue;

            C2D_DrawParams params;
            memset(&params, 0, sizeof(params));
            params.pos.x = p->x - 40.0f;
            params.pos.y = p->y - 240.0f;
            params.pos.w = p->size;
            params.pos.h = p->size;
            params.center.x = p->size * 0.5f;
            params.center.y = p->size * 0.5f;
            params.angle = p->angle;
            params.depth = 0.96f;

            C2D_DrawImage(*img, &params, NULL);
        }
        /* Un 3e copier-coller du meme chargement (deja retire 2 fois plus
           haut dans ce fichier) etait ici, imbrique dans la boucle de
           particules ci-dessus - memes raisons, meme suppression. */
    }
}


void player_ui_draw_top(const PlayerUI *ui, const AudioMetadata *meta,
                        const Playlist *pl)
{
    Theme *th = current_theme;
    static AudioMetadata nd_temp_meta;
    if (nd_get_playing()) {
        memset(&nd_temp_meta, 0, sizeof(nd_temp_meta));
        strncpy(nd_temp_meta.title, nd_get_playing_title(), sizeof(nd_temp_meta.title)-1);
        strncpy(nd_temp_meta.artist, nd_get_playing_artist(), sizeof(nd_temp_meta.artist)-1);
        strncpy(nd_temp_meta.album, "Navidrome", sizeof(nd_temp_meta.album)-1);
        nd_temp_meta.has_cover = (nd_get_current_cover_img() != NULL);
        meta = &nd_temp_meta;
    }

    // ── UN SEUL clear du TextBuf pour toute la frame ──────────
    // C'est ici que le clear est fait, PAS dans chaque draw_text()
    frame_begin();

    // ── FOND ─────────────────────────────────────────────────
    draw_rect(0, 0,            TOP_WIDTH, TOP_HEIGHT/2, th->bg_primary);
    draw_rect(0, TOP_HEIGHT/2, TOP_WIDTH, TOP_HEIGHT/2, th->bg_secondary);

    // ════════════════════════════════════════════════════════
    // HEADER  d3(0.8f) ← changer pour plus/moins d'effet
    // ════════════════════════════════════════════════════════
    float hd = d3(0.8f);
    draw_rect(hd, 0, TOP_WIDTH, 24, th->bg_header);
    draw_text(8 + hd, 4, 0.52f, th->text_accent, "3DSoundShell V" APP_VERSION);
    audio_viz_tick(); /* VIZ14c : viz au rythme du draw, pas du decodage */

    /* Notif MAJ bas gauche - visible 5sec au demarrage */
    if (g_update_notif_timer > 0) {
        if (g_server_unreachable) {
            draw_text(8, 225, 0.50f, RGBA8(255,150,50,255), T("Serveur inaccessible"));
        } else if (g_version_ahead) {
            /* V0.96 : version installee > enregistree (build dev) */
            draw_text(8, 225, 0.50f, RGBA8(100,200,255,255), T("Version en avance"));
        } else if (g_update_available) {
            char maj_notif[48];
            snprintf(maj_notif, 48, T("Mise a jour V%s disponible"), g_latest_version);
            draw_text(8, 225, 0.50f, th->accent2, maj_notif);
        } else {
            draw_text(8, 225, 0.50f, th->text_disabled, T("Version a jour."));
        }
    }

    // Horloge (depuis le cache, pas de localtime() chaque frame)
    draw_text(TOP_WIDTH/2 - 28 + hd, 5, 0.48f, th->text_accent, s_clock_str);

    // Badge PLAY/PAUSE/STOP
    AudioState st = audio_get_state();
    const char *badge = (st==AUDIO_PLAYING) ? T("PLAY >") :
                        (st==AUDIO_PAUSED)  ? T("PAUSE ||") : T("STOP !");
    draw_text(TOP_WIDTH - 65 + hd, 5, 0.48f,
        st==AUDIO_PLAYING ? th->accent : th->text_disabled, badge);

    if (pl->shuffle)
        draw_text(TOP_WIDTH - 130 + hd, 5, 0.45f, th->accent2, "[A]");
    if (pl->repeat == REPEAT_ONE)
        draw_text(TOP_WIDTH - 100 + hd, 5, 0.45f, th->accent, T("rep.1"));
    else if (pl->repeat == REPEAT_ALL)
        draw_text(TOP_WIDTH - 100 + hd, 5, 0.45f, th->accent, T("rep.A"));

    // ════════════════════════════════════════════════════════
    // POCHETTE  d3(1.2f) ← changer pour plus/moins d'effet
    //   art_x  = position X (+ décalage 3D)
    //   art_y  = position Y
    //   art_sz = taille en pixels
    // ════════════════════════════════════════════════════════
    float art_x  = 10 + d3(1.2f);
    float art_y  = 30;
    float art_sz = 84;

    // Effet de déformation de gelée "Jelly Wave" si la distorsion est active !
    if (g_settings.distortion > 0) {
        float t = (float)(osGetTime() % 100000ULL) * 0.001f;
        float intensity = (float)g_settings.distortion / 100.0f;
        
        // Oscillation de taille (effet haut-parleur qui gonfle de manière asymétrique)
        float wave = sinf(t * 12.0f) * 10.0f * intensity; 
        art_sz += wave;
        art_x -= wave * 0.5f;
        art_y -= wave * 0.5f;
        
        // Tremblement léger de position
        art_x += sinf(t * 35.0f) * 1.5f * intensity;
        art_y += cosf(t * 28.0f) * 1.5f * intensity;
    }

    C2D_Image *nd_img = nd_get_current_cover_img();
    
    // Paramètres de taille et position par défaut
    float final_w = art_sz;
    float final_h = art_sz;
    float final_x = art_x;
    float final_y = art_y;

    // Effet vagues/vibrations de gelée si la distorsion est active !
    if (g_settings.distortion > 0) {
        float t = (float)(osGetTime() % 100000ULL) * 0.001f;
        float intensity = (float)g_settings.distortion / 100.0f;
        
        // Oscillation de taille (effet haut-parleur qui sature)
        float wave_w = 1.0f + sinf(t * 12.0f) * 0.15f * intensity;
        float wave_h = 1.0f + cosf(t * 12.0f) * 0.15f * intensity;
        
        // Tremblements de position
        float shake_x = sinf(t * 35.0f) * 2.5f * intensity;
        float shake_y = cosf(t * 28.0f) * 2.5f * intensity;
        
        final_w = art_sz * wave_w;
        final_h = art_sz * wave_h;
        final_x = art_x + (art_sz - final_w) * 0.5f + shake_x;
        final_y = art_y + (art_sz - final_h) * 0.5f + shake_y;
    }

    if (nd_get_playing() && nd_img) {
        float sx = final_w / (float)nd_img->subtex->width;
        float sy = final_h / (float)nd_img->subtex->height;
        C2D_DrawImageAt(*nd_img, final_x, final_y, 0, NULL, sx, sy);
        draw_rect(final_x - 2, final_y - 2,      final_w + 4, 2, th->accent);
        draw_rect(final_x - 2, final_y + final_h, final_w + 4, 2, th->accent);
        draw_rect(final_x - 2, final_y - 2,      2, final_h + 4, th->accent);
        draw_rect(final_x + final_w, final_y - 2, 2, final_h + 4, th->accent);
    } else if (g_settings.show_cover && meta->has_cover &&
        ui->cover_loaded && ui->cover_tex_init)
    {
        C2D_Image img = {
            (C3D_Tex*)&ui->cover_tex,
            (Tex3DS_SubTexture*)&ui->cover_subtex
        };
        float sx = final_w / (float)ui->cover_subtex.width;
        float sy = final_h / (float)ui->cover_subtex.height;
        C2D_DrawImageAt(img, final_x, final_y, 0, NULL, sx, sy);
        draw_rect(final_x - 2, final_y - 2,      final_w + 4, 2, th->accent);
        draw_rect(final_x - 2, final_y + final_h, final_w + 4, 2, th->accent);
        draw_rect(final_x - 2, final_y - 2,      2, final_h + 4, th->accent);
        draw_rect(final_x + final_w, final_y - 2, 2, final_h + 4, th->accent);
    } else {
        draw_cover_placeholder(art_x, art_y, art_sz);
    }

    // ════════════════════════════════════════════════════════
    // TITRE / ARTISTE / ALBUM  d3(0.8f) ← changer pour ajuster
    //   info_x  = X de départ (droite de la pochette)
    //   info_y0 = Y de départ
    // ════════════════════════════════════════════════════════
    float info_x  = 104 + d3(0.8f);
    float info_y0 = 32;

    float title_len  = strlen(meta->title) * 8.f * 0.52f;
    float title_zone = 200.f;
    if (title_len > title_zone) {
        // Titre long → scroll
        draw_text(ui->scroll_title_x + d3(0.8f), info_y0,
                  0.52f, th->text_primary, meta->title);
        // Masque pour cacher le débordement
        draw_rect(0, 24, 102 + d3(0.8f), 90, th->bg_primary);
        // Redessiner la pochette par-dessus
        C2D_Image *nd_img2 = nd_get_current_cover_img();
        if (nd_get_playing() && nd_img2) {
            C2D_DrawImageAt(*nd_img2, art_x, art_y, 0, NULL,
                art_sz/(float)nd_img2->subtex->width,
                art_sz/(float)nd_img2->subtex->height);
            draw_rect(art_x - 2, art_y - 2,      art_sz + 4, 2, th->accent);
            draw_rect(art_x - 2, art_y + art_sz, art_sz + 4, 2, th->accent);
            draw_rect(art_x - 2, art_y - 2,      2, art_sz + 4, th->accent);
            draw_rect(art_x + art_sz, art_y - 2, 2, art_sz + 4, th->accent);
        } else if (g_settings.show_cover && meta->has_cover &&
            ui->cover_loaded && ui->cover_tex_init) {
            C2D_Image img2 = {
                (C3D_Tex*)&ui->cover_tex,
                (Tex3DS_SubTexture*)&ui->cover_subtex
            };
            C2D_DrawImageAt(img2, art_x, art_y, 0, NULL,
                art_sz/(float)ui->cover_subtex.width,
                art_sz/(float)ui->cover_subtex.height);
            /* CADRE2 : le masque du titre scrollant mangeait la
               bordure (elle n'etait pas redessinee avec la pochette,
               seul le trait bas survivait sous le masque) */
            draw_rect(art_x - 2, art_y - 2,      art_sz + 4, 2, th->accent);
            draw_rect(art_x - 2, art_y + art_sz, art_sz + 4, 2, th->accent);
            draw_rect(art_x - 2, art_y - 2,      2, art_sz + 4, th->accent);
            draw_rect(art_x + art_sz, art_y - 2, 2, art_sz + 4, th->accent);
        } else {
            draw_cover_placeholder(art_x, art_y, art_sz);
        }
    } else {
        draw_text(info_x, info_y0, 0.52f, th->text_primary, meta->title);
    }

    draw_text(info_x, info_y0 + 22, 0.46f, th->text_secondary, meta->artist);
    draw_text(info_x, info_y0 + 38, 0.44f, th->text_disabled,  meta->album);

    /* V0.97 : Affichage du sleep timer a droite */
    
    /* V0.98 : Affichage de l'Alarme sous le Sleep Timer */
    if (g_settings.alarm_enabled) {
        char alarm_buf[16];
        snprintf(alarm_buf, sizeof(alarm_buf), "ALARM %02d:%02d", g_settings.alarm_hour, g_settings.alarm_minute);
        u32 acol = current_theme ? current_theme->accent2 : C2D_Color32(255, 100, 100, 255);
        draw_text(TOP_WIDTH - 90, 26, 0.40f, acol, alarm_buf);
    }

    if (sleep_timer_is_active()) {
        int rem = sleep_timer_get_remaining_sec();
        char tstr[24];
        if (rem >= 3600)
            snprintf(tstr, 24, "SLEEP %d:%02d:%02d",
                rem / 3600, (rem % 3600) / 60, rem % 60);
        else
            snprintf(tstr, 24, "SLEEP %d:%02d", rem / 60, rem % 60);

        u32 tcol = sleep_timer_is_paused() ? th->text_disabled : th->accent2;
        draw_text(info_x + 195, info_y0 + 22, 0.42f, tcol, tstr);
    }

    if (meta->year > 0) {
        char yr[16]; snprintf(yr, 16, "(%d)", meta->year);
        draw_text(info_x + 180, info_y0 + 38, 0.44f, th->text_disabled, yr);
    }
    if (meta->genre[0])
        draw_text(info_x, info_y0 + 54, 0.42f, th->text_disabled, meta->genre);

    char info_line[48] = "";
    if (meta->format[0] && pl->count > 0 && pl->current >= 0)
        snprintf(info_line, 48, "%s  %s %d/%d",
                 meta->format, T("Piste"), pl->current+1, pl->count);
    else if (pl->count > 0 && pl->current >= 0)
        snprintf(info_line, 48, "%s %d/%d", T("Piste"), pl->current+1, pl->count);
    else if (meta->format[0])
        snprintf(info_line, 48, "%s", meta->format);
    draw_text(info_x, info_y0 + 70, 0.40f, th->text_disabled, info_line);

    // ════════════════════════════════════════════════════════
    // BARRE DE PROGRESSION  d3(1.0f) ← changer pour ajuster
    //   pb_x = X (+ décalage 3D)
    //   pb_y = Y
    //   pb_w = largeur totale
    //   pb_h = hauteur
    // ════════════════════════════════════════════════════════
    float pb_d = d3(1.0f);
    float pb_x = 10 + pb_d;
    float pb_y = 128;
    float pb_w = 370;
    float pb_h = 8;

    draw_rect(pb_x, pb_y, pb_w, pb_h, th->progress_bg);
    extern bool  s_touch_seeking;
    extern float s_touch_seek_pct;
    float pct = s_touch_seeking ? s_touch_seek_pct : audio_get_position_pct();
    if (pct > 1.0f) pct = 1.0f; if (pct < 0.0f) pct = 0.0f;   /* AAC11 : clamp curseur */
    if (pct > 0.f)
        draw_rect(pb_x, pb_y, pb_w * pct, pb_h, th->progress_fill);

    // Curseur  d3(1.8f)
    float cur_d = d3(1.8f);
    float cur_x = pb_x + pb_w*pct + (cur_d - pb_d);
    float cur_y = pb_y + 4.0f;
    if (is_undertale_track(meta)) {
        static float s_sans_angle = 0.0f;
        if (audio_get_state() == AUDIO_PLAYING) {
            s_sans_angle += 0.025f;
            if (s_sans_angle >= 6.28318f) s_sans_angle -= 6.28318f;
        }
        draw_sans_png(cur_x, cur_y, s_sans_angle);
    } else {
        draw_rect(cur_x - 4, pb_y - 3, 8, 14, th->text_primary);
    }

    char t_cur[12], t_tot[12];
    fmt_time(t_cur, s_touch_seeking ? (int)(s_touch_seek_pct * audio_get_duration()) : audio_get_position());
    fmt_time(t_tot, audio_get_duration());
    draw_text(pb_x,         pb_y + 10, 0.44f, th->text_secondary, t_cur);
    draw_text(pb_x+pb_w-30, pb_y + 10, 0.44f, th->text_secondary, t_tot);

    // ── Volume  d3(0.0f) = plan neutre ───────────────────────
    float vol   = audio_get_volume();
    float vol_d = d3(0.f);
    draw_text(pb_x + vol_d,       pb_y + 22, 0.40f, th->text_disabled, T("VOL"));
    draw_rect(pb_x+26 + vol_d,    pb_y + 24, 120,     5, th->progress_bg);
    {   /* VOLBOOST3 : >100% la barre CONTINUE vers la droite en
           couleur custom (60 px par +100%), % suit le bout */
        float vol_n = vol > 1.0f ? 1.0f : vol;
        float bar_end = 120.0f;
        draw_rect(pb_x+26 + vol_d, pb_y + 24, 120*vol_n, 5, th->accent2);
        if (vol > 1.0f) {
            float bw = 60.0f * (vol - 1.0f);
            draw_rect(pb_x+26 + vol_d + 120.0f, pb_y + 24, bw, 5,
                      th->text_accent);
            bar_end += bw;
        }
        char vol_str[8];
        snprintf(vol_str, 8, "%d%%", (int)(vol*100));
        draw_text(pb_x+26 + vol_d + bar_end + 4, pb_y + 22, 0.40f,
                  th->text_disabled, vol_str);
    }

    // ════════════════════════════════════════════════════════
    // VISUALISEUR  d3(0.6f) ← changer pour ajuster
    //   vx = X (+ décalage 3D)
    //   vy = Y
    //   vw = largeur
    //   vh = hauteur
    // ════════════════════════════════════════════════════════
    float vd = d3(0.6f);
    float vx  = 8  + vd;
    float vy  = 163;
    float vw  = 384;
    float vh  = 60;

    if (!g_settings.viz_enabled) {
        /* V0.96 : viz desactive, on affiche juste "VIZ OFF" */
        draw_rect(vx - 2, vy - 2, vw + 4, vh + 4, th->bg_secondary);
        draw_text(vx + vw/2.f - 30, vy + vh/2.f - 6, 0.45f,
                  th->text_disabled, "-- Viz OFF --");
        return;
    }

    draw_rect(vx - 2, vy - 2, vw + 4, vh + 4, th->bg_secondary);

                                                /* FNF:HOOK */
    if (fnfa_active(meta)) draw_fnfa_arrows(ui, FNFA_ZONE_X, FNFA_ZONE_Y, FNFA_ZONE_W, FNFA_ZONE_H);
switch (ui->viz_style) {
        case VIZ_BARS:
            draw_viz_bars(ui, vx, vy, vw, vh);
            break;
        case VIZ_WAVE:
            draw_viz_wave(ui, vx, vy, vw, vh);
            break;
        case VIZ_CIRCLE:
            draw_viz_circle(ui, vx + vw/2.f, vy + vh/2.f, vh/2.f - 2);
            break;
        case VIZ_FIRE:
            draw_viz_fire(ui, vx, vy, vw, vh);
            break;
                case VIZ_WAVES:
            draw_viz_waves(ui, vx, vy, vw, vh);
            break;
        case VIZ_TUNNEL:
            draw_viz_tunnel(ui, vx, vy, vw, vh);
            break;
        case VIZ_PULSE:
            draw_viz_pulse(ui, vx, vy, vw, vh);
            break;
        case VIZ_WAVEFORM:
            draw_viz_waveform(ui, vx, vy, vw, vh);
            break;
        case VIZ_EQ:
            draw_viz_eq(ui, vx, vy, vw, vh);
            break;
        case VIZ_OSCILLO:
            draw_viz_oscillo(ui, vx, vy, vw, vh);
            break;
        default:
            break;
    }

    const char *viz_names[] = {T("Barres"),T("Onde"),T("Cercle"),T("Feu"),T("Oscillo"),T("Waves"),T("Tunnel"),T("Pulse"),T("Waveform"),T("EQ")};
    draw_text(vx + vw - 40, vy + vh + 4, 0.38f,
              th->text_disabled, viz_names[ui->viz_style]);
    if (is_amogus_track(meta)) handle_amogus_top();
    if (is_fnaf_track(meta)) handle_fnaf_top(meta);
    if (is_ddlc_track(meta)) draw_ddlc_chibis_top(10.f, 238.f);
    event_draw_top();
}

// ============================================================
// DRAW BOTTOM — écran du bas (pas d'effet 3D)
// ============================================================
void player_ui_draw_bottom(PlayerUI *ui, const AudioMetadata *meta,
                           const Playlist *pl)
{
    Theme *th = current_theme;
    static AudioMetadata nd_temp_meta;
    if (nd_get_playing()) {
        memset(&nd_temp_meta, 0, sizeof(nd_temp_meta));
        strncpy(nd_temp_meta.title, nd_get_playing_title(), sizeof(nd_temp_meta.title)-1);
        strncpy(nd_temp_meta.artist, nd_get_playing_artist(), sizeof(nd_temp_meta.artist)-1);
        strncpy(nd_temp_meta.album, "Navidrome", sizeof(nd_temp_meta.album)-1);
        nd_temp_meta.has_cover = (nd_get_current_cover_img() != NULL);
        meta = &nd_temp_meta;
    }

    // Clear du TextBuf pour l'écran du bas
    // (nécessaire car draw_top et draw_bottom sont des scènes séparées)
    frame_begin();

    draw_rect(0, 0, BOT_WIDTH, BOT_HEIGHT, th->bg_primary);
    draw_rect(0, 0, BOT_WIDTH, 26, th->bg_header);
    draw_text(8, 5, 0.52f, th->text_accent, T("Controles"));

    /* --- Mini-Panel Dev Evenements (Bandeau Header X:110..240) --- */
    static bool s_dev_badge_held = false;
    touchPosition dev_tp;
    hidTouchRead(&dev_tp);
    if (dev_tp.px >= 110 && dev_tp.px <= 240 && dev_tp.py >= 0 && dev_tp.py <= 30) {
        if (!s_dev_badge_held) {
            cycle_dev_event_mode();
            s_dev_badge_held = true;
        }
    } else if (dev_tp.px == 0 && dev_tp.py == 0) {
        s_dev_badge_held = false;
    }

        if (is_any_event_day()) {
        const char *ev_label = g_settings.seasonal_events_enabled ? "[Event: ON]" : "[Event: OFF]";
        u32 btn_color = g_settings.seasonal_events_enabled ? th->accent2 : th->text_secondary;
        draw_text(130, 5, 0.42f, btn_color, ev_label);
    }

    // Batterie
    u8 batt_pct = 0, charging = 0;
    MCUHWC_GetBatteryLevel(&batt_pct);
    PTMU_GetBatteryChargeState(&charging);
    int pct = (int)batt_pct;
    char bstr[16];
    if (charging)       snprintf(bstr, 16, "BAT+ %d%%", pct);
    else if (pct <= 5)  snprintf(bstr, 16, "BAT!! %d%%", pct);
    else if (pct <= 20) snprintf(bstr, 16, "BAT! %d%%", pct);
    else                snprintf(bstr, 16, "BAT %d%%", pct);
    draw_text(BOT_WIDTH-65, 5, 0.40f,
        pct <= 20 ? th->accent2 : th->text_disabled, bstr);

    if (is_oneshot_track(meta)) draw_oneshot_bulb(28.f, 185.f);
    // Indication seek tactile (simple texte, pas de barre)
    draw_text(90, 224, 0.42f, th->text_disabled, T("< appuiez ici pour avancer >"));

    // Boutons principaux
    float by = 36;
    struct { const char *lbl; float x; u32 col; } btns[] = {
        {"<- Prev",   14,  th->text_secondary},
        {"L+R Pause", 110, th->accent},  /* L+R = touches, universel */
        {"Next ->",   222, th->text_secondary},
    };
    for (int i = 0; i < 3; i++) {
        draw_rect(btns[i].x, by, 80, 20, th->bg_secondary);
        draw_text(btns[i].x+4, by+4, 0.42f, btns[i].col, btns[i].lbl);
    }
    draw_rect(0, by+24, BOT_WIDTH, 1, th->border);
    by += 32;

    draw_text(10,  by, 0.44f, th->text_secondary, T("+- Volume"));
    draw_text(110, by, 0.44f, th->text_secondary, T("ZL/ZR +-10s"));
    draw_text(222, by, 0.44f,
        pl->shuffle ? th->accent : th->text_disabled,
        pl->shuffle ? T("Aleat ON") : T("Aleat OFF"));
    by += 20;

    const char *rep_str[] = {T("Repet: Non"),T("Repet: x1"),T("Repet: Tout")};
    draw_text(10, by, 0.44f,
        pl->repeat != REPEAT_NONE ? th->accent : th->text_disabled,
        rep_str[pl->repeat]);
    const char *viz_n[] = {T("Barres"),T("Onde"),T("Cercle"),T("Feu"),T("Oscillo"),T("Waves"),T("Tunnel"),T("Pulse"),T("Waveform"),T("EQ Pro")};
    draw_text(180, by, 0.44f, th->text_secondary, viz_n[ui->viz_style]);
    draw_rect(0, by+18, BOT_WIDTH, 1, th->border);
    by += 24;

    const char *keys[] = {
        T("A = Play/Pause"),    T("L+R = Play/Pause"),
        T("X = Playlist"),      T("Y = Aleatoire"),
        T("Select = Fichiers"), T("R = Changer Viz"),
        T("B = Retour"),
    };
    for (int i = 0; i < 7; i++) {
        float kx = (i%2==0) ? 8 : 164;
        float ky  = by + (i/2)*16;
        draw_text(kx, ky, 0.38f, th->text_disabled, keys[i]);
    }

    /* V0.97 : Bouton tactile play/pause timer (bas droite) - visible seulement si actif */
    if (sleep_timer_is_active()) {
        float bx = BOT_WIDTH - 90;
        float by2 = BOT_HEIGHT - 60;
        float bw = 82, bh = 32;

        /* V0.97 : couleurs INVERSEES : actif = theme accent, pause = grise */
        u32 bg_col;
        if (sleep_timer_is_paused()) {
            bg_col = RGBA8(80, 80, 90, 255); /* grise */
        } else {
            bg_col = th->accent; /* couleur theme (accent principal) */
        }
        draw_rect(bx, by2, bw, bh, bg_col);

        /* Bordure */
        draw_rect(bx, by2, bw, 1, th->border);
        draw_rect(bx, by2+bh-1, bw, 1, th->border);
        draw_rect(bx, by2, 1, bh, th->border);
        draw_rect(bx+bw-1, by2, 1, bh, th->border);

        /* Texte : blanc sur actif, gris clair sur pause */
        const char *label = sleep_timer_is_paused() ? "SLEEP ||" : "SLEEP >";
        u32 tcol = sleep_timer_is_paused()
            ? RGBA8(180, 180, 190, 255)
            : RGBA8(255, 255, 255, 255);
        draw_text(bx + 6, by2 + 4, 0.45f, tcol, label);

        /* Temps restant */
        int rem = sleep_timer_get_remaining_sec();
        char tstr[16];
        snprintf(tstr, 16, "%d:%02d", rem/60, rem%60);
        draw_text(bx + 6, by2 + 18, 0.38f, tcol, tstr);
    }

    if (is_amogus_track(meta)) handle_amogus_bottom();
    event_draw_bottom();
    (void)meta;
}

// ============================================================
// UTILITAIRES
// ============================================================

void player_ui_cycle_viz(PlayerUI *ui)
{
    ui->viz_style = (VisualizerStyle)((ui->viz_style+1) % VIZ_COUNT);
}

void player_ui_exit(PlayerUI *ui)
{
    if (ui->cover_tex_init) {
        C3D_TexDelete(&ui->cover_tex);
        ui->cover_tex_init = false;
    }
    if (s_textbuf) {
        C2D_TextBufDelete(s_textbuf);
        s_textbuf = NULL;
    }
}

/* V0.98 : Permet a l'alarme de forcer la mise a jour de la pochette au reveil */
void player_ui_force_cover_update(void)
{
    /* On force la liberation de l'ancienne pochette pour declencher le rechargement */
    extern void free_cover(void);
    free_cover();
}


/* VRAI VISUALISEUR POUR LE SPLASH DE DÉMARRAGE (Utilise les vraies fonctions draw_viz_*) */
/* VRAI VISUALISEUR DU LECTEUR POUR LE SPLASH DE DÉMARRAGE (Multi-fréquence synthétique fluide 60 FPS) */
/* VRAI VISUALISEUR DU LECTEUR POUR LE SPLASH DE DÉMARRAGE (60 FPS dynamique) */
void player_ui_draw_splash_viz(VisualizerStyle style, float x, float y, float w, float h, float alpha, int frame)
{
    extern int g_3d_eye;
    int saved_eye = g_3d_eye;
    g_3d_eye = 0; /* FORCE L'EIL 0 : Débloque les moteurs d'animation de Feu/Tunnel/EQ/Pulse ! */

    PlayerUI dummy_ui;
    memset(&dummy_ui, 0, sizeof(PlayerUI));
    dummy_ui.viz_style = style;

    /* Générateur de rythme multi-fréquence rythmé */
    float f = (float)frame;
    for (int i = 0; i < EQ_BANDS; i++) {
        float b1 = sinf(f * 0.12f + (float)i * 0.4f);
        float b2 = cosf(f * 0.22f + (float)i * 0.8f);
        float val = (0.35f + 0.35f * b1 + 0.20f * b2) * alpha;
        if (val < 0.05f) val = 0.05f;
        if (val > 0.95f) val = 0.95f;
        dummy_ui.viz_smooth[i] = val;
    }

    switch (style) {
        case VIZ_BARS:
            draw_viz_bars(&dummy_ui, x, y, w, h);
            break;
        case VIZ_WAVE:
            draw_viz_wave(&dummy_ui, x, y, w, h);
            break;
        case VIZ_CIRCLE:
            draw_viz_circle(&dummy_ui, x + w/2.f, y + h/2.f, h/2.f - 2);
            break;
        case VIZ_FIRE:
            draw_viz_fire(&dummy_ui, x, y, w, h);
            break;
        case VIZ_WAVES:
            draw_viz_waves(&dummy_ui, x, y, w, h);
            break;
        case VIZ_TUNNEL:
            draw_viz_tunnel(&dummy_ui, x, y, w, h);
            break;
        case VIZ_PULSE: {
            Theme *th = current_theme;
            float cx = x + w / 2.f;
            float cy = y + h / 2.f;

            if (!s_pulse_init) {
                for (int i = 0; i < PULSE_PARTICLES; i++) s_pulse_p[i].life = 0.f;
                s_pulse_init = true;
            }

            /* Rythme synthétique : Beat marqué toutes les 16 frames (~3.7 Hz) */
            float beat_cycle = fmodf(f, 16.f);
            bool is_beat = (beat_cycle < 1.0f);
            float synth_rms = powf(1.0f - (beat_cycle / 16.f), 2.2f) * alpha;

            /* 1. SPAWN PARTICULES SUR CHAQUE BEAT */
            if (is_beat && alpha > 0.1f) {
                int spawned = 0;
                for (int i = 0; i < PULSE_PARTICLES && spawned < 16; i++) {
                    if (s_pulse_p[i].life <= 0.f) {
                        float ang = (float)(rand() % 3600) * 3.14159265f / 1800.f;
                        float speed = 2.2f + (float)(rand() % 40) / 10.f;
                        s_pulse_p[i].x = cx;
                        s_pulse_p[i].y = cy;
                        s_pulse_p[i].vx = cosf(ang) * speed;
                        s_pulse_p[i].vy = sinf(ang) * speed;
                        s_pulse_p[i].life = 1.f;
                        s_pulse_p[i].size = 3.5f + (float)(rand() % 35) / 10.f;

                        u32 col = th->visualizer_bars[rand() % EQ_BANDS];
                        s_pulse_p[i].r = (col >> 0) & 0xff;
                        s_pulse_p[i].g = (col >> 8) & 0xff;
                        s_pulse_p[i].b = (col >> 16) & 0xff;
                        spawned++;
                    }
                }
            }

            /* 2. RENDU & ANIMATION DES PARTICULES */
            for (int i = 0; i < PULSE_PARTICLES; i++) {
                if (s_pulse_p[i].life <= 0.f) continue;

                s_pulse_p[i].x += s_pulse_p[i].vx;
                s_pulse_p[i].y += s_pulse_p[i].vy;
                s_pulse_p[i].vx *= 0.95f; /* Freinage fluide */
                s_pulse_p[i].vy *= 0.95f;
                s_pulse_p[i].life -= 0.035f; /* Disparition progressive */

                if (s_pulse_p[i].x < x || s_pulse_p[i].x > x + w ||
                    s_pulse_p[i].y < y || s_pulse_p[i].y > y + h ||
                    s_pulse_p[i].life <= 0.f) {
                    s_pulse_p[i].life = 0;
                    continue;
                }

                float sz = s_pulse_p[i].size * s_pulse_p[i].life;
                u8 pa = (u8)(s_pulse_p[i].life * 255.f * alpha);
                draw_rect(s_pulse_p[i].x - sz / 2.f, s_pulse_p[i].y - sz / 2.f, sz, sz,
                          RGBA8(s_pulse_p[i].r, s_pulse_p[i].g, s_pulse_p[i].b, pa));
            }

            /* 3. CŒUR CENTRAL PULSANT */
            float heart_sz = (10.f + synth_rms * 24.f);
            u32 heart_col = is_beat ? RGBA8(255, 255, 255, (u8)(255 * alpha))
                                    : (synth_rms > 0.3f ? th->accent2 : th->accent);
            draw_rect(cx - heart_sz / 2.f, cy - heart_sz / 2.f, heart_sz, heart_sz, heart_col);

            /* 4. HALO D'AURA */
            float halo_sz = heart_sz + 8.f;
            u8 halo_a = (u8)((40.f + synth_rms * 150.f) * alpha);
            draw_rect(cx - halo_sz / 2.f, cy - halo_sz / 2.f, halo_sz, halo_sz,
                      RGBA8((heart_col >> 0) & 0xff, (heart_col >> 8) & 0xff, (heart_col >> 16) & 0xff, halo_a));
            break;
        }
        case VIZ_WAVEFORM: {
            /* Mode WAVEFORM (Dictaphone complet) : 36 barres fines denses et fluides */
            Theme *th = current_theme;
            float cy = y + h / 2.f;
            int num_wf_bars = 36;
            float bw = (w / (float)num_wf_bars) - 1.5f;

            for (int i = 0; i < num_wf_bars; i++) {
                float phase = f * 0.14f + (float)i * 0.22f;
                /* Enveloppe en parabole pour adoucir les bords gauche/droite */
                float envelope = sinf((float)i * 3.14159265f / (float)num_wf_bars);
                float amp = (0.20f + 0.75f * fabsf(sinf(phase) * cosf(phase * 0.7f))) * envelope * alpha;
                float bh = amp * h;
                if (bh < 2.f) bh = 2.f;

                float bx = x + (float)i * (bw + 1.5f);
                u32 col = th->visualizer_bars[(i * 8) / num_wf_bars];
                draw_rect(bx, cy - bh / 2.f, bw, bh, col);
            }
            break;
        }
        case VIZ_EQ:
            draw_viz_eq(&dummy_ui, x, y, w, h);
            break;
        case VIZ_OSCILLO: {
            Theme *th = current_theme;
            u32 line_col = RGBA8(0, 255, 200, (u8)(255 * alpha));
            float cy = y + h / 2.f;
            draw_rect(x, cy, w, 1, RGBA8(80, 80, 100, (u8)(180 * alpha)));

            float prev_px = -1.f, prev_py = -1.f;
            for (int i = 0; i < 80; i++) {
                float px = x + (float)i * (w / 80.f);
                float phase = f * 0.18f + (float)i * 0.15f;
                float sample = (sinf(phase) * 0.5f + sinf(phase * 2.2f) * 0.3f) * alpha;
                float py = cy - sample * (h * 0.42f);

                if (prev_px >= 0.f) {
                    float dx = px - prev_px, dy = py - prev_py;
                    int steps = (int)(fabsf(dy) / 2.f) + 1;
                    if (steps > 10) steps = 10;
                    for (int s = 0; s <= steps; s++) {
                        float t_val = (float)s / (float)steps;
                        draw_rect(prev_px + dx * t_val, prev_py + dy * t_val, 2, 2, line_col);
                    }
                } else {
                    draw_rect(px, py, 2, 2, line_col);
                }
                prev_px = px; prev_py = py;
            }
            break;
        }
        default:
            draw_viz_bars(&dummy_ui, x, y, w, h);
            break;
    }

    g_3d_eye = saved_eye; /* Restaure l'état d'origine */
}
