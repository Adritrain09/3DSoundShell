#include "power_save.h"
/* =============================================================================
   3DSoundShell - MAGNETOPHONE V1.10 (ASYNC + LISTE + LECTEUR INTEGRE)
   Interface REC + Liste des enregistrements avec lecture / renommer / supprimer
   ============================================================================= */

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include <dirent.h>
#include <math.h>
#include "recorder.h"
#include "settings.h"
#include "theme.h"

/* Forward declaration : fonction externe du main.c pour lancer la lecture */
extern void load_and_play(const char *path);

#define REC_DIR          "sdmc:/Records"
#define REC_SAMPLERATE   32728
#define MIC_BUF_SIZE     0x30000
#define ASYNC_BUF_SIZE   (512 * 1024)
#define WRITE_CHUNK      4096
#define REC_VERSION      "V1.09"  /* Modifiable facilement ici ! */

#define MAX_REC_FILES    256
#define REC_NAME_MAX     128

extern C3D_RenderTarget *g_top;
extern C3D_RenderTarget *g_bot;
#define TOP_WIDTH  400
#define TOP_HEIGHT 240
#define BOT_WIDTH  320
#define BOT_HEIGHT 240

#define BTN_CTR_X    160.0f
#define BTN_CTR_Y    130.0f
#define BTN_RADIUS   50.0f

/* Bouton toggle LED (coin superieur droit) */
#define LED_BTN_X    270.0f
#define LED_BTN_Y    10.0f
#define LED_BTN_W    44.0f
#define LED_BTN_H    22.0f

/* Bouton toggle REC/LIST (coin superieur gauche) */
#define MODE_BTN_X    6.0f
#define MODE_BTN_Y    10.0f
#define MODE_BTN_W    54.0f
#define MODE_BTN_H    22.0f

/* Boutons LIRE / SUPP (mode LIST, en bas) */
#define LIST_BTN_Y    200.0f
#define LIST_BTN_H    30.0f
#define PLAY_BTN_X    20.0f
#define PLAY_BTN_W    130.0f
#define DEL_BTN_X     170.0f
#define DEL_BTN_W     130.0f

/* Zone de liste */
#define LIST_X        6.0f
#define LIST_Y        40.0f
#define LIST_W        (BOT_WIDTH - 12.0f)
#define LIST_ITEM_H   18.0f
#define LIST_VISIBLE  8

static void rec_make_dir(void);
static void rec_find_next_filename(char *out, size_t outsz);
static void rec_write_wav_header(FILE *f, u32 data_size);
static void rec_start(void);
static void rec_stop_and_save(void);
static void rec_set_led(bool on);
static void rec_scan_files(void);
static void rec_play_selected(void);
static void rec_delete_selected(void);
static void rec_rename_selected(void);
static void draw_rect(float x, float y, float w, float h, u32 color);
static void draw_circle(float x, float y, float r, u32 color);
static void draw_line(float x1, float y1, float x2, float y2, float w, u32 color);
static void draw_text_centered(float cx, float y, float sz, u32 col, const char *txt);
static void draw_text_left(float x, float y, float sz, u32 col, const char *txt);

static u8   *s_mic_buf = NULL;
static u32   s_mic_pos = 0;
static u8   *s_async_buf = NULL;
static volatile u32 s_async_head = 0;
static volatile u32 s_async_tail = 0;

static bool  s_recording = false;
static bool  s_rec_clapet_paused = false;
static u64   s_rec_paused_elapsed = 0;
static u64   s_rec_start_time = 0;
static bool  s_mic_ready = false;
static int   s_last_file_num = 0;
static bool  s_init_fail = false;
static Result s_mic_err = 0;
static float s_ui_pulse = 0.f;
static float s_reel_angle = 0.f;

static FILE *s_rec_file = NULL;
static volatile u32 s_rec_bytes_written = 0;
static char  s_current_filename[256] = {0};

static Thread s_write_thread = NULL;
static volatile bool s_thread_run = false;
static volatile bool s_thread_finished = true;

static s16   s_vu_amplitude = 0;
static bool  s_buffer_overrun = false;

static bool  s_led_enabled = true;
static bool  s_led_was_on = false;

/* Mode d'affichage de l'écran bas : 0 = REC, 1 = LIST */
static int   s_mode = 0;

/* Securite suppression */
static bool  s_del_confirm = false;

/* Securite suppression */

/* Liste des fichiers */
static char  s_files[MAX_REC_FILES][REC_NAME_MAX];
static int   s_file_count = 0;
static int   s_list_sel = 0;
static int   s_list_scroll = 0;

static void draw_rect(float x, float y, float w, float h, u32 color)
{
    C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
}

static void draw_circle(float x, float y, float r, u32 color)
{
    C2D_DrawCircleSolid(x, y, 0.5f, r, color);
}

static void draw_line(float x1, float y1, float x2, float y2, float w, u32 color)
{
    C2D_DrawLine(x1, y1, color, x2, y2, color, w, 0.5f);
}

/* =============================================================================
   LED DE NOTIFICATION 3DS (Respiration Studio)
   ============================================================================= */
