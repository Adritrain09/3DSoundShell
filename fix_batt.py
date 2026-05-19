#!/usr/bin/env python3
# fix_batt.py

with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    lines = f.readlines()

# Afficher lignes 310-320 pour voir exactement ce qu'il y a
print('=== Lignes 310-320 ===')
for i, l in enumerate(lines[309:320], 310):
    print(str(i) + ': ' + repr(l))

# Chercher et remplacer les lignes problematiques
new_lines = []
i = 0
fixed = False
while i < len(lines):
    l = lines[i]

    # Detecter la ligne avec PTMU ou 