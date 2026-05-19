#!/usr/bin/env python3
# fix_cleanup.py

with open('source/main.c','r',encoding='utf-8') as f:
    m = f.read()

# Trouver le debut du cleanup
idx = m.find('// ─── Cleanup')
if idx < 0:
    idx = m.find('Cleanup')
print('Cleanup trouve a index:', idx)
print('Contenu:')
print(m[idx:idx+500])