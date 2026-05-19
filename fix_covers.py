#!/usr/bin/env python3
# fix_covers.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# ============================================================
# Fix FLAC cover - lire les blocs PICTURE manuellement
# dr_flac ne supporte pas drflac_next_metadata_block
# On va lire le fichier FLAC directement
# ============================================================

# Ajouter fonction extract_flac_cover apres extract_ogg_cover
old_marker = 'static AudioFormat detect_fmt(const char *path)'
new_flac_cover = '''static void extract_flac_cover(const char *path)
{
    /* Lire les blocs metadata FLAC manuellement */
    FILE *f = fopen(path, "rb");
    if(!f) return;

    /* Verifier signature fLaC */
    u8 sig[4];
    if(fread(sig,1,4,f)!=4 || memcmp(sig,"fLaC",4)!=0) {
        fclose(f); return;
    }

    /* Parcourir les blocs metadata */
    while(1) {
        u8 hdr[4];
        if(fread(hdr,1,4,f)!=4) break;

        bool last_block = (hdr[0] & 0x80) != 0;
        u8   block_type = hdr[0] & 0x7F;
        u32  block_size = ((u32)hdr[1]<<16)|((u32)hdr[2]<<8)|hdr[3];

        if(block_type == 6) { /* PICTURE block */
            u8 *data = malloc(block_size);
            if(!data) break;
            if(fread(data,1,block_size,f)!=block_size) {
                free(data); break;
            }
            /* Structure: type(4) + mime_len(4) + mime + desc_len(4) + desc */
            /* + width(4) + height(4) + depth(4) + colors(4) + data_len(4) + data */
            u32 off = 4; /* skip picture type */
            if(off+4>block_size){free(data);break;}
            u32 mime_len=((u32)data[off]<<24)|((u32)data[off+1]<<16)|
                         ((u32)data[off+2]<<8)|data[off+3]; off+=4+mime_len;
            if(off+4>block_size){free(data);break;}
            u32 desc_len=((u32)data[off]<<24)|((u32)data[off+1]<<16)|
                         ((u32)data[off+2]<<8)|data[off+3]; off+=4+desc_len;
            off+=16; /* skip width,height,depth,colors */
            if(off+4>block_size){free(data);break;}
            u32 img_size=((u32)data[off]<<24)|((u32)data[off+1]<<16)|
                         ((u32)data[off+2]<<8)|data[off+3]; off+=4;
            if(off+img_size<=block_size && img_size>0) {
                free_cover();
                s_meta.cover_data=malloc(img_size);
                if(s_meta.cover_data){
                    memcpy(s_meta.cover_data,data+off,img_size);
                    s_meta.cover_size=img_size;
                    s_meta.has_cover=true;
                }
            }
            free(data);
            break;
        } else {
            fseek(f,(long)block_size,SEEK_CUR);
        }
        if(last_block) break;
    }
    fclose(f);
}

static AudioFormat detect_fmt(const char *path)'''

if old_marker in a:
    a = a.replace(old_marker, new_flac_cover)
    print('OK: extract_flac_cover ajoute')
else:
    print('WARN: marker non trouve')

# ============================================================
# Appeler extract_flac_cover dans le case FMT_FLAC
# ============================================================
old_flac_load = (
    '    case FMT_FLAC: {\n'
    '        s_flac = drflac_open_file(path, NULL);\n'
    '        if (!s_flac) return -1;\n'
    '        s_sample_rate = s_flac->sampleRate;\n'
    '        ndspChnSetRate(NDSP_CHANNEL, (float)s_flac->sampleRate);\n'
    '        if (s_flac->sampleRate > 0)\n'
    '            s_meta.duration_sec = (int)(s_flac->totalPCMFrameCount / s_flac->sampleRate);\n'
    '        break;\n'
    '    }'
)
new_flac_load = (
    '    case FMT_FLAC: {\n'
    '        s_flac = drflac_open_file(path, NULL);\n'
    '        if (!s_flac) return -1;\n'
    '        s_sample_rate = s_flac->sampleRate;\n'
    '        ndspChnSetRate(NDSP_CHANNEL, (float)s_flac->sampleRate);\n'
    '        if (s_flac->sampleRate > 0)\n'
    '            s_meta.duration_sec = (int)(s_flac->totalPCMFrameCount / s_flac->sampleRate);\n'
    '        extract_flac_cover(path);\n'
    '        break;\n'
    '    }'
)
if old_flac_load in a:
    a = a.replace(old_flac_load, new_flac_load)
    print('OK: extract_flac_cover appele dans FMT_FLAC')