static void rec_set_led(bool on)
{
    if (!s_led_enabled) {
        if (s_led_was_on) {
            InfoLedPattern off = {0};
            MCUHWC_SetInfoLedPattern(&off);
            s_led_was_on = false;
        }
        return;
    }

    if (on && !s_led_was_on) {
        InfoLedPattern p = {0};
        p.delay = 0x10;
        p.smoothing = 0x30;
        p.loopDelay = 0x00;
        p.blinkSpeed = 0x00;

        for (int i = 0; i < 32; i++) {
            p.greenPattern[i] = 0;
            p.bluePattern[i]  = 0;
            if (i < 4) {
                p.redPattern[i] = 0;
            } else if (i < 12) {
                float t = (float)(i - 4) / 7.f;
                p.redPattern[i] = (u8)(255 * t);
            } else if (i < 20) {
                p.redPattern[i] = 255;
            } else if (i < 28) {
                float t = (float)(i - 20) / 7.f;
                p.redPattern[i] = (u8)(255 * (1.f - t));
            } else {
                p.redPattern[i] = 0;
            }
        }
        MCUHWC_SetInfoLedPattern(&p);
        s_led_was_on = true;
    } else if (!on && s_led_was_on) {
        InfoLedPattern off = {0};
        MCUHWC_SetInfoLedPattern(&off);
        s_led_was_on = false;
    }
}

static void rec_make_dir(void)
{
    mkdir(REC_DIR, 0777);
}

/* =============================================================================
   SCAN DU DOSSIER RECORDS
   ============================================================================= */
static void rec_scan_files(void)
{
    s_file_count = 0;
    DIR *d = opendir(REC_DIR);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) != NULL && s_file_count < MAX_REC_FILES) {
        const char *name = e->d_name;
        int len = strlen(name);
        if (len < 5) continue;
        if (strcasecmp(name + len - 4, ".wav") != 0) continue;
        snprintf(s_files[s_file_count], REC_NAME_MAX, "%s", name);
        s_file_count++;
    }
    closedir(d);

    /* Tri alphabétique */
    for (int i = 0; i < s_file_count - 1; i++) {
        for (int j = 0; j < s_file_count - i - 1; j++) {
            if (strcasecmp(s_files[j], s_files[j+1]) > 0) {
                char tmp[REC_NAME_MAX];
                strcpy(tmp, s_files[j]);
                strcpy(s_files[j], s_files[j+1]);
                strcpy(s_files[j+1], tmp);
            }
        }
    }

    /* Blindage sécurité d'indexation pour éviter le crash */
    if (s_file_count == 0) {
        s_list_sel = 0;
        s_list_scroll = 0;
    } else {
        if (s_list_sel >= s_file_count) s_list_sel = s_file_count - 1;
        if (s_list_sel < 0) s_list_sel = 0;
        if (s_list_scroll > s_list_sel) s_list_scroll = s_list_sel;
        if (s_list_scroll < s_list_sel - LIST_VISIBLE + 1)
            s_list_scroll = s_list_sel - LIST_VISIBLE + 1;
        if (s_list_scroll < 0) s_list_scroll = 0;
    }
}

static void rec_find_next_filename(char *out, size_t outsz)
{
    for (int i = 1; i < 999; i++) {
        snprintf(out, outsz, "%s/Rec_%d.wav", REC_DIR, i);
        FILE *f = fopen(out, "rb");
        if (!f) {
            s_last_file_num = i;
            return;
        }
        fclose(f);
    }
    snprintf(out, outsz, "%s/Rec_999.wav", REC_DIR);
}

static void rec_write_wav_header(FILE *f, u32 data_size)
{
    u32 file_size = data_size + 36;
    u32 sample_rate = REC_SAMPLERATE;
    u16 channels = 1;
    u16 bits_per_sample = 16;
    u32 byte_rate = sample_rate * channels * bits_per_sample / 8;
    u16 block_align = channels * bits_per_sample / 8;

    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    u32 fmt_size = 16;
    u16 audio_format = 1;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
}

static void rec_write_thread_func(void *arg)
{
    (void)arg;
    u8 local_chunk[WRITE_CHUNK];
    s_thread_finished = false;

    while (s_thread_run) {
        u32 head = s_async_head;
        u32 tail = s_async_tail;
        u32 avail = 0;

        if (tail >= head) avail = tail - head;
        else avail = (ASYNC_BUF_SIZE - head) + tail;

        if (avail >= WRITE_CHUNK) {
            u32 to_write = WRITE_CHUNK;
            for (u32 i = 0; i < to_write; i++) {
                local_chunk[i] = s_async_buf[(head + i) % ASYNC_BUF_SIZE];
            }
            if (s_rec_file) {
                fwrite(local_chunk, 1, to_write, s_rec_file);
                s_rec_bytes_written += to_write;
            }
            s_async_head = (head + to_write) % ASYNC_BUF_SIZE;
        } else {
            svcSleepThread(5000000ULL);
        }
    }

    u32 head = s_async_head;
    u32 tail = s_async_tail;
    u32 avail = 0;
    if (tail >= head) avail = tail - head;
    else avail = (ASYNC_BUF_SIZE - head) + tail;

    while (avail > 0 && s_rec_file) {
        u32 to_write = (avail > WRITE_CHUNK) ? WRITE_CHUNK : avail;
        for (u32 i = 0; i < to_write; i++) {
            local_chunk[i] = s_async_buf[(head + i) % ASYNC_BUF_SIZE];
        }
        fwrite(local_chunk, 1, to_write, s_rec_file);
        s_rec_bytes_written += to_write;
        head = (head + to_write) % ASYNC_BUF_SIZE;
        s_async_head = head;

        tail = s_async_tail;
        if (tail >= head) avail = tail - head;
        else avail = (ASYNC_BUF_SIZE - head) + tail;
    }

    s_thread_finished = true;
}

