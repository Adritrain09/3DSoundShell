#!/usr/bin/env python3
# fix_formats.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# ============================================================
# Fix 1: WAV cover - chercher TOUS les types de chunks ID3
# ============================================================
old_wav_cover = (
    '    u8 ch[8];\n'
    '    while(fread(ch,1,8,f)==8) {\n'
    '        u32 csz=ch[4]|(ch[5]<<8)|(ch[6]<<16)|(ch[7]<<24);\n'
    '        if(memcmp(ch,"id3 ",4)==0||memcmp(ch,"ID3 ",4)==0){\n'
    '            fclose(f);\n'
    '            extract_id3v2_cover(path);\n'
    '            return;\n'
    '        }\n'
    '        fseek(f,(long)(csz+(csz&1)),SEEK_CUR);\n'
    '    }\n'
    '    fclose(f);\n'
    '}'
)
new_wav_cover = (
    '    u8 ch[8];\n'
    '    while(fread(ch,1,8,f)==8) {\n'
    '        u32 csz=ch[4]|(ch[5]<<8)|(ch[6]<<16)|(ch[7]<<24);\n'
    '        /* Chercher chunk ID3 sous toutes ses formes */\n'
    '        if(memcmp(ch,"id3 ",4)==0||memcmp(ch,"ID3 ",4)==0||\n'
    '           memcmp(ch,"ID32",4)==0||memcmp(ch,"id32",4)==0||\n'
    '           memcmp(ch,"ID3\\0",4)==0) {\n'
    '            fclose(f);\n'
    '            extract_id3v2_cover(path);\n'
    '            return;\n'
    '        }\n'
    '        if(csz==0||csz>0x10000000) break;\n'
    '        fseek(f,(long)(csz+(csz&1)),SEEK_CUR);\n'
    '    }\n'
    '    fclose(f);\n'
    '}'
)
if old_wav_cover in a:
    a = a.replace(old_wav_cover, new_wav_cover)
    print('OK: WAV cover chunks etendus')
else:
    print('WARN: WAV cover non trouve')

# ============================================================
# Fix 2: M4A/AAC support via mpg123
# mpg123 peut lire M4A si compile avec support AAC
# On essaie et on retourne erreur propre si ca marche pas
# ============================================================
old_detect = (
    '    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2")) return FMT_MP3;\n'
    '    /* AAC desactive: mpg123 crashe sur 3DS avec AAC */'
)
new_detect = (
    '    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2")) return FMT_MP3;\n'
    '    if (!strcasecmp(e, "m4a")) return FMT_M4A;'
)
if old_detect in a:
    a = a.replace(old_detect, new_detect)
    print('OK: M4A ajoute dans detect_fmt')
else:
    print('WARN: detect_fmt non trouve')
    idx = a.find('mp3.*mp2')
    idx2 = a.find('FMT_MP3')
    if idx2 >= 0:
        print(repr(a[max(0,idx2-50):idx2+100]))

# Ajouter FMT_M4A dans l enum
old_enum = 'typedef enum {\n    FMT_UNKNOWN, FMT_OGG, FMT_WAV, FMT_FLAC, FMT_MP3, FMT_OPUS\n} AudioFormat;'
new_enum = 'typedef enum {\n    FMT_UNKNOWN, FMT_OGG, FMT_WAV, FMT_FLAC, FMT_MP3, FMT_OPUS, FMT_M4A\n} AudioFormat;'
if old_enum in a:
    a = a.replace(old_enum, new_enum)
    print('OK: FMT_M4A ajoute dans enum')
else:
    print('WARN: enum non trouve')
    idx = a.find('FMT_UNKNOWN')
    if idx >= 0:
        print(repr(a[idx:idx+80]))

# Ajouter case FMT_M4A dans audio_load
# M4A = MP4 container avec AAC - on utilise mpg123 en mode probe
old_switch_end = (
    '    case FMT_OPUS: {'
)
new_m4a_case = (
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
    '        off_t len = mpg123_length(s_mpg);\n'
    '        if (len > 0 && rate > 0)\n'
    '            s_meta.duration_sec = (int)(len / rate);\n'
    '        s_fmt = FMT_M4A;\n'
    '        break;\n'
    '    }\n'
    '    case FMT_OPUS: {'
)
if old_switch_end in a:
    a = a.replace(old_switch_end, new_m4a_case)
    print('OK: case FMT_M4A ajoute dans audio_load')
else:
    print('WARN: case FMT_OPUS non trouve')

# Ajouter FMT_M4A dans decode_chunk - utilise le meme decodeur que MP3
old_decode_mp3 = (
    '    case FMT_MP3: {\n'
    '        if (!s_mpg) return 0;\n'
    '        unsigned char *audio;\n'
    '        size_t bytes;\n'
    '        off_t num;\n'
    '        int err = mpg123_decode_frame(s_mpg, &num, &audio, &bytes);\n'
    '        if (err == MPG123_DONE || bytes == 0) return 0;\n'
    '        int pairs = (int)(bytes / (2 * CHANNELS));\n'
    '        if (pairs > max_pairs) pairs = max_pairs;\n'
    '        memcpy(out, audio, pairs * CHANNELS * 2);\n'
    '        return pairs;\n'
    '    }'
)
new_decode_mp3 = (
    '    case FMT_MP3:\n'
    '    case FMT_M4A: {\n'
    '        if (!s_mpg) return 0;\n'
    '        unsigned char *audio;\n'
    '        size_t bytes;\n'
    '        off_t num;\n'
    '        int err = mpg123_decode_frame(s_mpg, &num, &audio, &bytes);\n'
    '        if (err == MPG123_DONE || bytes == 0) return 0;\n'
    '        int pairs = (int)(bytes / (2 * CHANNELS));\n'
    '        if (pairs > max_pairs) pairs = max_pairs;\n'
    '        memcpy(out, audio, pairs * CHANNELS * 2);\n'
    '        return pairs;\n'
    '    }'
)
if old_decode_mp3 in a:
    a = a.replace(old_decode_mp3, new_decode_mp3)
    print('OK: FMT_M4A dans decode_chunk')
else:
    print('WARN: decode MP3 non trouve')

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

print('\nTermine! Lance: make clean && make 2>&1 | grep error:')