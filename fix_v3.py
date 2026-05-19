#!/usr/bin/env python3
# fix_v3.py

import re

# ============================================================
# FIX 1 : audio.c - Desactiver AAC (crash mpg123)
# AAC via mpg123 n'est pas fiable sur 3DS -> retirer l'extension
# ============================================================
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Retirer .aac du detect_fmt -> eviter le crash
old_aac = (
    '    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2") '
    '|| !strcasecmp(e, "aac")) return FMT_MP3;'
)
new_aac = (
    '    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2")) return FMT_MP3;\n'
    '    /* AAC desactive: mpg123 crashe sur 3DS avec AAC */'
)

if old_aac in a:
    a = a.replace(old_aac, new_aac)
    print('OK: AAC desactive dans detect_fmt')
else:
    # Chercher variante
    idx = a.find('"aac"')
    if idx >= 0:
        print('WARN: pattern AAC different, contexte:')
        print(repr(a[max(0,idx-80):idx+80]))
    else:
        print('INFO: AAC deja absent')

# FIX : Securiser le chargement MP3
# Ajouter verification du format apres mpg123_getformat
old_mp3_fmt = (
    '        long rate; int ch, enc;\n'
    '        mpg123_getformat(s_mpg, &rate, &ch, &enc);\n'
    '        mpg123_format_none(s_mpg);\n'
    '        mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);'
)
new_mp3_fmt = (
    '        long rate; int ch, enc;\n'
    '        if(mpg123_getformat(s_mpg, &rate, &ch, &enc) != MPG123_OK) {\n'
    '            mpg123_close(s_mpg); mpg123_delete(s_mpg);\n'
    '            s_mpg = NULL; return -1;\n'
    '        }\n'
    '        if(rate <= 0 || rate > 96000) rate = 44100;\n'
    '        mpg123_format_none(s_mpg);\n'
    '        mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);'
)

if old_mp3_fmt in a:
    a = a.replace(old_mp3_fmt, new_mp3_fmt)
    print('OK: mpg123_getformat securise')
else:
    print('WARN: mpg123_getformat pattern non trouve')

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# ============================================================
# FIX 2 : Freeze - audio_thread trop agressif
# Le thread ne dort pas assez quand STOPPED -> CPU a 100%
# ============================================================
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

old_thread = (
    'static void audio_thread(void *arg)\n'
    '{\n'
    '    (void)arg;\n'
    '    while (s_thread_run) {\n'
    '        if (s_state != AUDIO_PLAYING) {\n'
    '            svcSleepThread(16000000LL); /* 16 ms */\n'
    '            continue;\n'
    '        }\n'
    '\n'
    '        ndspWaveBuf *wb = &s_wbufs[s_buf_idx];\n'
    '        if (wb->status == NDSP_WBUF_QUEUED || wb->status == NDSP_WBUF_PLAYING) {\n'
    '            svcSleepThread(8000000LL); /* 8 ms */\n'
    '            continue;\n'
    '        }'
)
new_thread = (
    'static void audio_thread(void *arg)\n'
    '{\n'
    '    (void)arg;\n'
    '    while (s_thread_run) {\n'
    '        if (s_state != AUDIO_PLAYING) {\n'
    '            svcSleepThread(33000000LL); /* 33ms - libere CPU */\n'
    '            continue;\n'
    '        }\n'
    '\n'
    '        ndspWaveBuf *wb = &s_wbufs[s_buf_idx];\n'
    '        if (wb->status == NDSP_WBUF_QUEUED || wb->status == NDSP_WBUF_PLAYING) {\n'
    '            svcSleepThread(16000000LL); /* 16ms */\n'
    '            continue;\n'
    '        }'
)

if old_thread in a:
    a = a.replace(old_thread, new_thread)
    print('OK: audio_thread sleep augmente')
else:
    print('WARN: audio_thread pattern non trouve')
    idx = a.find('16000000LL')
    if idx >= 0:
        print('contexte:', repr(a[max(0,idx-100):idx+50]))

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# ============================================================
# FIX 3 : main.c - Limiter les FPS a 30 pour liberer CPU
# ============================================================
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

# Ajouter un sleep dans la boucle principale
old_draw = '        // Draw\n        draw_frame(dt);'
new_draw = (
    '        // Draw\n'
    '        draw_frame(dt);\n'
    '        // Limiter CPU: dormir 16ms entre frames\n'
    '        svcSleepThread(16000000LL);'
)

if old_draw in m:
    m = m.replace(old_draw, new_draw)
    print('OK: sleep 16ms ajoute dans boucle principale')
else:
    print('WARN: draw_frame non trouve dans main.c')
    idx = m.find('draw_frame')
    if idx >= 0:
        print('contexte:', repr(m[max(0,idx-50):idx+80]))

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

# ============================================================
# FIX 4 : Batterie - lire toutes les 60 frames
# pour eviter les appels mcuHwc trop frequents
# ============================================================
with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    p = f.read()

old_batt = (
    '    u8 batt=0;\n'
    '    mcuHwcGetBatteryLevel(&batt);\n'
    '    char bstr[16];\n'
    '    snprintf(bstr,sizeof(bstr),"BAT:%d%%",(int)batt);\n'
    '    u32 batt_col = batt<=20?th->accent2:th->text_disabled;\n'
    '    draw_text(BOT_WIDTH-65,5,0.42f,batt_col,bstr);'
)
new_batt = (
    '    static u8 batt=50; static int batt_timer=0;\n'
    '    if(batt_timer<=0) {\n'
    '        mcuHwcGetBatteryLevel(&batt);\n'
    '        batt_timer=180; /* relire toutes les 3sec ~60fps */\n'
    '    } else batt_timer--;\n'
    '    char bstr[16];\n'
    '    snprintf(bstr,sizeof(bstr),"BAT:%d%%",(int)batt);\n'
    '    u32 batt_col = batt<=20?th->accent2:th->text_disabled;\n'
    '    draw_text(BOT_WIDTH-65,5,0.42f,batt_col,bstr);'
)

if old_batt in p:
    p = p.replace(old_batt, new_batt)
    print('OK: batterie mise en cache 3sec')
else:
    print('WARN: code batterie non trouve')
    idx = p.find('mcuHwcGetBatteryLevel')
    if idx >= 0:
        print('contexte:', repr(p[max(0,idx-100):idx+200]))

with open('source/player_ui.c', 'w', encoding='utf-8') as f:
    f.write(p)

# ============================================================
# VERIFICATION
# ============================================================
print('\n=== VERIFICATION ===')
checks = [
    ('source/audio.c',    'aac',                    False, 'AAC absent'),
    ('source/audio.c',    