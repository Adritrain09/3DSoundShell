#!/usr/bin/env python3
# fix_freeze3.py

# ============================================================
# Le freeze sans musique = probleme de rendu/memoire
# On va chercher tous les TextBufNew restants et les corriger
# ============================================================

import re

files = [
    'source/filebrowser.c',
    'source/playlist.c',
    'source/settings.c',
    'source/equalizer.c',
    'source/player_ui.c',
]

print('=== TextBufNew restants ===')
for fname in files:
    with open(fname, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    for i, l in enumerate(lines):
        if 'TextBufNew' in l:
            print(str(i+1) + ' ' + fname + ': ' + repr(l.strip()))

# ============================================================
# Fix global : remplacer TOUS les TextBufNew par buffer global
# Dans chaque fichier qui a encore des TextBufNew
# ============================================================

for fname in files:
    with open(fname, 'r', encoding='utf-8') as f:
        content = f.read()

    if 'TextBufNew' not in content:
        print('OK: ' + fname + ' propre')
        continue

    changed = False

    # Trouver et remplacer le pattern exact dans chaque fichier
    # Pattern: C2D_Text XX; C2D_TextBuf tb=C2D_TextBufNew(NNN);
    # suivi de TextParse, TextOptimize, DrawText, TextBufDelete

    # Regex pour capturer n'importe quel pattern draw_text avec TextBufNew
    pattern = re.compile(
        r'(static void draw_(?:text|rect)\w*\([^)]+\)\s*\{[^}]*?)' +
        r'C2D_Text (\w+);\s*C2D_TextBuf \w+=C2D_TextBufNew\(\d+\);\s*' +
        r'C2D_TextParse\(&\2,\w+,(\w+)\);\s*' +
        r'C2D_TextOptimize\(&\2\);\s*' +
        r'C2D_DrawText\(&\2,([^;]+);\s*' +
        r'C2D_TextBufDelete\(\w+\);\s*\}',
        re.DOTALL
    )

    # Approche plus simple : ligne par ligne
    lines = content.split('\n')
    new_lines = []
    i = 0
    while i < len(lines):
        l = lines[i]

        # Detecter ligne avec TextBufNew dans une fonction draw
        if 'C2D_TextBuf' in l and 'TextBufNew' in l and 'static' not in l:
            # Remplacer par utilisation du buffer global
            indent = len(l) - len(l.lstrip())
            sp = ' ' * indent

            # Chercher le nom de variable texte sur cette ligne ou precedente
            # Remplacer toute la sequence
            new_lines.append(sp + 'if(!s_textbuf) s_textbuf=C2D_TextBufNew(1024);')
            new_lines.append(sp + 'C2D_TextBufClear(s_textbuf);')
            # Remplacer tb par s_textbuf dans les lignes suivantes
            i += 1
            while i < len(lines):
                nl = lines[i]
                if 'C2D_TextBufDelete' in nl:
                    i += 1
                    break
                # Remplacer le nom du buffer temporaire par s_textbuf
                nl = re.sub(r'\btb\b', 's_textbuf', nl)
                new_lines.append(nl)
                i += 1
            changed = True
            continue

        new_lines.append(l)
        i += 1

    if changed:
        with open(fname, 'w', encoding='utf-8') as f:
            f.write('\n'.join(new_lines))
        print('OK: ' + fname + ' corrige')

# ============================================================
# Fix main.c - gspWaitForVBlank
# ============================================================
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

if 'gspWaitForVBlank' not in m:
    old = '        // Draw\n        draw_frame(dt);'
    new = '        // Draw\n        draw_frame(dt);\n        gspWaitForVBlank();'
    if old in m:
        m = m.replace(old, new)
        print('OK: gspWaitForVBlank ajoute')
    else:
        print('WARN: draw_frame non trouve dans main.c')
        idx = m.find('draw_frame')
        if idx >= 0:
            print('contexte:', repr(m[max(0,idx-30):idx+60]))
else:
    print('OK: gspWaitForVBlank deja present')

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

# ============================================================
# Fix audio.c - buffers et priorite thread
# ============================================================
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Buffers
if 'SAMPLES_PER_BUF  4096' not in a:
    a = a.replace(
        '#define SAMPLES_PER_BUF  8192\n#define NUM_BUFS         3',
        '#define SAMPLES_PER_BUF  4096\n#define NUM_BUFS         4'
    )
    print('OK: buffers audio 4x4096')
else:
    print('OK: buffers deja corriges')

# Priorite thread
if '0x30' not in a and '0x18' in a:
    a = a.replace('0x18, -1, false', '0x30, -1, false')
    print('OK: priorite thread 0x30')
else:
    print('OK: priorite deja correcte')

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# ============================================================
# VERIFICATION FINALE
# ============================================================
print('\n=== VERIFICATION ===')
all_files = [
    'source/main.c',
    'source/player_ui.c',
    'source/filebrowser.c',
    'source/playlist.c',
    'source/settings.c',
    'source/equalizer.c',
    'source/audio.c',
]
for fname in all_files:
    with open(fname, 'r', encoding='utf-8') as f:
        src = f.read()
    nb = src.count('TextBufNew')
    issues = []
    if nb > 1:
        issues.append('TextBufNew=' + str(nb))
    if 'gspWaitForVBlank' in src and fname == 'source/main.c':
        issues.append('VBlank=OK')
    status = 'WARN' if issues and 'TextBufNew' in str(issues) else 'OK'
    print(status + ' ' + fname + (' | ' + ' '.join(issues) if issues else ''))

print('\nLance: make clean && make 2>&1 | grep error:')