static void rec_kill_write_thread(void)
{
    if (s_write_thread) {
        s_thread_run = false;
        int timeout_ms = 3000;
        while (!s_thread_finished && timeout_ms > 0) {
            svcSleepThread(10000000ULL);
            timeout_ms -= 10;
        }
        threadJoin(s_write_thread, U64_MAX);
        threadFree(s_write_thread);
        s_write_thread = NULL;
    }
}

void recorder_enter(void)
{
    rec_make_dir();
    s_recording = false;
    s_rec_start_time = 0;
    s_init_fail = false;
    s_rec_bytes_written = 0;
    s_mic_pos = 0;
    s_mic_err = 0;
    s_vu_amplitude = 0;
    s_reel_angle = 0.f;
    s_buffer_overrun = false;
    s_thread_finished = true;
    s_led_was_on = false;

    if (!s_mic_buf) {
        s_mic_buf = (u8*)memalign(0x1000, MIC_BUF_SIZE);
        if (!s_mic_buf) { s_init_fail = true; return; }
    }
    if (!s_async_buf) {
        s_async_buf = (u8*)malloc(ASYNC_BUF_SIZE);
        if (!s_async_buf) { s_init_fail = true; return; }
    }

    if (!s_mic_ready) {
        Result rc = micInit(s_mic_buf, MIC_BUF_SIZE);
        if (R_FAILED(rc)) {
            s_mic_err = rc;
            s_init_fail = true;
        } else {
            s_mic_ready = true;
            MICU_SetPower(true);
            MICU_SetGain(7);
            MICU_SetClamp(true);
        }
    }

    /* Scan initial */
    rec_scan_files();
}

void recorder_leave(void)
{
    if (s_recording) rec_stop_and_save();
    rec_kill_write_thread();
    rec_set_led(false);

    if (s_rec_file) {
        fclose(s_rec_file);
        s_rec_file = NULL;
    }

    if (s_mic_ready) {
        MICU_SetPower(false);
        micExit();
        s_mic_ready = false;
    }
    if (s_mic_buf) { free(s_mic_buf); s_mic_buf = NULL; }
    if (s_async_buf) { free(s_async_buf); s_async_buf = NULL; }
}

static void rec_start(void)
{
    if (!s_mic_ready || s_recording || s_init_fail) return;

    rec_kill_write_thread();

    s_rec_bytes_written = 0;
    s_mic_pos = 0;
    s_async_head = 0;
    s_async_tail = 0;
    s_vu_amplitude = 0;
    s_buffer_overrun = false;
    memset(s_mic_buf, 0, MIC_BUF_SIZE);
    memset(s_async_buf, 0, ASYNC_BUF_SIZE);

    rec_find_next_filename(s_current_filename, sizeof(s_current_filename));
    s_rec_file = fopen(s_current_filename, "wb");
    if (!s_rec_file) { s_init_fail = true; return; }
    rec_write_wav_header(s_rec_file, 0);

    s_thread_run = true;
    s_thread_finished = false;
    s_write_thread = threadCreate(rec_write_thread_func, NULL, 16 * 1024, 0x31, -1, false);
    if (!s_write_thread) {
        fclose(s_rec_file);
        s_rec_file = NULL;
        s_thread_run = false;
        s_thread_finished = true;
        s_init_fail = true;
        return;
    }

    Result rc = MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED,
                                   MICU_SAMPLE_RATE_32730, 0,
                                   micGetSampleDataSize(), true);
    if (R_SUCCEEDED(rc)) {
        s_recording = true;
        aptSetSleepAllowed(false);
        s_rec_start_time = osGetTime();
        rec_set_led(true);
    } else {
        rec_kill_write_thread();
        fclose(s_rec_file);
        s_rec_file = NULL;
        s_mic_err = rc;
        s_init_fail = true;
    }
}

static void rec_stop_and_save(void)
{
    if (s_rec_clapet_paused) return;
    if (!s_recording || !s_rec_file) return;

    MICU_StopSampling();
    s_recording = false;
    { extern Settings g_settings; aptSetSleepAllowed(!g_settings.lid_playback_enabled); }
    rec_set_led(false);

    u32 micbuf_readpos = s_mic_pos;
    s_mic_pos = micGetLastSampleOffset();
    u32 mic_datasize = micGetSampleDataSize();

    while (micbuf_readpos != s_mic_pos) {
        u32 next = (micbuf_readpos + 1) % mic_datasize;
        s16 sample = (s16)((s_mic_buf[micbuf_readpos]) | (s_mic_buf[next] << 8));
        s32 boosted = (s32)sample * 8;
        if (boosted > 32767) boosted = 32767;
        if (boosted < -32768) boosted = -32768;
        u8 b1 = (u8)(boosted & 0xFF);
        u8 b2 = (u8)((boosted >> 8) & 0xFF);
        u32 next_tail2 = (s_async_tail + 2) % ASYNC_BUF_SIZE;
        if (next_tail2 != s_async_head) {
            s_async_buf[s_async_tail] = b1;
            s_async_buf[(s_async_tail + 1) % ASYNC_BUF_SIZE] = b2;
            s_async_tail = next_tail2;
        }
        micbuf_readpos = (micbuf_readpos + 2) % mic_datasize;
    }

    rec_kill_write_thread();

    if (s_rec_file) {
        rec_write_wav_header(s_rec_file, s_rec_bytes_written);
        fclose(s_rec_file);
        s_rec_file = NULL;
    }

    /* Rafraîchir la liste après enregistrement */
    rec_scan_files();
}

