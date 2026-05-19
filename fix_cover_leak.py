#!/usr/bin/env python3
# fix_cover_leak.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# ============================================================
# Fix 1: extract_id3v2_cover - Libérer vieille pochette AVANT malloc
# ============================================================

# Ajouter free_cover() au début de extract_id3v2_cover
old_extract_id3 = (
    'static void extract_id3v2_cover(const char *path)\n'
    '{\n'
    '    FILE *f=fopen(path,"rb"); if(!f) return;\n'
    '    u8 hdr[10];'
)
new_extract_id3 = (
    'static void extract_id3v2_cover(const char *path)\n'
    '{\n'
    '    /* LIBERER ancienne pochette avant extraction */\n'
    '    free_cover();\n'
    '    FILE *f=fopen(path,"rb"); if(!f) return;\n'
    '    u8 hdr[10];'
)

if old_extract_id3 in a:
    a = a.replace(old_extract_id3, new_extract_id3)
    print('OK: free_cover ajoute dans extract_id3v2_cover')
else:
    print('WARN: extract_id3v2_cover non trouve exact')
    idx = a.find('extract_id3v2_cover')
    if idx >= 0:
        print(repr(a[idx:idx+150]))

# ============================================================
# Fix 2: extract_ogg_cover - Meme chose
# ============================================================

old_extract_ogg = (
    'static void extract_ogg_cover(stb_vorbis_comment *vc)\n'
    '{\n'
    '    for(int i=0;i<vc->comment_list_length;i++){'
)
new_extract_ogg = (
    'static void extract_ogg_cover(stb_vorbis_comment *vc)\n'
    '{\n'
    '    /* LIBERER ancienne pochette avant extraction */\n'
    '    free_cover();\n'
    '    for(int i=0;i<vc->comment_list_length;i++){'
)

if old_extract_ogg in a:
    a = a.replace(old_extract_ogg, new_extract_ogg)
    print('OK: free_cover ajoute dans extract_ogg_cover')
else:
    print('WARN: extract_ogg_cover non trouve exact')
    idx = a.find('extract_ogg_cover')
    if idx >= 0:
        print(repr(a[idx:idx+150]))

# ============================================================
# Fix 3: Dans audio_load - s'assurer que free_cover() est appele au debut
# ============================================================

old_audio_load = (
    'Result audio_load(const char *path)\n'
    '{\n'
    '    audio_stop();\n'
    '    memset(&s_meta, 0, sizeof(s_meta));\n'
    '    meta_from_filename(path);'
)
new_audio_load = (
    'Result audio_load(const char *path)\n'
    '{\n'
    '    audio_stop();\n'
    '    /* VIDER toutes les donnees metadonnees anciennes */\n'
    '    free_cover();\n'
    '    memset(&s_meta, 0, sizeof(s_meta));\n'
    '    meta_from_filename(path);'
)

if old_audio_load in a:
    a = a.replace(old_audio_load, new_audio_load)
    print('OK: free_cover ajoute au debut de audio_load')
else:
    print('WARN: audio_load non trouve exact')
    idx = a.find('Result audio_load')
    if idx >= 0:
        print(repr(a[idx:idx+150]))

# ============================================================
# Fix 4: Dans audio_stop - liberer aussi la couverture
# ============================================================

old_stop = '''void audio_stop(void)
{
    s_state = AUDIO_STOPPED;'''
new_stop = '''void audio_stop(void)
{
    s_state = AUDIO_STOPPED;
    /* LIBERER pochette quand on arrete */
    free_cover();'''

if old_stop in a:
    a = a.replace(old_stop, new_stop)
    print('OK: free_cover ajoute dans audio_stop')
else:
    print('INFO: audio_stop verifie manuellement')

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# ============================================================
# VERIFICATION
# ============================================================
print('\n=== VERIFICATION ===')
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

nb_malloc = a.count('cover_data=malloc')
nb_free_in_load = 'free_cover()' in a[a.find('audio_load'):a.find('Result ', a.find('audio_load')+1)]

print('Cover malloc dans audio.c:', nb_malloc)
print('free_cover dans audio_load:', nb_free_in_load)
print('free_cover dans extract functions:', a.count('/* LIBERER ancienne pochette'))

print('\nLance: make clean && make 2>&1 | grep error:')