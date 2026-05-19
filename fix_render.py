#!/usr/bin/env python3
# fix_render.py

# Verifier tous les TextBuf dans tous les fichiers
import os

files = [f for f in os.listdir('source') if f.endswith('.c')]
print('=== TextBufNew par fichier ===')
total = 0
for fname in sorted(files):
    path = 'source/' + fname
    with open(path, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    news = [(i+1, l.strip()) for i,l in enumerate(lines) if 'TextBufNew' in l]
    dels = [(i+1, l.strip()) for i,l in enumerate(lines) if 'TextBufDelete' in l]
    if news or dels:
        print('\n' + fname + ':')
        for ln, l in news:
            print('  NEW  ligne ' + str(ln) + ': ' + l[:80])
        for ln, l in dels:
            print('  DEL  ligne ' + str(ln) + ': ' + l[:80])
        total += len(news)

print('\nTotal TextBufNew: ' + str(total))
print('Si total > nombre de fichiers = FUITE MEMOIRE!')

# Verifier aussi les draw_text qui font encore alloc
print('\n=== Fonctions draw_text avec alloc ===')
for fname in sorted(files):
    path = 'source/' + fname
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    # Chercher fonctions draw_text qui contiennent TextBufNew
    import re
    funcs = re.findall(
        r'(static void draw_\w+[^{]*\{[^}]*TextBufNew[^}]*\})',
        content, re.DOTALL
    )
    if funcs:
        print('\n' + fname + ': ' + str(len(funcs)) + ' fonction(s) avec TextBufNew')
        for f in funcs:
            print('  ' + f[:100].replace('\n',' '))