#!/usr/bin/env python3
# fix_wav_debug.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Remplacer extract_wav_cover par version avec log
old_wav = (
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

new_wav = (
    'static void extract_wav_cover(const char *path)\n'
    '{\n'
    '    FILE *f=fopen(path,"rb"); if(!f) return;\n'
    '    u8 hdr[12];\n'
    '    if(fread(hdr,1,12,f)!=12||memcmp(hdr,"RIFF",4)||memcmp(hdr+8,"WAVE",4)){\n'
    '        fclose(f); return;\n'
    '    }\n'
    '    /* Log chunks pour debug */\n'
    '    FILE *lg=fopen("sdmc:/3DSoundShell/wav_chunks.log","w");\n'
    '    u8 ch[8];\n'
    '    while(fread(ch,1,8,f)==8) {\n'
    '        u32 csz=ch[4]|(ch[5]<<8)|(ch[6]<<16)|(ch[7]<<24);\n'
    '        if(lg) fprintf(lg,"chunk: %c%c%c%c size=%lu\\n",\n'
    '            ch[0],ch[1],ch[2],ch[3],(unsigned long)csz);\n'
    '        if(memcmp(ch,"id3 ",4)==0||memcmp(ch,"ID3 ",4)==0||\n'
    '           memcmp(ch,"ID32",4)==0||memcmp(ch,"id32",4)==0||\n'
    '           memcmp(ch,"ID3\\0",4)==0) {\n'
    '            if(lg){fprintf(lg,"FOUND ID3!\\n");fclose(lg);}\n'
    '            fclose(f);\n'
    '            extract_id3v2_cover(path);\n'
    '            return;\n'
    '        }\n'
    '        if(csz==0||csz>0x10000000) break;\n'
    '        fseek(f,(long)(csz+(csz&1)),SEEK_CUR);\n'
    '    }\n'
    '    if(lg){fprintf(lg,"END - no ID3 found\\n");fclose(lg);}\n'
    '    fclose(f);\n'
    '}'
)

if old_wav in a:
    a = a.replace(old_wav, new_wav)
    print('OK: log WAV chunks ajoute')
else:
    print('WARN: pattern non trouve')

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

print('Lance: make clean && make')