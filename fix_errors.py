#!/usr/bin/env python3
# fix_errors.py

# ============================================================
# FIX 1 : main.c - renommer les variables statiques dupliquees
# ============================================================
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

# Le probleme : deux blocs avec static u64 _nav_held et static bool _nav_rep
# Le premier bloc (lignes ~115) = repeat haut/bas
# Le second bloc (lignes ~134) = repeat gauche/droite
# On renomme le second bloc

old_nav2 = (
    '    static u64 _nav_held=0; static bool _nav_rep=false;\n'
    '    if(held&KEY_RIGHT||held&KEY_LEFT){\n'
    '        if(!_nav_rep){if(_nav_held==0)_nav_held=osGetTime();\n'
    '        if(osGetTime()-_nav_held>800){_nav_rep=true;}}\n'
    '        if(_nav_rep){if(held&KEY_RIGHT)fb_select_next(&g_browser);\n'
    '        else fb_select_prev(&g_browser);}}\n'
    '    else{_nav_held=0;_nav_rep=false;}'
)
new_nav2 = (
    '    static u64 _nav2_held=0; static bool _nav2_rep=false;\n'
    '    if(held&KEY_RIGHT||held&KEY_LEFT){\n'
    '        if(!_nav2_rep){if(_nav2_held==0)_nav2_held=osGetTime();\n'
    '        if(osGetTime()-_nav2_held>800){_nav2_rep=true;}}\n'
    '        if(_nav2_rep){if(held&KEY_RIGHT)fb_select_next(&g_browser);\n'
    '        else fb_select_prev(&g_browser);}}\n'
    '    else{_nav2_held=0;_nav2_rep=false;}'
)

if old_nav2 in m:
    m = m.replace(old_nav2, new_nav2)
    print('OK: _nav_held renomme en _nav2_held')
else:
    print('WARN: bloc nav2 non trouve - recherche alternative...')
    # Compter les occurrences
    count = m.count('static u64 _nav_held')
    print('  occurrences de _nav_held:', count)
    if count == 2:
        # Remplacer seulement la deuxieme occurrence
        idx = m.find('static u64 _nav_held')
        idx2 = m.find('static u64 _nav_held', idx+1)
        if idx2 >= 0:
            m = m[:idx2] + m[idx2:].replace(
                'static u64 _nav_held=0; static bool _nav_rep=false;',
                'static u64 _nav2_held=0; static bool _nav2_rep=false;',
                1
            )
            # Fixer les references
            section = m[idx2:]
            section = section.replace('_nav_rep', '_nav2_rep', 10)
            section = section.replace('_nav_held', '_nav2_held', 10)
            m = m[:idx2] + section
            print('OK: renommage force OK')

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

# ============================================================
# FIX 2 : Corriger les TextBufNew restants dans tous les fichiers
# ============================================================
# Le probleme : draw_text dans ces fichiers utilise 'C2D_Text t' 
# et 'C2D_TextParse(&t,tb,t)' - conflit de noms t!
# Le fix precedent cherchait le mauvais pattern

files = [
    'source/filebrowser.c',
    'source/playlist.c', 
    'source/settings.c',
    'source/equalizer.c',
]

for fname in files:
    with open(fname, 'r', encoding='utf-8') as f:
        src = f.read()
    
    changed = False
    nb_before = src.count('TextBufNew')
    
    # Pattern exact de filebrowser/playlist/settings/equalizer
    # Ces fichiers ont: C2D_Text t; C2D_TextBuf tb=C2D_TextBufNew(512);
    # avec variable 't' au lieu de 'tx'
    
    old_dt_v2 = (
        'static void draw_text(float x,float y,float sz,u32 c,const char *t)\n'
        '{\n'
        '    C2D_Text t; C2D_TextBuf tb=C2D_TextBufNew(512);\n'
        '    C2D_TextParse(&t,tb,t);\n'
        '    C2D_TextOptimize(&t);\n'
        '    C2D_DrawText(&t,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);\n'
        '    C2D_TextBufDelete(tb);\n'
        '}'
    )
    # Version equalizer avec TextBufNew(256)
    old_dt_v3 = (
        'static void draw_text(float x,float y,float sz,u32 c,const char *t)\n'
        '{\n'
        '    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(256);\n'
        '    C2D_TextParse(&tx,tb,t);\n'
        '    C2D_TextOptimize(&tx);\n'
        '    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);\n'
        '    C2D_TextBufDelete(tb);\n'
        '}'
    )

    new_dt = (
        'static void draw_text(float x,float y,float sz,u32 c,const char *t)\n'
        '{\n'
        '    if(!t||!t[0]) return;\n'
        '    if(!s_textbuf) s_textbuf=C2D_TextBufNew(1024);\n'
        '    C2D_TextBufClear(s_textbuf);\n'
        '    C2D_Text tx;\n'
        '    C2D_TextParse(&tx,s_textbuf,t);\n'
        '    C2D_TextOptimize(&tx);\n'
        '    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);\n'
        '}'
    )

    if old_dt_v2 in src:
        src = src.replace(old_dt_v2, new_dt)
        changed = True
        print('OK: ' + fname + ' draw_text v2 remplace')
    elif old_dt_v3 in src:
        src = src.replace(old_dt_v3, new_dt)
        changed = True
        print('OK: ' + fname + ' draw_text v3 remplace')
    else:
        # Afficher le pattern exact pour debug
        idx = src.find('TextBufNew')
        if idx >= 0:
            print('WARN: ' + fname + ' pattern non reconnu, contexte:')
            print(repr(src[max(0,idx-100):idx+100]))

    nb_after = src.count('TextBufNew')
    
    if changed:
        with open(fname, 'w', encoding='utf-8') as f:
            f.write(src)
        print('  TextBufNew: ' + str(nb_before) + ' -> ' + str(nb_after))

# ============================================================
# VERIFICATION
# ============================================================
print('\n=== VERIFICATION FINALE ===')
all_files = [
    'source/main.c',
    'source/player_ui.c',
    'source/filebrowser.c',
    'source/playlist.c',
    'source/settings.c',
    'source/equalizer.c',
]
errors = 0
for fname in all_files:
    with open(fname, 'r', encoding='utf-8') as f:
        src = f.read()
    nb = src.count('TextBufNew')
    nav = src.count('static u64 _nav_held')
    status = 'OK' if nb <= 1 else 'WARN'
    print(status + ' ' + fname + ': TextBufNew=' + str(nb) + 
          (' | _nav_held=' + str(nav) if nav > 0 else ''))
    if nb > 1:
        errors += 1

if errors == 0:
    print('\nTout OK! Lance: make clean && make')
else:
    print('\n' + str(errors) + ' fichier(s) a corriger manuellement')
    print('Lance: grep -n "TextBufNew" source/*.c')