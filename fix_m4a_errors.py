#!/usr/bin/env python3
# fix_m4a_errors.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Fix 1: Supprimer le case FMT_M4A mal place dans decode_chunk
old_wrong = (
    '    case FMT_M4A: {\n'
    '        /* M4A via mpg123 - fonctionne si mpg123 supporte AAC */\n'
    '        int err;\n'
    '        s_mpg = mpg123_new(NULL, &err);\n'
    '        if (!s_mpg) return -1;\n'
    '        /* Ne pas forcer le format - laisser mpg123 detecter */\n'
    '        if (mpg123_open(s_mpg, path) != MPG123_OK) {\n'
    '            mpg123_delete(s_mpg); s_mpg = NULL; return -2;\n'
    '        }\n'
    '        long rate; int ch, enc;\n'
    '        if(mpg123_getformat(s_mpg,&rate,&ch,&enc)!=MPG123_OK){\n'
    '            mpg123_close(s_mpg);mpg123_delete(s_mpg);s_mpg=NULL;return -3;\n'
    '        }\n'
    '        if(rate<=0||rate>96000) rate=44100;\n'
    '        mpg123_format_none(s_mpg);\n'
    '        mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);\n'
    '        ndspChnSetRate(NDSP_CHANNEL, (float)rate);\n'
    '        s_sample_rate = rate;\n'
)

idx = a.find(old_wrong)
if idx >= 0:
    # Trouver la fin du case
    end = a.find('        break;\n    }\n    case FMT_OPUS:', idx)
    if end >= 0:
        a = a[:idx] + a[end:]
        print('OK: case FMT_M4A mal place supprime de decode_chunk')
    else:
        end2 = a.find('    case FMT_OPUS:', idx)
        if end2 >= 0:
            a = a[:idx] + a[end2:]
            print('OK: case FMT_M4A supprime (v2)')
        else:
            print('WARN: fin du case non trouvee')
else:
    print('INFO: case FMT_M4A dans decode_chunk non trouve')

# Fix 2: Verifier que FMT_M4A est bien dans audio_load
idx2 = a.find('case FMT_M4A:')
if idx2 >= 0:
    print('INFO: FMT_M4A trouve a ligne', a[:idx2].count('\n')+1)
    print(repr(a[idx2:idx2+100]))
else:
    print('WARN: FMT_M4A absent - on lajoute dans audio_load')
    # Ajouter dans audio_load avant FMT_OPUS
    old_opus_case = '    case FMT_OPUS: {'
    new_m4a = (
        '    case FMT_M4A: {\n'
        '        int _err;\n'
        '        s_mpg = mpg123_new(NULL, &_err);\n'
        '        if (!s_mpg) return -1;\n'
        '        if (mpg123_open(s_mpg, path) != MPG123_OK) {\n'
        '            mpg123_delete(s_mpg); s_mpg = NULL; return -2;\n'
        '        }\n'
        '        long rate; int ch, enc;\n'
        '        if(mpg123_getformat(s_mpg,&rate,&ch,&enc)!=MPG123_OK){\n'
        '            mpg123_close(s_mpg);mpg123_delete(s_mpg);s_mpg=NULL;return -3;\n'
        '        }\n'
        '        if(rate<=0||rate>96000) rate=44100;\n'
        '        mpg123_format_none(s_mpg);\n'
        '        mpg123_format(s_mpg, rate, 2, MPG123_ENC_SIGNED_16);\n'
        '        ndspChnSetRate(NDSP_CHANNEL, (float)rate);\n'
        '        s_sample_rate = rate;\n'
        '        off_t _len = mpg123_length(s_mpg);\n'
        '        if (_len > 0 && rate > 0)\n'
        '            s_meta.duration_sec = (int)(_len / rate);\n'
        '        break;\n'
        '    }\n'
        '    case FMT_OPUS: {'
    )
    if old_opus_case in a:
        a = a.replace(old_opus_case, new_m4a, 1)
        print('OK: FMT_M4A ajoute dans audio_load')

# Fix 3: detect_fmt - ajouter M4A
if 'm4a' not in a:
    old_mp3_line = '    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2")) return FMT_MP3;'
    new_mp3_line = (
        '    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2")) return FMT_MP3;\n'
        '    if (!strcasecmp(e, "m4a")) return FMT_M4A;'
    )
    if old_mp3_line in a:
        a = a.replace(old_mp3_line, new_mp3_line)
        print('OK: m4a dans detect_fmt')
else:
    print('OK: m4a deja dans detect_fmt')

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

print('\nLance: make clean && make 2>&1 | grep error:')