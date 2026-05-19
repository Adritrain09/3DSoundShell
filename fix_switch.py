#!/usr/bin/env python3
# fix_switch.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Fix 1: decode_chunk - supprimer le break/} en trop apres FMT_M4A
old_decode = (
    '        memcpy(out, audio, pairs * CHANNELS * 2);\n'
    '        return pairs;\n'
    '    }\n'
    '        break;\n'
    '    }\n'
    '    case FMT_OPUS: {'
)
new_decode = (
    '        memcpy(out, audio, pairs * CHANNELS * 2);\n'
    '        return pairs;\n'
    '    }\n'
    '    case FMT_OPUS: {'
)
if old_decode in a:
    a = a.replace(old_decode, new_decode)
    print('OK: decode_chunk corrige')
else:
    print('WARN: decode_chunk pattern non trouve')

# Fix 2: audio_load - supprimer le break/} en trop apres MP3
old_load = (
    '        extract_id3v2_cover(path);\n'
    '        break;\n'
    '    }\n'
    '        break;\n'
    '    }\n'
    '    case FMT_OPUS: {'
)
new_load = (
    '        extract_id3v2_cover(path);\n'
    '        break;\n'
    '    }\n'
    '    case FMT_M4A: {\n'
    '        int _err;\n'
    '        s_mpg = mpg123_new(NULL, &_err);\n'
    '        if (!s_mpg) return -1;\n'
    '        if (mpg123_open(s_mpg, path) != MPG123_OK) {\n'
    '            mpg123_delete(s_mpg); s_mpg = NULL; return -2;\n'
    '        }\n'
    '        long _rate; int _ch, _enc;\n'
    '        if(mpg123_getformat(s_mpg,&_rate,&_ch,&_enc)!=MPG123_OK){\n'
    '            mpg123_close(s_mpg);mpg123_delete(s_mpg);s_mpg=NULL;return -3;\n'
    '        }\n'
    '        if(_rate<=0||_rate>96000) _rate=44100;\n'
    '        mpg123_format_none(s_mpg);\n'
    '        mpg123_format(s_mpg, _rate, 2, MPG123_ENC_SIGNED_16);\n'
    '        ndspChnSetRate(NDSP_CHANNEL, (float)_rate);\n'
    '        s_sample_rate = _rate;\n'
    '        off_t _mlen = mpg123_length(s_mpg);\n'
    '        if (_mlen > 0 && _rate > 0)\n'
    '            s_meta.duration_sec = (int)(_mlen / _rate);\n'
    '        break;\n'
    '    }\n'
    '    case FMT_OPUS: {'
)
if old_load in a:
    a = a.replace(old_load, new_load)
    print('OK: audio_load corrige + FMT_M4A ajoute')
else:
    print('WARN: audio_load pattern non trouve')
    idx = a.find('extract_id3v2_cover')
    if idx >= 0:
        print(repr(a[idx:idx+150]))

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

print('\nLance: make clean && make 2>&1 | grep error:')