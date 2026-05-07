#!/usr/bin/env python3
# patch_cover.py — Ajoute le support des pochettes dans 3DSoundShell

# ============================================================
# 1. player_ui.h — ajout des champs texture
# ============================================================
with open('include/player_ui.h', 'r') as f:
    h = f.read()

h = h.replace(
    '    u64             last_tick;\n} PlayerUI;',
    '    u64             last_tick;\n    C3D_Tex          cover_tex;\n    Tex3DS_SubTexture cover_subtex;\n    bool             cover_tex_init;\n} PlayerUI;'
)

with open('include/player_ui.h', 'w') as f:
    f.write(h)
print("OK player_ui.h")

# ============================================================
# 2. player_ui.c — stb_image + texture + affichage
# ============================================================
with open('source/player_ui.c', 'r') as f:
    c = f.read()

# --- stb_image include ---
c = c.replace(
    '#include "player_ui.h"\n',
    '#include "player_ui.h"\n'
    '#define STB_IMAGE_IMPLEMENTATION\n'
    '#define STBI_NO_STDIO\n'
    '#define STBI_ONLY_JPEG\n'
    '#define STBI_ONLY_PNG\n'
    '#include "stb_image.h"\n'
)

# --- fonctions texture ---
cover_funcs = '''
/* ── Cover texture ─────────────────────────────────────────── */
static u32 npow2(u32 v){v--;v|=v>>1;v|=v>>2;v|=v>>4;v|=v>>8;v|=v>>16;return v+1;}
static u32 morton(u32 x,u32 y){u32 r=0;for(u32 i=0;i<4;i++)r|=((x>>i&1)<<(2*i+1))|((y>>i&1)<<(2*i));return r;}

static void upload_cover_tex(PlayerUI *ui, const u8 *rgba, int w, int h)
{
    if(ui->cover_tex_init){C3D_TexDelete(&ui->cover_tex);ui->cover_tex_init=false;}
    u32 tw=npow2(w), th=npow2(h);
    if(!C3D_TexInit(&ui->cover_tex,(u16)tw,(u16)th,GPU_RGBA8)) return;
    u8 *dst=(u8*)ui->cover_tex.data;
    memset(dst,0,tw*th*4);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        u32 tile=(y/8)*(tw/8)+(x/8), mi=morton(x&7,y&7), di=(tile*64+mi)*4;
        const u8 *s=rgba+(y*w+x)*4;
        dst[di]=s[3]; dst[di+1]=s[2]; dst[di+2]=s[1]; dst[di+3]=s[0];
    }
    C3D_TexSetFilter(&ui->cover_tex,GPU_LINEAR,GPU_LINEAR);
    ui->cover_subtex.width=(u16)w; ui->cover_subtex.height=(u16)h;
    ui->cover_subtex.left=0.f; ui->cover_subtex.top=1.f;
    ui->cover_subtex.right=(float)w/tw; ui->cover_subtex.bottom=1.f-(float)h/th;
    ui->cover_tex_init=true;
}

'''

c = c.replace(
    '/* ── Draw helpers',
    cover_funcs + '/* ── Draw helpers'
)

# --- init cover_tex_init dans player_ui_init ---
c = c.replace(
    'ui->cover_tex_id=-1;',
    'ui->cover_tex_id=-1;\n    ui->cover_tex_init=false;'
)

# --- player_ui_load_cover ---
old_load = ('void player_ui_load_cover(PlayerUI *ui, const AudioMetadata *meta)\n'
            '{\n'
            '    ui->cover_loaded = meta->has_cover && meta->cover_data!=NULL;\n'
            '    // Full impl: decode JPEG/PNG cover_data \xe2\x86\x92 C2D_Image\n'
            '}')
