#!/usr/bin/env python3

# 1. audio.c - remplir s_meta.format apres detect_fmt
with open('source/audio.c', 'r') as f:
    c = f.read()

old = '    s_fmt = detect_fmt(path);\n    if (s_fmt == FMT_UNKNOWN) return -1;'
new = ('    s_fmt = detect_fmt(path);\n'
       '    if (s_fmt == FMT_UNKNOWN) return -1;\n'
       '    {const char *_e=strrchr(path,\'.\');\n'
       '    if(_e){strncpy(s_meta.format,_e+1,7);s_meta.format[7]=0;\n'
       '    for(int _i=0;s_meta.format[_i];_i++) if(s_meta.format[_i]>=\'a\'&&s_meta.format[_i]<=\'z\') s_meta.format[_i]-=32;}\n'
       '    else strcpy(s_meta.format,"???");}')

if old in c:
    c = c.replace(old, new)
    print("OK audio.c format")
else:
    print("WARN detect_fmt non trouve")

with open('source/audio.c', 'w') as f:
    f.write(c)

# 2. player_ui.c - afficher le format a cote du titre
with open('source/player_ui.c', 'r') as f:
    p = f.read()

old = ('    draw_text(tx, 34, 0.55f, th->text_primary, meta->title);\n')
new = ('    draw_text(tx, 34, 0.55f, th->text_primary, meta->title);\n'
       '    if(meta->format[0]) draw_text(info_x, 104, 0.38f, th->text_disabled, meta->format);\n')

if old in p:
    p = p.replace(old, new)
    print("OK player_ui.c format affiche")
else:
    print("WARN titre non trouve")

with open('source/player_ui.c', 'w') as f:
    f.write(p)

print("Termine! Lance: make 3dsx")
