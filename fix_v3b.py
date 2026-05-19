#!/usr/bin/env python3
# fix_v3b.py - fixes separes pour eviter erreurs syntaxe

# ============================================================
# FIX 1 : audio.c - Desactiver AAC + securiser MP3
# ============================================================
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Desactiver AAC
old1 = '    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2") || !strcasecmp(e, "aac")) return FMT_MP3;'
new1 = '    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2")) return FMT_MP3;'
if old1 in a:
    a = a.replace(old1, new1)
    print('OK: AAC desactive')
else:
    print('INFO: AAC deja absent ou pattern different')
    idx = a.find('"aac"')
    if idx >= 0:
        print('  trouve aac a ligne:', a[:idx].count('\n')+1)

# Securiser mpg123_getformat
old2 = '        long rate; int ch, enc;\n        mpg123_getformat(s_mpg, &rate, &ch, &enc);\n        mpg123_format_none(s_mpg);\n        mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);'
new2 = '        long rate; int ch, enc;\n        if(mpg123_getformat(s_mpg,&rate,&ch,&enc)!=MPG123_OK){\n            mpg123_close(s_mpg);mpg123_delete(s_mpg);s_mpg=NULL;return -1;\n        }\n        if(rate<=0||rate>96000) rate=44100;\n        mpg123_format_none(s_mpg);\n        mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);'
if old2 in a:
    a = a.replace(old2, new2)
    print('OK: mpg123 securise')
else:
    print('WARN: mpg123_getformat pattern non trouve')

# Thread sleep augmente
old3 = '            svcSleepThread(16000000LL); /* 16 ms */'
new3 = '            svcSleepThread(33000000LL); /* 33ms */'
if old3 in a:
    a = a.replace(old3, new3)
    print('OK: thread sleep 33ms')
else:
    print('WARN: thread sleep non trouve')
    count = a.count('16000000LL')
    print('  occurrences 16000000LL:', count)

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# ============================================================
# FIX 2 : main.c - Sleep dans boucle principale
# ============================================================
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

old4 = '        // Draw\n        draw_frame(dt);'
new4 = '        // Draw\n        draw_frame(dt);\n        svcSleepThread(16000000LL);'
if old4 in m:
    m = m.replace(old4, new4)
    print('OK: sleep 16ms boucle principale')
elif 'svcSleepThread(16000000LL)' in m:
    print('INFO: sleep deja present dans main')
else:
    print('WARN: draw_frame non trouve')
    idx = m.find('draw_frame')
    if idx >= 0:
        print('  contexte:', repr(m[max(0,idx-30):idx+50]))

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

# ============================================================
# FIX 3 : player_ui.c - Cache batterie
# ============================================================
with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    p = f.read()

old5 = '    static u8 batt=50; static int batt_timer=0;\n    if(batt_timer<=0) {\n        mcuHwcGetBatteryLevel(&batt);\n        batt_timer=180;\n    } else batt_timer--;'
if old5 in p:
    print('INFO: cache batterie deja present')
else:
    old5b = '    u8 batt=0;\n    mcuHwcGetBatteryLevel(&batt);'
    new5b = '    static u8 batt=50; static int batt_timer=0;\n    if(batt_timer<=0){\n        mcuHwcGetBatteryLevel(&batt);\n        batt_timer=180;\n    } else batt_timer--;'
    if old5b in p:
        p = p.replace(old5b, new5b)
        print('OK: cache batterie 3sec')
    else:
        print('WARN: code batterie non trouve')
        idx = p.find('mcuHwcGetBatteryLevel')
        if idx >= 0:
            print('  contexte:', repr(p[max(0,idx-50):idx+100]))
        idx2 = p.find('PTMU_GetBatteryLevel')
        if idx2 >= 0:
            print('  encore PTMU!:', repr(p[max(0,idx2-50):idx2+100]))

with open('source/player_ui.c', 'w', encoding='utf-8') as f:
    f.write(p)

# ============================================================
# VERIFICATION FINALE
# ============================================================
print('\n=== VERIFICATION ===')
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()
with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    p = f.read()

print('AAC absent audio.c:', '"aac"' not in a)
print('33ms thread audio.c:', '33000000LL' in a)
print('sleep main.c:', 'svcSleepThread(16000000LL)' in m)
print('cache batt player_ui.c:', 'batt_timer' in p)
print('mcuHwc player_ui.c:', 'mcuHwcGetBatteryLevel' in p)
print('PTMU absent player_ui.c:', 'PTMU_GetBatteryLevel' not in p)
print('\nLance: make clean && make 2>&1 | grep error:')