#!/usr/bin/env python3
# fix_theme.py

import os

# ============================================================
# 1. Ameliorer theme.c - parser complet + charger fichier
# ============================================================
with open('source/theme.c', 'r', encoding='utf-8') as f:
    t = f.read()

# Remplacer theme_load_from_file par une version complete
old_parser = '''bool theme_load_from_file(const char *path, Theme *out)
{
    FILE *f = fopen(path, "r");
    if(!f) return false;
    // Simple key=value parser
    char line[128]; char key[64]; unsigned int val;
    while(fgets(line, sizeof(line), f)) {
        if(sscanf(line, "name=%63s", out->name)==1) continue;
        if(sscanf(line, "bg_primary=0x%x", &val)==1){ out->bg_primary=val; continue; }
        if(sscanf(line, "bg_secondary=0x%x",&val)==1){ out->bg_secondary=val; continue; }
        if(sscanf(line, "accent=0x%x",&val)==1){ out->accent=val; continue; }
        if(sscanf(line, "accent2=0x%x",&val)==1){ out->accent2=val; continue; }
        if(sscanf(line, "text_primary=0x%x",&val)==1){ out->text_primary=val; continue; }
        if(sscanf(line, "text_secondary=0x%x",&val)==1){ out->text_secondary=val; continue; }
        if(sscanf(line, "text_accent=0x%x",&val)==1){ out->text_accent=val; continue; }
        if(sscanf(line, "progress_fill=0x%x",&val)==1){ out->progress_fill=val; continue; }
    }
    fclose(f);
    return true;
}'''

new_parser = '''bool theme_load_from_file(const char *path, Theme *out)
{
    FILE *f = fopen(path, "r");
    if(!f) return false;
    char line[256];
    while(fgets(line, sizeof(line), f)) {
        /* Skip comments et lignes vides */
        char *p=line; while(*p==' '||*p=='\\t') p++;
        if(*p==';'||*p=='#'||*p=='\\n'||*p=='\\r'||*p==0) continue;
        /* Enlever newline */
        line[strcspn(line,"\\r\\n")]=0;
        /* Chercher = */
        char *eq=strchr(line,'=');
        if(!eq) continue;
        *eq=0; char *key=line; char *val=eq+1;
        /* Trim espaces */
        while(*key==' ') key++;
        /* Parse valeur hex */
        unsigned int ival=0;
        if(sscanf(val,"0x%x",&ival)==1) {
            if(!strcmp(key,"bg_primary"))    out->bg_primary=ival;
            if(!strcmp(key,"bg_secondary"))  out->bg_secondary=ival;
            if(!strcmp(key,"bg_header"))     out->bg_header=ival;
            if(!strcmp(key,"bg_selected"))   out->bg_selected=ival;
            if(!strcmp(key,"bg_playing"))    out->bg_playing=ival;
            if(!strcmp(key,"text_primary"))  out->text_primary=ival;
            if(!strcmp(key,"text_secondary"))out->text_secondary=ival;
            if(!strcmp(key,"text_accent"))   out->text_accent=ival;
            if(!strcmp(key,"text_disabled")) out->text_disabled=ival;
            if(!strcmp(key,"accent"))        out->accent=ival;
            if(!strcmp(key,"accent2"))       out->accent2=ival;
            if(!strcmp(key,"border"))        out->border=ival;
            if(!strcmp(key,"scrollbar"))     out->scrollbar=ival;
            if(!strcmp(key,"progress_bg"))   out->progress_bg=ival;
            if(!strcmp(key,"progress_fill")) out->progress_fill=ival;
            if(!strcmp(key,"eq_bar"))        out->eq_bar=ival;
            if(!strcmp(key,"eq_handle"))     out->eq_handle=ival;
        }
        if(!strcmp(key,"name")) snprintf(out->name,32,"%s",val);
    }
    fclose(f);
    return true;
}'''

if old_parser in t:
    t = t.replace(old_parser, new_parser)
    print('OK: theme_load_from_file complet')
else:
    print('WARN: parser non trouve exact')
    idx = t.find('theme_load_from_file')
    if idx >= 0:
        print(repr(t[idx:idx+100]))

with open('source/theme.c', 'w', encoding='utf-8') as f:
    f.write(t)

# ============================================================
# 2. Ajouter theme custom dans main.c apres settings_load
# ============================================================
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

old_theme = (
    '    // Init subsystems\n'
    '    theme_init();\n'
    '    settings_load();\n'
    '    theme_set(theme_get(g_settings.theme_index));'
)
new_theme = (
    '    // Init subsystems\n'
    '    theme_init();\n'
    '    settings_load();\n'
    '    /* Charger theme custom si existe */\n'
    '    Theme custom_theme;\n'
    '    if(theme_load_from_file("sdmc:/3DSoundShell/theme.ini",&custom_theme)) {\n'
    '        theme_set(&custom_theme);\n'
    '    } else {\n'
    '        theme_set(theme_get(g_settings.theme_index));\n'
    '    }'
)
if old_theme in m:
    m = m.replace(old_theme, new_theme)
    print('OK: theme custom charge dans main.c')
else:
    print('WARN: theme init non trouve')
    idx = m.find('theme_init')
    if idx >= 0:
        print(repr(m[idx:idx+150]))

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

# ============================================================
# 3. Ajouter declaration dans theme.h
# ============================================================
with open('include/theme.h', 'r', encoding='utf-8') as f:
    h = f.read()

if 'theme_load_from_file' not in h:
    old_h = 'bool        theme_load_from_file(const char *path, Theme *out);'
    if old_h not in h:
        # Ajouter avant la derniere fonction
        old_h2 = 'void theme_save_to_file(const char *path, const Theme *t);'
        new_h2 = 'bool theme_load_from_file(const char *path, Theme *out);\nvoid theme_save_to_file(const char *path, const Theme *t);'
        if old_h2 in h:
            h = h.replace(old_h2, new_h2)
            print('OK: declaration theme_load_from_file ajoutee')
        else:
            print('WARN: theme_save_to_file non trouve dans theme.h')
    else:
        print('INFO: declaration deja presente')
else:
    print('INFO: theme_load_from_file deja declare')

with open('include/theme.h', 'w', encoding='utf-8') as f:
    f.write(h)

print('\nLance: make clean && make 2>&1 | grep error:')
print('Puis place ton theme.ini dans sdmc:/3DSoundShell/theme.ini')