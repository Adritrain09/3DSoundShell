#!/usr/bin/env python3
with open('source/player_ui.c', 'r') as f:
    p = f.read()

# Ajouter include settings.h
p = p.replace(
    '#include "player_ui.h"\n',
    '#include "player_ui.h"\n#include "settings.h"\n'
)

# Remplacer extern AppSettings par include direct
p = p.replace(
    '    extern AppSettings g_settings;\n    if(g_settings.show_cover&&',
    '    if(g_settings.show_cover&&'
)

with open('source/player_ui.c', 'w') as f:
    f.write(p)
print("OK")
