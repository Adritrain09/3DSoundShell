#!/usr/bin/env python3
# fix_resume.py

# Fix 1: main.c - last_position
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

old = '    g_settings.last_position = 0;'
new = '    g_settings.last_position = audio_get_position();'
if old in m:
    m = m.replace(old, new)
    print('OK: last_position corrige')
else:
    print('WARN: last_position non trouve')

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

# Fix 2: settings.c - last_path avec espaces
with open('source/settings.c', 'r', encoding='utf-8') as f:
    s = f.read()

old2 = '        if(sscanf(line,"last_path=%511s",sval)==1)    { strncpy(g_settings.last_path,sval,511); continue; }'
new2 = '        if(strncmp(line,"last_path=",10)==0) { strncpy(g_settings.last_path,line+10,511); g_settings.last_path[strcspn(g_settings.last_path,"\\r\\n")]=0; continue; }'

if old2 in s:
    s = s.replace(old2, new2)
    print('OK: last_path espaces corrige')
else:
    print('WARN: last_path non trouve')
    idx = s.find('last_path')
    if idx >= 0:
        print(repr(s[max(0,idx-10):idx+80]))

with open('source/settings.c', 'w', encoding='utf-8') as f:
    f.write(s)

print('Termine!')