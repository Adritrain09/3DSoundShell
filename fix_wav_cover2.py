#!/usr/bin/env python3
# fix_wav_cover2.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Remplacer extract_wav_cover - lire ID3 directement sans rouvrir le fichier
old_wav = (
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

new_wav = (
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
    '        if(memcmp(ch,"id3 ",4)==0||memcmp(ch,"ID3 ",4)==0||\n'
    '           memcmp(ch,"ID32",4)==0||memcmp(ch,"id32",4)==0) {\n'
    '            /* Lire le chunk ID3 directement ici */\n'
    '            if(csz>0&&csz<0x100000) {\n'
    '                u8 *id3buf=malloc(csz);\n'
    '                if(id3buf&&fread(id3buf,1,csz,f)==csz) {\n'
    '                    /* Verifier header ID3v2 */\n'
    '                    if(memcmp(id3buf,"ID3",3)==0) {\n'
    '                        int ver=id3buf[3];\n'
    '                        u32 tsize=((id3buf[6]&0x7F)<<21)|((id3buf[7]&0x7F)<<14)|\n'
    '                                  ((id3buf[8]&0x7F)<<7)|(id3buf[9]&0x7F);\n'
    '                        u32 pos=10;\n'
    '                        while(pos+10<tsize&&pos+10<csz) {\n'
    '                            u8 *fh=id3buf+pos; pos+=10;\n'
    '                            u32 fsz;\n'
    '                            if(ver>=4) fsz=((fh[4]&0x7F)<<21)|((fh[5]&0x7F)<<14)|\n'
    '                                           ((fh[6]&0x7F)<<7)|(fh[7]&0x7F);\n'
    '                            else       fsz=(fh[4]<<24)|(fh[5]<<16)|(fh[6]<<8)|fh[7];\n'
    '                            if(fsz==0||pos+fsz>csz) break;\n'
    '                            if(memcmp(fh,"APIC",4)==0) {\n'
    '                                u8 *fd=id3buf+pos;\n'
    '                                u32 off=1;\n'
    '                                while(off<fsz&&fd[off]) off++; off++;\n'
    '                                off++;\n'
    '                                while(off<fsz&&fd[off]) off++; off++;\n'
    '                                if(off<fsz) {\n'
    '                                    u32 isz=fsz-off;\n'
    '                                    free_cover();\n'
    '                                    s_meta.cover_data=malloc(isz);\n'
    '                                    if(s_meta.cover_data) {\n'
    '                                        memcpy(s_meta.cover_data,fd+off,isz);\n'
    '                                        s_meta.cover_size=isz;\n'
    '                                        s_meta.has_cover=true;\n'
    '                                    }\n'
    '                                }\n'
    '                                break;\n'
    '                            }\n'
    '                            pos+=fsz;\n'
    '                        }\n'
    '                    }\n'
    '                    free(id3buf);\n'
    '                } else if(id3buf) free(id3buf);\n'
    '            }\n'
    '            fclose(f);\n'
    '            return;\n'
    '        }\n'
    '        if(csz==0||csz>0x10000000) break;\n'
    '        fseek(f,(long)(csz+(csz&1)),SEEK_CUR);\n'
    '    }\n'
    '    fclose(f);\n'
    '}'
)

if old_wav in a:
    a = a.replace(old_wav, new_wav)
    print('OK: extract_wav_cover corrige')
else:
    print('WARN: pattern non trouve')
    idx = a.find('extract_wav_cover')
    if idx >= 0:
        print(repr(a[idx:idx+100]))

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

print('Lance: make clean && make 2>&1 | grep error:')