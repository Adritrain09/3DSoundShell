#!/usr/bin/env python3
# fix_final.py - Corrections freezes 3DSoundShell

# ============================================================
# FIX 1 : main.c - Supprimer double cleanup
# ============================================================
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

old_cleanup = (
    '    settings_save();\n'
    '    ptmuExit();\n'
    '    player_ui_exit(&g_player_ui);\n'
    '    g_settings.last_position = audio_get_position();\n'
    '    settings_save();\n'
    '    ptmuExit();\n'
    '    player_ui_exit(&g_player_ui);\n'
    '    audio_exit();\n'
    '    C2D_Fini();\n'
    '    C3D_Fini();\n'
    '    gfxExit();\n'
    '    cfguExit();'
)
new_cleanup = (
    '    g_settings.last_position = audio_get_position();\n'
    '    settings_save();\n'
    '    player_ui_exit(&g_player_ui);\n'
    '    audio_exit();\n'
    '    C2D_Fini();\n'
    '    C3D_Fini();\n'
    '    gfxExit();\n'
    '    cfguExit();'
)

if old_cleanup in m:
    m = m.replace(old_cleanup, new_cleanup)
    print('OK: double cleanup corrige dans main.c')
else:
    print('WARN: cleanup non trouve - verification manuelle necessaire')
    # Compter les occurrences pour debug
    print('  ptmuExit count:', m.count('ptmuExit()'))
    print('  player_ui_exit count:', m.count('player_ui_exit'))

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

# ============================================================
# FIX 2 : player_ui.c - TextBuf global
# ============================================================
with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    p = f.read()

# Verifier si deja fait
if 's_textbuf' in p:
    print('INFO: TextBuf global deja present dans player_ui.c')
else:
    # Trouver la fin des includes
    marker = '#include <citro2d.h>'
    if marker in p:
        insert = (
            '\n\n'
            '/* ── TextBuf global (evite malloc chaque frame) ── */\n'
            'static C2D_TextBuf s_textbuf = NULL;\n'
            'static void s_tbuf_init(void){\n'
            '    if(!s_textbuf) s_textbuf=C2D_TextBufNew(2048);\n'
            '}\n'
        )
        p = p.replace(marker, marker + insert, 1)
        print('OK: TextBuf global insere')
    else:
        print('WARN: citro2d.h non trouve dans player_ui.c')

# Remplacer draw_text qui fait TextBufNew a chaque appel
old_dt = (
    'static void draw_text(float x,float y,float sz,u32 c,const char *t)\n'
    '{\n'
    '    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(512);\n'
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
    '    s_tbuf_init();\n'
    '    C2D_TextBufClear(s_textbuf);\n'
    '    C2D_Text tx;\n'
    '    C2D_TextParse(&tx,s_textbuf,t);\n'
    '    C2D_TextOptimize(&tx);\n'
    '    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);\n'
    '}'
)

if old_dt in p:
    p = p.replace(old_dt, new_dt)
    print('OK: draw_text optimise')
else:
    print('WARN: draw_text non trouve exactement')

# Remplacer draw_text_centered
old_dtc = (
    'static void draw_text_centered(float cx,float y,float sz,u32 c,const char *t)\n'
    '{\n'
    '    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(512);\n'
    '    C2D_TextParse(&tx,tb,t);\n'
    '    C2D_TextOptimize(&tx);\n'
    '    float tw=0,th=0;\n'
    '    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);\n'
    '    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);\n'
    '    C2D_TextBufDelete(tb);\n'
    '}'
)
new_dtc = (
    'static void draw_text_centered(float cx,float y,float sz,u32 c,const char *t)\n'
    '{\n'
    '    if(!t||!t[0]) return;\n'
    '    s_tbuf_init();\n'
    '    C2D_TextBufClear(s_textbuf);\n'
    '    C2D_Text tx;\n'
    '    C2D_TextParse(&tx,s_textbuf,t);\n'
    '    C2D_TextOptimize(&tx);\n'
    '    float tw=0,th=0;\n'
    '    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);\n'
    '    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);\n'
    '}'
)

if old_dtc in p:
    p = p.replace(old_dtc, new_dtc)
    print('OK: draw_text_centered optimise')
else:
    print('WARN: draw_text_centered non trouve exactement')

