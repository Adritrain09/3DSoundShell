#!/usr/bin/env python3

# ============================================================
# 1. player_ui.c - fixes multiples
# ============================================================
with open('source/player_ui.c', 'r') as f:
    p = f.read()

# Fix 1: Remplacer emojis non supportés
p = p.replace('"🔀 Aléat ON"', '"Aléat ON"')
p = p.replace('"🔀 Aléat OFF"', '"Aléat OFF"')
p = p.replace('if(pl->shuffle) draw_text(TOP_WIDTH-115,5,0.45f,th->accent2,"🔀");',
              'if(pl->shuffle) draw_text(TOP_WIDTH-115,5,0.45f,th->accent2,"[S]");')

# Fix 2: Utiliser show_cover depuis les settings
old_cover = 'if(meta->has_cover&&ui->cover_loaded&&ui->cover_tex_init){'
new_cover = 'extern AppSettings g_settings;\n    if(g_settings.show_cover&&meta->has_cover&&ui->cover_loaded&&ui->cover_tex_init){'
if old_cover in p:
    p = p.replace(old_cover, new_cover)
    print("OK show_cover")
else:
    print("WARN show_cover non trouve")

with open('source/player_ui.c', 'w') as f:
    f.write(p)
print("OK player_ui.c")

# ============================================================
# 2. main.c - navigation explorateur flèches gauche/droite
# ============================================================
with open('source/main.c', 'r') as f:
    m = f.read()

# Ajouter navigation +5/-5 dans l'explorateur avec repeat
old_nav = ('    if(kd & KEY_DOWN)   fb_select_next(&g_browser);\n'
           '    if(kd & KEY_UP)     fb_select_prev(&g_browser);')
new_nav = ('    if(kd & KEY_DOWN)   fb_select_next(&g_browser);\n'
           '    if(kd & KEY_UP)     fb_select_prev(&g_browser);\n'
           '    // Flèches gauche/droite: sauter 5 fichiers\n'
           '    if(kd & KEY_RIGHT) for(int _i=0;_i<5;_i++) fb_select_next(&g_browser);\n'
           '    if(kd & KEY_LEFT)  for(int _i=0;_i<5;_i++) fb_select_prev(&g_browser);\n'
           '    // Maintenu: continuer après 0.8sec\n'
           '    static u64 _nav_held=0; static bool _nav_rep=false;\n'
           '    if(held&KEY_RIGHT||held&KEY_LEFT){\n'
           '        if(!_nav_rep){if(_nav_held==0)_nav_held=osGetTime();\n'
           '        if(osGetTime()-_nav_held>800){_nav_rep=true;}}\n'
           '        if(_nav_rep){if(held&KEY_RIGHT)fb_select_next(&g_browser);\n'
           '        else fb_select_prev(&g_browser);}}\n'
           '    else{_nav_held=0;_nav_rep=false;}')

if old_nav in m:
    m = m.replace(old_nav, new_nav)
    print("OK navigation explorateur")
else:
    print("WARN navigation non trouvee")

with open('source/main.c', 'w') as f:
    f.write(m)
print("OK main.c")

# ============================================================
# 3. audio.c - pochettes FLAC
# ============================================================
with open('source/audio.c', 'r') as f:
    a = f.read()

# Ajouter extraction pochette FLAC via dr_flac metadata
flac_cover = '''
static void extract_flac_cover(const char *path)
{
    drflac *f = drflac_open_file(path, NULL);
    if(!f) return;
    // dr_flac expose les blocs metadata via iteration
    // On cherche le bloc PICTURE (type 6)
    drflac_metadata meta;
    while(drflac_next_metadata_block(f, &meta)) {
        if(meta.type == DRFLAC_METADATA_BLOCK_TYPE_PICTURE) {
            drflac_picture *pic = &meta.data.picture;
            if(pic->data && pic->dataSize > 0) {
                s_meta.cover_data = malloc(pic->dataSize);
                if(s_meta.cover_data) {
                    memcpy(s_meta.cover_data, pic->data, pic->dataSize);
                    s_meta.cover_size = pic->dataSize;
                    s_meta.has_cover = true;
                }
                break;
            }
        }
    }
    drflac_close(f);
}
'''

# Verifier si dr_flac supporte drflac_next_metadata_block
# Si non, on utilise une approche differente
# Pour l'instant on skip FLAC cover et on note le TODO
print("NOTE: pochette FLAC necessite verification API dr_flac - skip pour l'instant")

# Ajouter AAC support via detect_fmt (rediriger vers MP3 decoder)
old_detect = ('    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2"))  return FMT_MP3;')
new_detect = ('    if (!strcasecmp(e, "mp3") || !strcasecmp(e, "mp2") || !strcasecmp(e, "aac")) return FMT_MP3;')
if old_detect in a:
    a = a.replace(old_detect, new_detect)
    print("OK AAC via mpg123")
else:
    print("WARN detect_fmt MP3 non trouve")

# Ajouter M4A via OPUS decoder (si possible) - skip pour l'instant
# WMA non supporte

with open('source/audio.c', 'w') as f:
    f.write(a)
print("OK audio.c")

print("\nTermine! Lance: make 3dsx")