/* =============================================================================
   ACTIONS SUR LA LISTE
   ============================================================================= */
static void rec_play_selected(void)
{
    if (s_file_count == 0 || s_list_sel < 0 || s_list_sel >= s_file_count) return;
    if (s_recording) rec_stop_and_save();

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", REC_DIR, s_files[s_list_sel]);

    /* Nettoyage complet du magnetophone AVANT lecture (sinon crash mémoire) */
    recorder_leave();

    /* Lancer la lecture (change automatiquement l'état vers STATE_PLAYER) */
    load_and_play(full_path);
}

static void rec_delete_selected(void)
{
    if (s_file_count == 0 || s_list_sel < 0 || s_list_sel >= s_file_count) return;
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", REC_DIR, s_files[s_list_sel]);
    remove(full_path);

    /* Recalcule de l'index sélectionné avant le scan pour éviter le out-of-bounds */
    if (s_list_sel >= s_file_count - 1) {
        s_list_sel = s_file_count - 2;
    }
    if (s_list_sel < 0) s_list_sel = 0;

    rec_scan_files();
}

static void rec_rename_selected(void)
{
    if (s_file_count == 0 || s_list_sel < 0 || s_list_sel >= s_file_count) return;

    char new_name[REC_NAME_MAX] = {0};
    /* Pré-remplir avec le nom actuel sans extension */
    snprintf(new_name, sizeof(new_name), "%s", s_files[s_list_sel]);
    int len = strlen(new_name);
    if (len > 4 && strcasecmp(new_name + len - 4, ".wav") == 0) {
        new_name[len - 4] = 0;
    }

    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, REC_NAME_MAX - 8);
    swkbdSetInitialText(&kb, new_name);
    swkbdSetHintText(&kb, "Nouveau nom du fichier");
    swkbdSetValidation(&kb, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);

    SwkbdButton btn = swkbdInputText(&kb, new_name, sizeof(new_name));
    if (btn != SWKBD_BUTTON_CONFIRM) return;

    /* Nettoyer les caractères invalides pour FAT */
    for (int i = 0; new_name[i]; i++) {
        char c = new_name[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            new_name[i] = '_';
        }
    }

    char old_path[512], new_path[512];
    snprintf(old_path, sizeof(old_path), "%s/%s", REC_DIR, s_files[s_list_sel]);
    snprintf(new_path, sizeof(new_path), "%s/%s.wav", REC_DIR, new_name);
    rename(old_path, new_path);
    rec_scan_files();
}

/* =============================================================================
   GESTION DES ENTREES
   ============================================================================= */
