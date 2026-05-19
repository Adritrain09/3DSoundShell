#!/usr/bin/env python3
# fix_batt_exact.py - Remplacement EXACT de PTMU par mcuHwc

with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    lines = f.readlines()

# Chercher et remplacer les lignes avec PTMU
changed = False
for i, line in enumerate(lines):
    if 'PTMU_GetBatteryLevel' in line:
        print(f'Ligne {i+1}: trouve PTMU')
        print(f'  Avant: {repr(line)}')
        
        # Remplacer cette ligne + les 2 suivantes
        # Ligne actuelle: u8 batt=0; PTMU_GetBatteryLevel(&batt);
        # Ligne suivante: int pct = ...
        # Ligne d'apres: draw_text(...)
        
        # Nouvelle version
        new_lines = [
            '    static u8 batt=50; static int batt_timer=0;\n',
            '    if(batt_timer<=0){\n',
            '        mcuHwcGetBatteryLevel(&batt);\n',
            '        batt_timer=180;\n',
            '    } else batt_timer--;\n',
        ]
        
        # Supprimer les 3 anciennes lignes (celle-ci + 2 suivantes)
        # Mais d'abord verifier ce qu'elles contiennent
        old_block = ''.join(lines[i:i+3])
        print(f'  Bloc ancien: {repr(old_block[:150])}')
        
        # Remplacer
        lines[i] = new_lines[0]
        lines.insert(i+1, new_lines[1])
        lines.insert(i+2, new_lines[2])
        lines.insert(i+3, new_lines[3])
        lines.insert(i+4, new_lines[4])
        
        # Maintenant supprimer les anciennes lignes qui sont decalees
        # Apres insertion, les anciennes sont a i+5, i+6, i+7
        # Mais on doit aussi remplacer la ligne du snprintf et draw_text
        
        changed = True
        break

if not changed:
    print('ERREUR: PTMU non trouve!')
    for i, line in enumerate(lines):
        if 'batt' in line.lower():
            print(f'  Ligne {i+1}: {repr(line[:80])}')

with open('source/player_ui.c', 'w', encoding='utf-8') as f:
    f.writelines(lines)

print('\nVerifions maintenant...')
with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    content = f.read()

print('PTMU present:', 'PTMU_GetBatteryLevel' in content)
print('mcuHwc present:', 'mcuHwcGetBatteryLevel' in content)
print('batt_timer present:', 'batt_timer' in content)