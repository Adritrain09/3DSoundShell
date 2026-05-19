#!/usr/bin/env python3
# fix_memlog.py - Ajouter log memoire pendant lecture

with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

# Ajouter log memoire toutes les 60 frames
old_input = (
    '        // Diagnostic freeze\n'
    '        static int _frame=0; _frame++;\n'
    '        if(_frame%60==0){\n'
    '            u32 _free=osGetMemRegionFree(MEMREGION_LINEAR);\n'
    '            FILE *_f=fopen("sdmc:/3DSoundShell/diag.log","a");\n'
    '            if(_f){\n'
    '                fprintf(_f,"F:%d RAM:%luKB state:%d\\n",\n'
    '                    _frame,(unsigned long)_free/1024,\n'
    '                    (int)audio_get_state());\n'
    '                fclose(_f);\n'
    '            }\n'
    '        }\n'
    '        // Input\n'
    '        input_update(&g_input);'
)

new_input = (
    '        // Input\n'
    '        input_update(&g_input);'
)

# Verifier si le diagnostic est deja la
if '// Diagnostic freeze' in m:
    print('INFO: diagnostic deja present')
else:
    old_input2 = '        // Input\n        input_update(&g_input);'
    new_input2 = (
        '        // Log memoire toutes les 60 frames\n'
        '        static int _fr=0;\n'
        '        if(++_fr % 60 == 0) {\n'
        '            u32 _lf = osGetMemRegionFree(MEMREGION_LINEAR);\n'
        '            u32 _af = osGetMemRegionFree(MEMREGION_APPLICATION);\n'
        '            FILE *_lg = fopen("sdmc:/3DSoundShell/mem.log","a");\n'
        '            if(_lg) {\n'
        '                fprintf(_lg,"frame=%d linear=%luKB app=%luKB audio=%d\\n",\n'
        '                    _fr,\n'
        '                    (unsigned long)_lf/1024,\n'
        '                    (unsigned long)_af/1024,\n'
        '                    (int)audio_get_state());\n'
        '                fclose(_lg);\n'
        '            }\n'
        '        }\n'
        '        // Input\n'
        '        input_update(&g_input);'
    )
    if old_input2 in m:
        m = m.replace(old_input2, new_input2)
        print('OK: log memoire ajoute')
    else:
        print('WARN: point injection non trouve')
        idx = m.find('input_update')
        if idx >= 0:
            print(repr(m[max(0,idx-30):idx+50]))

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

print('Lance: make clean && make 2>&1 | grep error:')
print('Puis teste jusqu\'au freeze')
print('Puis lis: \\\\192.168.137.254\\3DSoundShell\\mem.log')