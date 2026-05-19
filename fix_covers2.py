#!/usr/bin/env python3
# fix_covers2.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Fix WAV cover
wav_func = (
    'static void extract_wav_cover(const char *path)\n'
    '{\n'
    '    FILE *f=fopen(path,"rb"); if(!f) return;\n'
    '    u8 hdr[12];\n'
    '    if(fread(hdr,1,12,f)!=12||memcmp(hdr,"RIFF",4)||memcmp(hdr+8,"WAVE",4)){\n'
    '        fclose(f); return;\n'
    '    }\n'
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
    '}\n\n'
)

marker = 'static AudioFormat detect_fmt(const char *path)'
if 'extract_wav_cover' not in a:
    if marker in a:
        a = a.replace(marker, wav_func + marker)
        print('OK: extract_wav_cover ajoute')
    else:
        print('WARN: marker non trouve')
else:
    print('INFO: extract_wav_cover deja present')

# Appeler dans FMT_WAV
old_wav = (
    '        s_meta.duration_sec = (int)(s_wav.totalPCMFrameCount / s_wav.sampleRate);\n'
    '        break;\n'
    '    }'
)
new_wav = (
    '        s_meta.duration_sec = (int)(s_wav.totalPCMFrameCount / s_wav.sampleRate);\n'
    '        extract_wav_cover(path);\n'
    '        break;\n'
    '    }'
)
if old_wav in a:
    a = a.replace(old_wav, new_wav)
    print('OK: extract_wav_cover appele')
else:
    print('WARN: FMT_WAV non trouve')

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

print('Termine!')