void recorder_input(int *state)
{
    u32 kd = hidKeysDown();
    if (power_save_is_b_locked(hidKeysHeld())) { kd = 0; }
    touchPosition tp;
    hidTouchRead(&tp);

    /* START = retour aux paramètres n'importe où */
    if (kd & KEY_START) {
        if (s_recording) rec_stop_and_save();
        recorder_leave();
        *state = 3;
        return;
    }

    /* B = retour */
    if (kd & KEY_B) {
        if (s_recording) rec_stop_and_save();
        recorder_leave();
        *state = 3;
        return;
    }

    /* A = toggle REC/STOP */
    if ((kd & KEY_A) && !s_init_fail) {
        if (s_recording) rec_stop_and_save();
        else rec_start();
    }

    /* X = toggle LED */
    if (kd & KEY_X) {
        s_led_enabled = !s_led_enabled;
        if (!s_led_enabled) rec_set_led(false);
        else if (s_recording) rec_set_led(true);
    }

    /* Mode LIST : navigation */
    if (s_mode == 1) {
        /* Tout appui de touche physique réinitialise la confirmation de suppression */
        if (kd & (KEY_DUP | KEY_DDOWN | KEY_Y | KEY_A | KEY_X)) {
            s_del_confirm = false;
        }

        if (kd & KEY_DUP) {
            if (s_list_sel > 0) s_list_sel--;
            if (s_list_sel < s_list_scroll) s_list_scroll = s_list_sel;
        }
        if (kd & KEY_DDOWN) {
            if (s_list_sel < s_file_count - 1) s_list_sel++;
            if (s_list_sel >= s_list_scroll + LIST_VISIBLE)
                s_list_scroll = s_list_sel - LIST_VISIBLE + 1;
        }
        if (kd & KEY_Y) {
            rec_rename_selected();
        }
    }

    /* Interactions tactiles */
    if ((kd & KEY_TOUCH) && !s_init_fail) {
        /* Mode LIST : Si on clique n'importe où sauf sur le bouton SUPPRIMER, on annule la confirmation */
        if (s_mode == 1) {
            bool on_del_btn = (tp.py >= (int)LIST_BTN_Y && tp.py < (int)(LIST_BTN_Y + LIST_BTN_H) &&
                               tp.px >= (int)DEL_BTN_X && tp.px < (int)(DEL_BTN_X + DEL_BTN_W));
            if (!on_del_btn) {
                s_del_confirm = false;
            }
        }

        /* Toggle LED */
        if (tp.px >= (int)LED_BTN_X && tp.px < (int)(LED_BTN_X + LED_BTN_W) &&
            tp.py >= (int)LED_BTN_Y && tp.py < (int)(LED_BTN_Y + LED_BTN_H)) {
            s_led_enabled = !s_led_enabled;
            if (!s_led_enabled) rec_set_led(false);
            else if (s_recording) rec_set_led(true);
            return;
        }

        /* Toggle MODE REC / LIST */
        if (tp.px >= (int)MODE_BTN_X && tp.px < (int)(MODE_BTN_X + MODE_BTN_W) &&
            tp.py >= (int)MODE_BTN_Y && tp.py < (int)(MODE_BTN_Y + MODE_BTN_H)) {
            s_mode = 1 - s_mode;
            if (s_mode == 1) rec_scan_files();
            return;
        }

        if (s_mode == 0) {
            /* Mode REC : bouton circulaire */
            float dx = (float)tp.px - BTN_CTR_X;
            float dy = (float)tp.py - BTN_CTR_Y;
            if ((dx * dx + dy * dy) <= (BTN_RADIUS * BTN_RADIUS)) {
                if (s_recording) rec_stop_and_save();
                else rec_start();
            }
        } else {
            /* Mode LIST */
            if (tp.py >= (int)LIST_BTN_Y && tp.py < (int)(LIST_BTN_Y + LIST_BTN_H)) {
                /* Bouton LIRE */
                if (tp.px >= (int)PLAY_BTN_X && tp.px < (int)(PLAY_BTN_X + PLAY_BTN_W)) {
                    rec_play_selected();
                    return;
                }
                /* Bouton SUPPRIMER avec validation */
                if (tp.px >= (int)DEL_BTN_X && tp.px < (int)(DEL_BTN_X + DEL_BTN_W)) {
                    if (s_file_count > 0) {
                        if (s_del_confirm) {
                            rec_delete_selected();
                            s_del_confirm = false;
                        } else {
                            s_del_confirm = true;
                        }
                    }
                    return;
                }
            }

            /* Sélection tactile dans la liste */
            if (tp.py >= (int)LIST_Y && tp.py < (int)(LIST_Y + LIST_VISIBLE * LIST_ITEM_H)) {
                int idx = s_list_scroll + (int)((tp.py - LIST_Y) / LIST_ITEM_H);
                if (idx >= 0 && idx < s_file_count) {
                    s_list_sel = idx;
                }
            }
        }
    }
}

void recorder_pause_for_sleep(void) {
    if (!s_recording || s_rec_clapet_paused) return;
    s_rec_paused_elapsed = osGetTime() - s_rec_start_time;
    MICU_StopSampling();
    rec_set_led(false);
    s_rec_clapet_paused = true;
}

void recorder_resume_from_sleep(void) {
    if (!s_recording || !s_rec_clapet_paused) return;
    s_rec_clapet_paused = false;
    MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED, MICU_SAMPLE_RATE_32730, 0, micGetSampleDataSize(), true);
    s_rec_start_time = osGetTime() - s_rec_paused_elapsed;
    rec_set_led(true);
}

void recorder_update(void)
{
    s_ui_pulse += 0.12f;
    if (s_ui_pulse > 6.28f) s_ui_pulse -= 6.28f;

    u32 mic_datasize = micGetSampleDataSize();
    if (s_rec_clapet_paused) return;
    if (!s_recording || !s_rec_file || mic_datasize == 0) {
        s_vu_amplitude = (s_vu_amplitude * 7) / 8;
        return;
    }

    s_reel_angle += 0.08f;
    if (s_reel_angle > 6.28f) s_reel_angle -= 6.28f;

    u32 micbuf_readpos = s_mic_pos;
    s_mic_pos = micGetLastSampleOffset();

    s16 max_sample_this_frame = 0;

    while (micbuf_readpos != s_mic_pos) {
        u32 next = (micbuf_readpos + 1) % mic_datasize;
        s16 sample = (s16)((s_mic_buf[micbuf_readpos]) | (s_mic_buf[next] << 8));
        s16 abs_sample = sample < 0 ? (sample == -32768 ? 32767 : -sample) : sample;
        if (abs_sample > max_sample_this_frame) max_sample_this_frame = abs_sample;

        s32 boosted = (s32)sample * 8;
        if (boosted > 32767) boosted = 32767;
        if (boosted < -32768) boosted = -32768;

        u8 b1 = (u8)(boosted & 0xFF);
        u8 b2 = (u8)((boosted >> 8) & 0xFF);
        u32 next_tail2 = (s_async_tail + 2) % ASYNC_BUF_SIZE;
        if (next_tail2 != s_async_head) {
            s_async_buf[s_async_tail] = b1;
            s_async_buf[(s_async_tail + 1) % ASYNC_BUF_SIZE] = b2;
            s_async_tail = next_tail2;
        } else {
            s_buffer_overrun = true;
        }
        micbuf_readpos = (micbuf_readpos + 2) % mic_datasize;
    }

    s_vu_amplitude = (s16)(((s_vu_amplitude * 4) + (max_sample_this_frame * 8)) / 5);
}