new_load = ('void player_ui_load_cover(PlayerUI *ui, const AudioMetadata *meta)\n'
            '{\n'
            '    ui->cover_loaded=false;\n'
            '    if(!meta->has_cover||!meta->cover_data||meta->cover_size==0) return;\n'
            '    int w,h,ch;\n'
            '    u8 *px=stbi_load_from_memory(meta->cover_data,(int)meta->cover_size,&w,&h,&ch,4);\n'
            '    if(!px) return;\n'
            '    u8 *up=px; int uw=w,uh=h; u8 *sc=NULL;\n'
            '    if(w>128||h>128){\n'
            '        sc=malloc(128*128*4);\n'
            '        if(sc){for(int y=0;y<128;y++)for(int x=0;x<128;x++){int sx=x*w/128,sy=y*h/128;memcpy(sc+(y*128+x)*4,px+(sy*w+sx)*4,4);}\n'
            '        up=sc;uw=uh=128;}\n'
            '    }\n'
            '    upload_cover_tex(ui,up,uw,uh);\n'
            '    stbi_image_free(px); if(sc)free(sc);\n'
            '    ui->cover_loaded=ui->cover_tex_init;\n'
            '}')

if old_load in c:
    c = c.replace(old_load, new_load)
    print("OK player_ui_load_cover remplace")
else:
    print("WARN player_ui_load_cover non trouve, verifier manuellement")

# --- dessin de la pochette ---
old_draw = ('    if(meta->has_cover && ui->cover_loaded) {\n'
            '        // In full impl: render C2D_Image from cover texture\n'
            '        draw_rect(art_x,art_y,art_sz,art_sz,th->bg_secondary);\n'
            '        draw_text(art_x+10,art_y+30,0.4f,th->text_secondary,"[Cover]");\n'
            '    } else {\n'
            '        draw_cover_placeholder(art_x,art_y,art_sz);\n'
            '    }')
new_draw = ('    if(meta->has_cover&&ui->cover_loaded&&ui->cover_tex_init){\n'
            '        C2D_Image img={&ui->cover_tex,&ui->cover_subtex};\n'
            '        float sx=art_sz/(float)ui->cover_subtex.width, sy=art_sz/(float)ui->cover_subtex.height;\n'
            '        C2D_DrawImageAt(img,art_x,art_y,0,NULL,sx,sy);\n'
            '    } else {\n'
            '        draw_cover_placeholder(art_x,art_y,art_sz);\n'
            '    }')

if old_draw in c:
    c = c.replace(old_draw, new_draw)
    print("OK dessin pochette remplace")
else:
    print("WARN dessin pochette non trouve, verifier manuellement")

with open('source/player_ui.c', 'w') as f:
    f.write(c)
print("OK player_ui.c")

# ============================================================
# 3. audio.c — extraction des pochettes
# ============================================================
with open('source/audio.c', 'r') as f:
    a = f.read()