else:
    print('WARN: FMT_FLAC load non trouve')
    idx = a.find('case FMT_FLAC')
    if idx >= 0:
        print(repr(a[idx:idx+200]))

# ============================================================
# Fix OPUS cover - via les tags METADATA_BLOCK_PICTURE
# ============================================================
old_opus_load = (
        '        const OpusTags *tags = op_tags(s_opus, -1);\n'
        '        if (tags) {\n'
        '            const char *t = opus_tags_query(tags,"TITLE",0);\n'
        '            const char *a = opus_tags_query(tags,"ARTIST",0);\n'
        '            const char *b = opus_tags_query(tags,"ALBUM",0);\n'
        '            if (t) snprintf(s_meta.title,256,"%s",t);\n'
        '            if (a) snprintf(s_meta.artist,256,"%s",a);\n'
        '            if (b) snprintf(s_meta.album,256,"%s",b);\n'
        '        }\n'
        '        break;\n'
        '    }'
)
new_opus_load = (
        '        const OpusTags *tags = op_tags(s_opus, -1);\n'
        '        if (tags) {\n'
        '            const char *t = opus_tags_query(tags,"TITLE",0);\n'
        '            const char *ar = opus_tags_query(tags,"ARTIST",0);\n'
        '            const char *b = opus_tags_query(tags,"ALBUM",0);\n'
        '            if (t) snprintf(s_meta.title,256,"%s",t);\n'
        '            if (ar) snprintf(s_meta.artist,256,"%s",ar);\n'
        '            if (b) snprintf(s_meta.album,256,"%s",b);\n'
        '            /* Cover via METADATA_BLOCK_PICTURE tag (meme format OGG) */\n'
        '            int nc = opus_tags_query_count(tags,"METADATA_BLOCK_PICTURE");\n'
        '            if(nc > 0) {\n'
        '                const char *pic = opus_tags_query(tags,"METADATA_BLOCK_PICTURE",0);\n'
        '                if(pic) {\n'
        '                    stb_vorbis_comment vc;\n'
        '                    char *fake[1];\n'
        '                    char fakebuf[512];\n'
        '                    snprintf(fakebuf,512,"METADATA_BLOCK_PICTURE=%s",pic);\n'
        '                    fake[0]=fakebuf;\n'
        '                    vc.comment_list=fake;\n'
        '                    vc.comment_list_length=1;\n'
        '                    extract_ogg_cover(&vc);\n'
        '                }\n'
        '            }\n'
        '        }\n'
        '        break;\n'
        '    }'
)
if old_opus_load in a:
    a = a.replace(old_opus_load, new_opus_load)
    print('OK: cover OPUS ajoute')
else:
    print('WARN: FMT_OPUS tags non trouve')
    idx = a.find('op_tags')
    if idx >= 0:
        print(repr(a[idx:idx+200]))

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# ============================================================
# Desactiver AAC/M4A/WMA proprement
# Afficher message format non supporte
# ============================================================
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Verifier detect_fmt
idx = a.find('def detect_fmt') 
idx2 = a.find('detect_fmt')
print('\ndetect_fmt actuel:')
print(repr(a[idx2:idx2+300]))

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

print('\nTermine! Lance: make clean && make 2>&1 | grep error:')