int recorder_is_active(void) { return s_recording ? 1 : 0; }

/* Meme piege deja rencontre et documente ailleurs dans ce projet
   (player_ui.c, "BUFFER TEXTE OPTIMISE") : un C2D_TextBuf par draw_text()
   (ici en plus alloue ET libere a chaque appel, pire que le simple Clear deja
   identifie comme cause de "fragmentation memoire -> micro-freezes") -
   l'ecran magnetophone affiche 10-20 textes par image, soit 10-20
   allocations/liberations de tampon de texte (memoire lineaire GPU) par
   image en continu tant que cet ecran est ouvert. Meme motif que
   player_ui.c : UN SEUL tampon statique, cree une fois, vide une fois par
   image (voir rec_text_frame_begin, appele en tete de
   recorder_draw_top/recorder_draw_bottom). */
static C2D_TextBuf s_rec_textbuf = NULL;

static void rec_text_frame_begin(void)
{
    if (!s_rec_textbuf)
        s_rec_textbuf = C2D_TextBufNew(4096);
    C2D_TextBufClear(s_rec_textbuf);
}

static void draw_text_centered(float cx, float y, float sz, u32 col, const char *txt)
{
    C2D_Text t;
    float w, h;
    if (!s_rec_textbuf || !txt || !txt[0]) return;
    C2D_TextParse(&t, s_rec_textbuf, txt);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sz, sz, &w, &h);
    C2D_DrawText(&t, C2D_WithColor, cx - w / 2.f, y, 0.5f, sz, sz, col);
}

static void draw_text_left(float x, float y, float sz, u32 col, const char *txt)
{
    C2D_Text t;
    if (!s_rec_textbuf || !txt || !txt[0]) return;
    C2D_TextParse(&t, s_rec_textbuf, txt);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, sz, sz, col);
}

static void draw_cassette_reel(float cx, float cy, float r, float angle, u32 col_rim, u32 col_core)
{
    draw_circle(cx, cy, r, col_rim);
    draw_circle(cx, cy, r - 3.f, C2D_Color32(15, 15, 20, 255));
    draw_circle(cx, cy, r - 10.f, col_core);
    draw_circle(cx, cy, 6.f, C2D_Color32(10, 10, 12, 255));
    for (int i = 0; i < 3; i++) {
        float a = angle + (float)i * 2.094f;
        float hole_x = cx + cosf(a) * (r - 7.f);
        float hole_y = cy + sinf(a) * (r - 7.f);
        draw_circle(hole_x, hole_y, 4.f, C2D_Color32(15, 15, 20, 255));
    }
}

