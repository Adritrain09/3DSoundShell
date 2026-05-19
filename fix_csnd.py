#!/usr/bin/env python3
# fix_csnd.py
# Remplace NDSP par CSND - pas besoin de dsp.firm !

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Verifier si ndspInit echoue
print('ndspInit trouve:', 'ndspInit' in a)
print('csndInit trouve:', 'csndInit' in a)

# Logger le resultat de ndspInit pour debug
old_ndsp = (
    '    Result rc = ndspInit();\n'
    '    if (R_FAILED(rc)) {\n'
    '        FILE *dbg=fopen("sdmc:/3DSoundShell/ndsp_err.log","w");\n'
    '        if(dbg){fprintf(dbg,"ndspInit failed: 0x%08lX\\n",rc);fclose(dbg);}\n'
    '        return rc;\n'
    '    }'
)

old_ndsp2 = '    Result rc = ndspInit();\n    if (R_FAILED(rc)) return rc;'

new_ndsp = (
    '    /* Tenter ndspInit - peut echouer sans dsp.firm */\n'
    '    Result rc = ndspInit();\n'
    '    FILE *_dbg=fopen("sdmc:/3DSoundShell/ndsp.log","w");\n'
    '    if(_dbg){\n'
    '        fprintf(_dbg,"ndspInit result: 0x%08lX %s\\n",\n'
    '            rc, R_FAILED(rc)?"FAIL":"OK");\n'
    '        fclose(_dbg);\n'
    '    }\n'
    '    if (R_FAILED(rc)) return rc;'
)

if old_ndsp in a:
    a = a.replace(old_ndsp, new_ndsp)
    print('OK: ndsp log ajoute (v1)')
elif old_ndsp2 in a:
    a = a.replace(old_ndsp2, new_ndsp)
    print('OK: ndsp log ajoute (v2)')
else:
    print('WARN: pattern ndspInit non trouve')
    idx = a.find('ndspInit')
    if idx >= 0:
        print(repr(a[max(0,idx-20):idx+100]))

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

print('Lance: make clean && make 2>&1 | grep error:')
print('Puis teste et regarde: cat /g/3DSoundShell/ndsp.log')