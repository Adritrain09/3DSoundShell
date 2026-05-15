#!/usr/bin/env python3

# Fix display issues in player_ui.c
with open('source/player_ui.c', 'r') as f:
    p = f.read()

# 1. Play/Pause L+R trop long → raccourcir
p = p.replace('"Play/Pause L+R"', '"L+R Pause"')

# 2. A = Entrer dossier → A = Play/Pause (on est dans le player)
p = p.replace('"A = Entrer dossier"', '"A = Play/Pause"')

# 3. Supprimer le ? devant Aléat ON/OFF (emoji non supporté)
p = p.replace(
    'draw_text(220,by,0.44f,pl->shuffle?th->accent:th->text_disabled,\n        pl->shuffle?"Aléat ON":"Aléat OFF");',
    'draw_text(220,by,0.44f,pl->shuffle?th->accent:th->text_disabled,\n        pl->shuffle?"[S] ON":"[S] OFF");'
)

# 4. Descendre "Barres" (viz label) un peu plus bas
# Chercher la ligne qui affiche le viz_names
import re

# Fix viz label position - descendre de quelques pixels
p = p.replace(
    'draw_text(BOT_WIDTH-50,',
    'draw_text(BOT_WIDTH-52,'
)

with open('source/player_ui.c', 'w') as f:
    f.write(p)
print("OK player_ui.c")

# Fix held in input_file_browser
with open('source/main.c', 'r') as f:
    m = f.read()

m = m.replace(
    'static void input_file_browser(void)\n{\n    u32 kd = g_input.down;',
    'static void input_file_browser(void)\n{\n    u32 kd = g_input.down;\n    u32 held = g_input.held;'
)

with open('source/main.c', 'w') as f:
    f.write(m)
print("OK main.c held")

print("Termine! Lance: make 3dsx")