void recorder_draw_top(void)
{
    Theme *th = current_theme;

    rec_text_frame_begin();
    C2D_TargetClear(g_top, th->bg_primary);
    C2D_SceneBegin(g_top);

    draw_rect(0, 0, TOP_WIDTH, 4, th->text_accent);
    draw_rect(0, TOP_HEIGHT - 2, TOP_WIDTH, 2, th->border);

    /* Titre unique MAGNETOPHONE - V1.## */
    char title[64];
    snprintf(title, sizeof(title), "MAGNETOPHONE - %s", REC_VERSION);
    draw_text_centered(TOP_WIDTH / 2.f, 12, 0.65f, th->text_primary, title);
    draw_text_centered(TOP_WIDTH / 2.f, 36, 0.38f, th->text_secondary, "DIRECT FLUX SD  -  BOOST INTEGRE x8");

    float reel_y = 125.f;
    float reel_r = 28.f;
    float l_reel_x = 100.f;
    float r_reel_x = 300.f;

    draw_line(l_reel_x, reel_y + reel_r - 2.f, r_reel_x, reel_y + reel_r - 2.f, 2.f, th->border);
    draw_cassette_reel(l_reel_x, reel_y, reel_r, s_reel_angle, th->border, th->text_accent);
    draw_cassette_reel(r_reel_x, reel_y, reel_r, s_reel_angle, th->border, th->text_accent);

    if (s_init_fail) {
        char err_msg[128];
        snprintf(err_msg, sizeof err_msg, "ERREUR D'INIT (Code: %08lX)", (unsigned long)s_mic_err);
        draw_text_centered(TOP_WIDTH / 2.f, 72, 0.5f, C2D_Color32(240, 70, 70, 255), err_msg);
    }
    else if (s_recording) {
        float pulse = 0.5f + 0.5f * __builtin_sin(s_ui_pulse * 1.5f);
        u8 alpha = (u8)(140 + 115 * pulse);
        draw_circle(170, 72, 6, C2D_Color32(255, 30, 30, alpha));
        draw_text_centered(TOP_WIDTH / 2.f + 12, 63, 0.8f, th->text_accent, "REC");

        u64 elapsed = (osGetTime() - s_rec_start_time) / 1000;
        char time_str[32];
        snprintf(time_str, sizeof time_str, "%02lld:%02lld", elapsed / 60, elapsed % 60);
        draw_text_centered(TOP_WIDTH / 2.f, 155, 0.9f, th->text_primary, time_str);

        char size_str[64];
        snprintf(size_str, sizeof size_str, "Taille : %.2f MB", (float)s_rec_bytes_written / (1024.f * 1024.f));
        draw_text_centered(TOP_WIDTH / 2.f, 182, 0.42f, th->text_secondary, size_str);

        if (s_buffer_overrun) {
            draw_text_centered(TOP_WIDTH / 2.f, 196, 0.35f, C2D_Color32(255, 100, 50, 255), "SATURATION TAMPON!");
        }
    } 
    else {
        draw_text_centered(TOP_WIDTH / 2.f, 72, 0.55f, th->text_secondary, "PRET A L'ENREGISTREMENT");
        char info[64];
        snprintf(info, sizeof info, "%d enregistrement(s) sur la SD", s_file_count);
        draw_text_centered(TOP_WIDTH / 2.f, 162, 0.45f, th->text_accent, info);
    }

    /* VU-metre */
    float vu_level = (float)s_vu_amplitude / 32768.f;
    if (vu_level > 1.f) vu_level = 1.f;
    int max_bars = 24;
    int bars_active = (int)(vu_level * (float)max_bars);

    float vu_x = 60.f;
    float vu_y = 210.f;
    float vu_w = 280.f;
    float vu_h = 6.f;
    float bar_w = (vu_w / (float)max_bars) - 2.f;

    draw_rect(vu_x - 6, vu_y - 12, vu_w + 12, vu_h + 15, th->border);
    draw_rect(vu_x - 4, vu_y - 10, vu_w + 8, vu_h + 12, th->bg_primary);

    draw_text_centered(vu_x + 5.f, vu_y - 11, 0.3f, th->text_secondary, "-24");
    draw_text_centered(vu_x + (vu_w / 2.f), vu_y - 11, 0.3f, th->text_secondary, "-12");
    draw_text_centered(vu_x + (vu_w * 0.83f), vu_y - 11, 0.3f, th->text_accent, "-3");
    draw_text_centered(vu_x + vu_w - 10.f, vu_y - 11, 0.3f, C2D_Color32(230, 50, 50, 255), "CLIP");

    for (int i = 0; i < max_bars; i++) {
        u32 color = th->bg_secondary;
        if (i < bars_active) {
            if (i < 16) color = th->progress_fill;
            else if (i < 20) color = th->text_accent;
            else color = C2D_Color32(255, 50, 50, 255);
        }
        draw_rect(vu_x + (float)i * (bar_w + 2.f), vu_y, bar_w, vu_h, color);
    }

    draw_text_centered(TOP_WIDTH / 2.f, 226, 0.32f, th->text_secondary, "B/START = RETOUR  -  A = REC  -  X = LED");
}

