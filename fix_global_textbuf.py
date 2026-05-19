#!/usr/bin/env python3
# fix_global_textbuf.py

import os

# ============================================================
# 1. Ajouter le buffer global dans theme.c
# ============================================================
with open('source/theme.c', 'r', encoding='utf-8') as f:
    t = f.read()

if 'g_textbuf' not in t:
    old = '#include "theme.h"\n#include <stdio.h>\n#include <string.h>'
    new = '#include "theme.h"\n#include <stdio.h>\n#include <string.h>\n#include <citro2d.h>\n\n/* Buffer texte GLOBAL unique pour toute lapp */\nC2D_TextBuf g_textbuf = NULL;'
    if old in t:
        t = t.replace(old, new)
        print('OK: g_textbuf declare dans theme.c')
    else:
        idx = t.find('#include')
        end = t.rfind('#include')
        end = t.find('\n', end) + 1
        t = t[:end] + '\n#include <citro2d.h>\nC2D_TextBuf g_textbuf = NULL;\n' + t[end:]
        print('OK: g_textbuf declare dans theme.c (alt)')
    with open('source/theme.c', 'w', encoding='utf-8') as f:
        f.write(t)
else:
    print('INFO: g_textbuf deja dans theme.c')

# ============================================================
# 2. Ajouter declaration extern dans theme.h
# ============================================================
with open('include/theme.h', 'r', encoding='utf-8') as f:
    h = f.read()

if 'g_textbuf' not in h:
    old_h = 'extern Theme *current_theme;'
    new_h = 'extern Theme *current_theme;\n\n/* Buffer texte global */\n#include <citro2d.h>\nextern C2D_TextBuf g_textbuf;\nvoid g_textbuf_init(void);\nvoid g_textbuf_exit(void);'
    if old_h in h:
        h = h.replace(old_h, new_h)
        print('OK: g_textbuf dans theme.h')
    with open('include/theme.h', 'w', encoding='utf-8') as f:
        f.write(h)
else:
    print('INFO: g_textbuf deja dans theme.h')

# ============================================================
# 3. Ajouter g_textbuf_init/exit dans theme.c
# ============================================================
with open('source/theme.c', 'r', encoding='utf-8') as f:
    t = f.read()

if 'g_textbuf_init' not in t:
    t += '''
void g_textbuf_init(void)
{
    if(!g_textbuf)
        g_textbuf = C2D_TextBufNew(4096);
}

void g_textbuf_exit(void)
{
    if(g_textbuf){
        C2D_TextBufDelete(g_textbuf);
        g_textbuf = NULL;
    }
}
'''
    with open('source/theme.c', 'w', encoding='utf-8') as f:
        f.write(t)
    print('OK: g_textbuf_init/exit ajoute dans theme.c')

# ============================================================
# 4. Remplacer s_textbuf par g_textbuf dans tous les fichiers
# ============================================================
files_to_fix = [
    'source/equalizer.c',
    'source/filebrowser.c',
    'source/player_ui.c',
    'source/playlist.c',
    'source/settings.c',
]

for fname in files_to_fix:
    with open(fname, 'r', encoding='utf-8') as f:
        content = f.read()

    original = content

    # Supprimer la declaration locale s_textbuf
    import re

    # Supprimer: static C2D_TextBuf s_textbuf = NULL;
    content = re.sub(
        r'static C2D_TextBuf s_textbuf\s*=\s*NULL;\n',
        '',
        content
    )

    # Supprimer: static void s_tbuf_init(void){...}
    content = re.sub(
        r'static void s_tbuf_init\(void\)\{[^\n]*\}\n',
        '',
        content
    )

    # Remplacer toutes les references s_textbuf par g_textbuf
    content = content.replace('s_textbuf', 'g_textbuf')

    # Remplacer if(!g_textbuf) g_textbuf=C2D_TextBufNew par g_textbuf_init()
    content = re.sub(
        r'if\(!g_textbuf\)\s*g_textbuf\s*=\s*C2D_TextBufNew\(\d+\);',
        'g_textbuf_init();',
        content
    )

    if content != original:
        with open(fname, 'w', encoding='utf-8') as f:
            f.write(content)
        print('OK: ' + fname + ' mis a jour')
    else:
        print('INFO: ' + fname + ' inchange')

# ============================================================
# 5. Appeler g_textbuf_init dans main.c
# ============================================================
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

if 'g_textbuf_init' not in m:
    old = '    theme_init();'
    new = '    theme_init();\n    g_textbuf_init();'
    if old in m:
        m = m.replace(old, new)
        print('OK: g_textbuf_init dans main.c')

    old_exit = '    C2D_Fini();'
    new_exit = '    g_textbuf_exit();\n    C2D_Fini();'
    if old_exit in m:
        m = m.replace(old_exit, new_exit, 1)
        print('OK: g_textbuf_exit dans cleanup main.c')

    with open('source/main.c', 'w', encoding='utf-8') as f:
        f.write(m)
else:
    print('INFO: g_textbuf_init deja dans main.c')

# ============================================================
# VERIFICATION
# ============================================================
print('\n=== VERIFICATION ===')
all_files = [
    'source/equalizer.c',
    'source/filebrowser.c',
    'source/player_ui.c',
    'source/playlist.c',
    'source/settings.c',
]
for fname in all_files:
    with open(fname, 'r', encoding='utf-8') as f:
        src = f.read()
    nb_new = src.count('TextBufNew')
    nb_local = src.count('s_textbuf')
    nb_global = src.count('g_textbuf')
    status = 'OK' if nb_new <= 0 and nb_local == 0 else 'WARN'
    print(status + ' ' + fname + ': TextBufNew=' + str(nb_new) +
          ' s_textbuf=' + str(nb_local) +
          ' g_textbuf=' + str(nb_global))

print('\nLance: make clean && make 2>&1 | grep error:')