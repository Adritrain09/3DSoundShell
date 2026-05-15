#!/usr/bin/env python3
# fix_battery.py

# ============================================================
# FIX : Batterie avec mcuHwc (retourne vrai 0-100%)
# ============================================================

# -- player_ui.c ---------------------------------------------
with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    p = f.read()

# Supprimer tout le code batterie existant et remplacer
import re

# Remplacer n'importe quel code batterie existant
old = re.search(
    r'u8 batt=0;.*?draw_text\([^;]+bstr[^;]+\);',
    p, re.DOTALL
)
if old:
    print('Trouve:', repr(old.group()))
else:
    print('Pattern non trouve, cherche batt...')
    idx = p.find('batt')
    if idx >= 0:
        print(repr(p[max(0,idx-50):idx+200]))