# Fix player_ui_exit pour liberer le textbuf
old_exit = (
    'void player_ui_exit(PlayerUI *ui)\n'
    '{\n'
    '    if(ui->cover_tex_init){\n'
    '        C3D_TexDelete(&ui->cover_tex);\n'
    '        ui->cover_tex_init=false;\n'
    '    }\n'
    '}'
)
new_exit = (
    'void player_ui_exit(PlayerUI *ui)\n'
    '{\n'
    '    if(ui->cover_tex_init){\n'
    '        C3D_TexDelete(&ui->cover_tex);\n'
    '        ui->cover_tex_init=false;\n'
    '    }\n'
    '    if(s_textbuf){\n'
    '        C2D_TextBufDelete(s_textbuf);\n'
    '        s_textbuf=NULL;\n'
    '    }\n'
    '}'
)

if old_exit in p:
    p = p.replace(old_exit, new_exit)
    print('OK: player_ui_exit libere textbuf')
else:
    print('WARN: player_ui_exit non trouve')

with open('source/player_ui.c', 'w', encoding='utf-8') as f:
    f.write(p)

# ============================================================
# FIX 3 : Meme fix TextBuf pour tous les autres fichiers
# ============================================================
files_to_fix = [
    'source/filebrowser.c',
    'source/playlist.c',
    'source/settings.c',
    'source/equalizer.c',
]

for fname in files_to_fix:
    try:
        with open(fname, 'r', encoding='utf-8') as f:
            src = f.read()

        if 's_textbuf' in src:
            print('INFO: ' + fname + ' deja corrige')
            continue

        changed = False

        # Ajouter buffer global apres les includes citro2d
        if '#include <citro2d.h>' in src and 's_textbuf' not in src:
            buf_code = (
                '\n\n'
                '/* ── TextBuf global ── */\n'
                'static C2D_TextBuf s_textbuf = NULL;\n'
                'static void s_tbuf_init(void){'
                'if(!s_textbuf) s_textbuf=C2D_TextBufNew(1024);}\n'
            )
            src = src.replace('#include <citro2d.h>', '#include <citro2d.h>' + buf_code, 1)
            changed = True

        # Pattern draw_text avec TextBufNew(256)
        old256 = (
            'static void draw_text(float x,float y,float sz,u32 c,const char *t)\n'
            '{\n'
            '    C2D_Text t; C2D_TextBuf tb=C2D_TextBufNew(256);\n'
            '    C2D_TextParse(&t,tb,t);\n'
            '    C2D_TextOptimize(&t);\n'
            '    C2D_DrawText(&t,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);\n'
            '    C2D_TextBufDelete(tb);\n'
            '}'
        )

        # Pattern generique avec TextBufNew(512)
        old512 = (
            'static void draw_text(float x,float y,float sz,u32 c,const char *t)\n'
            '{\n'
            '    C2D_Text t; C2D_TextBuf tb=C2D_TextBufNew(512);\n'
            '    C2D_TextParse(&t,tb,t);\n'
            '    C2D_TextOptimize(&t);\n'
            '    C2D_DrawText(&t,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);\n'
            '    C2D_TextBufDelete(tb);\n'
            '}'
        )

        new_generic = (
            'static void draw_text(float x,float y,float sz,u32 c,const char *t)\n'
            '{\n'
            '    if(!t||!t[0]) return;\n'
            '    s_tbuf_init();\n'
            '    C2D_TextBufClear(s_textbuf);\n'
            '    C2D_Text tx;\n'
            '    C2D_TextParse(&tx,s_textbuf,t);\n'
            '    C2D_TextOptimize(&tx);\n'
            '    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);\n'
            '}'
        )

        if old256 in src:
            src = src.replace(old256, new_generic)
            changed = True
        if old512 in src:
            src = src.replace(old512, new_generic)
            changed = True

        # Chercher tous les TextBufNew restants
        if 'TextBufNew' in src:
            print('WARN: ' + fname + ' a encore des TextBufNew restants!')

        if changed:
            with open(fname, 'w', encoding='utf-8') as f:
                f.write(src)
            print('OK: ' + fname + ' corrige')
        else:
            print('INFO: ' + fname + ' - pas de pattern exact trouve')

    except FileNotFoundError:
        print('SKIP: ' + fname + ' non trouve')

# ============================================================
# VERIFICATION FINALE
# ============================================================
print('\n=== VERIFICATION ===')
import subprocess
for fname in ['source/main.c', 'source/player_ui.c',
              'source/filebrowser.c', 'source/playlist.c',
              'source/settings.c', 'source/equalizer.c']:
    try:
        with open(fname, 'r', encoding='utf-8') as f:
            src = f.read()
        nb = src.count('TextBufNew')
        nb_delete = src.count('TextBufDelete')
        print(fname + ': TextBufNew=' + str(nb) + ' TextBufDelete=' + str(nb_delete))
    except:
        pass

print('\nTermine! Lance: make clean && make')