cover_extraction = '''
/* ── Cover art extraction ──────────────────────────────────── */
static void free_cover(void)
{
    if(s_meta.cover_data){free(s_meta.cover_data);s_meta.cover_data=NULL;}
    s_meta.cover_size=0; s_meta.has_cover=false;
}

static void extract_id3v2_cover(const char *path)
{
    FILE *f=fopen(path,"rb"); if(!f) return;
    u8 hdr[10];
    if(fread(hdr,1,10,f)!=10||memcmp(hdr,"ID3",3)){fclose(f);return;}
    int ver=hdr[3];
    u32 tag_size=((hdr[6]&0x7F)<<21)|((hdr[7]&0x7F)<<14)|((hdr[8]&0x7F)<<7)|(hdr[9]&0x7F);
    u32 pos=0; u8 fh[10];
    while(pos+10<tag_size){
        if(fread(fh,1,10,f)!=10) break; pos+=10;
        u32 fsz;
        if(ver>=4) fsz=((fh[4]&0x7F)<<21)|((fh[5]&0x7F)<<14)|((fh[6]&0x7F)<<7)|(fh[7]&0x7F);
        else       fsz=(fh[4]<<24)|(fh[5]<<16)|(fh[6]<<8)|fh[7];
        if(fsz==0||fsz>tag_size) break;
        if(memcmp(fh,"APIC",4)==0){
            u8 *data=malloc(fsz); if(!data) break;
            if(fread(data,1,fsz,f)!=(size_t)fsz){free(data);break;}
            u32 off=1;
            while(off<fsz&&data[off]) off++; off++;
            if(off>=fsz){free(data);break;}
            off++;
            while(off<fsz&&data[off]) off++; off++;
            if(off<fsz){
                u32 isz=fsz-off;
                s_meta.cover_data=malloc(isz);
                if(s_meta.cover_data){memcpy(s_meta.cover_data,data+off,isz);s_meta.cover_size=isz;s_meta.has_cover=true;}
            }
            free(data); break;
        } else { fseek(f,(long)fsz,SEEK_CUR); pos+=fsz; }
    }
    fclose(f);
}

static const signed char b64t[256]={
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};
static u8* b64_decode(const char *src, u32 *out_len){
    u32 len=(u32)strlen(src);
    u8 *out=malloc((len/4)*3+4); if(!out) return NULL;
    u32 j=0;
    for(u32 i=0;i+3<len;i+=4){
        s32 a=b64t[(u8)src[i]],b2=b64t[(u8)src[i+1]],c=b64t[(u8)src[i+2]],d=b64t[(u8)src[i+3]];
        if(a<0||b2<0) break;
        out[j++]=(u8)((a<<2)|(b2>>4));
        if(src[i+2]!='='&&c>=0) out[j++]=(u8)((b2<<4)|(c>>2));
        if(src[i+3]!='='&&d>=0) out[j++]=(u8)((c<<6)|d);
    }
    *out_len=j; return out;
}

static void extract_ogg_cover(stb_vorbis_comment *vc)
{
    for(int i=0;i<vc->comment_list_length;i++){
        const char *kv=vc->comment_list[i];
        if(strncasecmp(kv,"METADATA_BLOCK_PICTURE=",23)!=0) continue;
        u32 blen=0; u8 *bin=b64_decode(kv+23,&blen);
        if(!bin||blen<32){free(bin);continue;}
        u32 off=4;
        u32 ml=(bin[off]<<24)|(bin[off+1]<<16)|(bin[off+2]<<8)|bin[off+3]; off+=4+ml;
        if(off+4>blen){free(bin);continue;}
        u32 dl=(bin[off]<<24)|(bin[off+1]<<16)|(bin[off+2]<<8)|bin[off+3]; off+=4+dl+16;
        if(off+4>blen){free(bin);continue;}
        u32 isz=(bin[off]<<24)|(bin[off+1]<<16)|(bin[off+2]<<8)|bin[off+3]; off+=4;
        if(off+isz<=blen&&isz>0){
            s_meta.cover_data=malloc(isz);
            if(s_meta.cover_data){memcpy(s_meta.cover_data,bin+off,isz);s_meta.cover_size=isz;s_meta.has_cover=true;}
        }
        free(bin); break;
    }
}

'''

a = a.replace(
    'static AudioFormat detect_fmt',
    cover_extraction + 'static AudioFormat detect_fmt'
)

# Remplacer le free_cover dans meta_from_filename
a = a.replace(
    '    s_meta.has_cover  = false;\n    s_meta.cover_data = NULL;\n    s_meta.cover_size = 0;',
    '    free_cover();'
)

# Ajouter extract_ogg_cover apres la boucle des commentaires OGG
a = a.replace(
    '            if (!strncasecmp(kv,"GENRE=",6))  snprintf(s_meta.genre,64,"%s",kv+6);\n        }\n        break;\n    }\n    case FMT_WAV:',
    '            if (!strncasecmp(kv,"GENRE=",6))  snprintf(s_meta.genre,64,"%s",kv+6);\n        }\n        extract_ogg_cover(&c);\n        break;\n    }\n    case FMT_WAV:'
)

# Ajouter extract_id3v2_cover dans FMT_MP3 avant le break
a = a.replace(
    '        break;\n    }\n    case FMT_OPUS:',
    '        extract_id3v2_cover(path);\n        break;\n    }\n    case FMT_OPUS:',
    1  # seulement la premiere occurrence
)

with open('source/audio.c', 'w') as f:
    f.write(a)
print("OK audio.c")

print("\nPatch termine! Lance: make 3dsx")