void recorder_draw_bottom(void)
{
    Theme *th = current_theme;

    rec_text_frame_begin();
    C2D_TargetClear(g_bot, th->bg_primary);
    C2D_SceneBegin(g_bot);

    /* Grille décorative */
    for (int i = 0; i < BOT_WIDTH; i += 8) {
        draw_rect((float)i, 0, 1, BOT_HEIGHT, th->bg_secondary);
    }
    for (int i = 0; i < BOT_HEIGHT; i += 24) {
        draw_rect(0, (float)i, BOT_WIDTH, 1, th->border);
    }

    /* Bouton toggle MODE REC/LIST (haut gauche) */
    u32 mode_btn_bg = (s_mode == 1) ? th->text_accent : th->bg_secondary;
    u32 mode_btn_fg = (s_mode == 1) ? th->bg_primary : th->text_primary;
    draw_rect(MODE_BTN_X, MODE_BTN_Y, MODE_BTN_W, MODE_BTN_H, mode_btn_bg);
    draw_rect(MODE_BTN_X, MODE_BTN_Y, MODE_BTN_W, 2, th->border);
    draw_text_centered(MODE_BTN_X + MODE_BTN_W / 2.f, MODE_BTN_Y + 5, 0.4f, mode_btn_fg,
                       (s_mode == 0) ? "LISTE" : "REC");

    /* Bouton toggle LED (haut droite) */
    u32 led_btn_bg = s_led_enabled ? C2D_Color32(200, 40, 40, 255) : th->bg_secondary;
    u32 led_btn_fg = s_led_enabled ? C2D_Color32(255, 255, 255, 255) : th->text_secondary;
    draw_rect(LED_BTN_X, LED_BTN_Y, LED_BTN_W, LED_BTN_H, led_btn_bg);
    draw_rect(LED_BTN_X, LED_BTN_Y, LED_BTN_W, 2, th->border);
    draw_circle(LED_BTN_X + 10.f, LED_BTN_Y + 11.f, 4.f, led_btn_fg);
    draw_text_centered(LED_BTN_X + 28.f, LED_BTN_Y + 5, 0.35f, led_btn_fg, "LED");

    if (s_mode == 0) {
        /* ============================ MODE REC ============================ */
        if (s_init_fail) {
            draw_circle(BTN_CTR_X, BTN_CTR_Y, BTN_RADIUS, th->bg_secondary);
            draw_text_centered(BOT_WIDTH / 2.f, BTN_CTR_Y - 10, 0.5f, th->text_secondary, "MICRO");
            draw_text_centered(BOT_WIDTH / 2.f, BTN_CTR_Y + 10, 0.5f, th->text_secondary, "INDISPONIBLE");
        } else {
            float glow_r = BTN_RADIUS + 4.f;
            if (s_recording) {
                float pulse = 0.5f + 0.5f * __builtin_sin(s_ui_pulse * 3.f);
                glow_r += pulse * 4.f;
                draw_circle(BTN_CTR_X, BTN_CTR_Y, glow_r, C2D_Color32(240, 40, 40, 60));
                draw_circle(BTN_CTR_X, BTN_CTR_Y, BTN_RADIUS + 1.f, th->text_accent);
                draw_circle(BTN_CTR_X, BTN_CTR_Y, BTN_RADIUS, th->bg_secondary);
                draw_rect(BTN_CTR_X - 14.f, BTN_CTR_Y - 14.f, 28.f, 28.f, C2D_Color32(240, 40, 40, 255));
            } else {
                draw_circle(BTN_CTR_X, BTN_CTR_Y, glow_r, C2D_Color32(30, 180, 50, 40));
                draw_circle(BTN_CTR_X, BTN_CTR_Y, BTN_RADIUS + 1.f, th->progress_fill);
                draw_circle(BTN_CTR_X, BTN_CTR_Y, BTN_RADIUS, th->bg_secondary);
                draw_circle(BTN_CTR_X, BTN_CTR_Y, 16.f, th->progress_fill);
            }
        }
        draw_text_centered(BOT_WIDTH / 2.f, 200, 0.42f, th->text_secondary, "APPUYER SUR LE COMMUTATEUR");
        draw_text_centered(BOT_WIDTH / 2.f, 220, 0.38f, th->text_secondary, "SD:/Records/");
    } else {
        /* ============================ MODE LIST ============================ */
        if (s_file_count == 0) {
            draw_text_centered(BOT_WIDTH / 2.f, 100, 0.5f, th->text_secondary, "AUCUN ENREGISTREMENT");
            draw_text_centered(BOT_WIDTH / 2.f, 120, 0.4f, th->text_secondary, "Bascule sur REC pour en creer");
        } else {
            /* Fond de liste */
            draw_rect(LIST_X, LIST_Y, LIST_W, LIST_VISIBLE * LIST_ITEM_H, th->bg_secondary);
            draw_rect(LIST_X, LIST_Y, LIST_W, 1, th->border);

            /* Elements */
            int end = s_list_scroll + LIST_VISIBLE;
            if (end > s_file_count) end = s_file_count;
            for (int i = s_list_scroll; i < end; i++) {
                float y = LIST_Y + (float)(i - s_list_scroll) * LIST_ITEM_H;
                if (i == s_list_sel) {
                    draw_rect(LIST_X, y, LIST_W, LIST_ITEM_H, th->text_accent);
                    draw_text_left(LIST_X + 6.f, y + 3.f, 0.42f, th->bg_primary, s_files[i]);
                } else {
                    draw_text_left(LIST_X + 6.f, y + 3.f, 0.42f, th->text_primary, s_files[i]);
                }
            }

            /* Indicateur de scroll (position/total) */
            char scroll_info[32];
            snprintf(scroll_info, sizeof(scroll_info), "%d/%d", s_list_sel + 1, s_file_count);
            draw_text_centered(BOT_WIDTH / 2.f, 185, 0.35f, th->text_secondary, scroll_info);
        }

        /* Boutons LIRE / SUPP */
        bool has_sel = (s_file_count > 0);
        u32 play_bg = has_sel ? C2D_Color32(30, 160, 50, 255) : th->bg_secondary;
        u32 del_bg = has_sel ? C2D_Color32(180, 40, 40, 255) : th->bg_secondary;
        u32 btn_fg = has_sel ? C2D_Color32(255, 255, 255, 255) : th->text_secondary;

        draw_rect(PLAY_BTN_X, LIST_BTN_Y, PLAY_BTN_W, LIST_BTN_H, play_bg);
        draw_rect(PLAY_BTN_X, LIST_BTN_Y, PLAY_BTN_W, 2, th->border);
        draw_text_centered(PLAY_BTN_X + PLAY_BTN_W / 2.f, LIST_BTN_Y + 8, 0.5f, btn_fg, "LIRE");

        /* Couleur du bouton supprimer dynamique */
        u32 final_del_bg = del_bg;
        const char *del_txt = "SUPPRIMER";
        if (s_del_confirm && has_sel) {
            /* Rouge vif clignotant pour confirmation */
            float pulse = 0.5f + 0.5f * __builtin_sin(s_ui_pulse * 4.0f);
            u8 red_pulse = (u8)(170 + 85 * pulse);
            final_del_bg = C2D_Color32(red_pulse, 20, 20, 255);
            del_txt = "CONFIRMER ?";
        }

        draw_rect(DEL_BTN_X, LIST_BTN_Y, DEL_BTN_W, LIST_BTN_H, final_del_bg);
        draw_rect(DEL_BTN_X, LIST_BTN_Y, DEL_BTN_W, 2, th->border);
        draw_text_centered(DEL_BTN_X + DEL_BTN_W / 2.f, LIST_BTN_Y + 8, 0.5f, btn_fg, del_txt);

        draw_text_centered(BOT_WIDTH / 2.f, 232, 0.32f, th->text_secondary, "Y = RENOMMER");
    }
}
