#!/usr/bin/env python3

with open('source/main.c', 'r') as f:
    m = f.read()

# 1. Ajouter repeat pour haut/bas dans l'explorateur
old_nav = ('    if(kd & KEY_DOWN)   fb_select_next(&g_browser);\n'
           '    if(kd & KEY_UP)     fb_select_prev(&g_browser);')
new_nav = ('    if(kd & KEY_DOWN)   fb_select_next(&g_browser);\n'
           '    if(kd & KEY_UP)     fb_select_prev(&g_browser);\n'
           '    // Repeat haut/bas maintenu\n'
           '    static u64 _ud_held=0; static bool _ud_rep=false;\n'
           '    if(held&KEY_DOWN||held&KEY_UP){\n'
           '        if(!_ud_rep){if(_ud_held==0)_ud_held=osGetTime();\n'
           '        if(osGetTime()-_ud_held>800){_ud_rep=true;}}\n'
           '        if(_ud_rep){if(held&KEY_DOWN)fb_select_next(&g_browser);\n'
           '        else fb_select_prev(&g_browser);}}\n'
           '    else{_ud_held=0;_ud_rep=false;}')

if old_nav in m:
    m = m.replace(old_nav, new_nav)
    print("OK repeat haut/bas")
else:
    print("WARN nav non trouvee")

# 2. Fixer la reprise - sauvegarder les settings a la fermeture
# et charger correctement au demarrage
old_resume = ('    if(g_settings.resume_on_start && g_settings.last_path[0]) {\n'
              '        pl_add(&g_playlist, g_settings.last_path, g_settings.last_path);\n'
              '        pl_jump(&g_playlist, 0);\n'
              '        play_current_track();\n'
              '        if(g_settings.last_position > 0)\n'
              '            audio_seek(g_settings.last_position);\n'
              '        g_state = STATE_PLAYER;\n'
              '    }')
new_resume = ('    if(g_settings.resume_on_start && g_settings.last_path[0]) {\n'
              '        // Recharger le dossier de la derniere piste\n'
              '        char _resume_dir[512];\n'
              '        strncpy(_resume_dir, g_settings.last_path, 511);\n'
              '        char *_slash = strrchr(_resume_dir, \'/\');\n'
              '        if(_slash) *_slash = \'\\0\';\n'
              '        pl_add_dir(&g_playlist, _resume_dir);\n'
              '        // Retrouver la piste\n'
              '        int _found = 0;\n'
              '        for(int _i=0;_i<g_playlist.count;_i++){\n'
              '            if(!strcmp(g_playlist.items[_i].path, g_settings.last_path)){\n'
              '                pl_jump(&g_playlist, _i); _found=1; break;\n'
              '            }\n'
              '        }\n'
              '        if(!_found && g_playlist.count>0) pl_jump(&g_playlist, 0);\n'
              '        if(g_playlist.count>0){\n'
              '            play_current_track();\n'
              '            if(g_settings.last_position > 0)\n'
              '                audio_seek(g_settings.last_position);\n'
              '        }\n'
              '        g_state = STATE_PLAYER;\n'
              '    }')

if old_resume in m:
    m = m.replace(old_resume, new_resume)
    print("OK resume fixe")
else:
    print("WARN resume non trouve")
    # Chercher la version existante
    idx = m.find('resume_on_start')
    if idx != -1:
        print("Contexte:", repr(m[idx-50:idx+200]))

# 3. Sauvegarder la position actuelle dans les settings avant de quitter
old_exit = '    ptmuExit();\n    player_ui_exit(&g_player_ui);\n    audio_exit();'
new_exit = ('    g_settings.last_position = audio_get_position();\n'
            '    settings_save();\n'
            '    ptmuExit();\n'
            '    player_ui_exit(&g_player_ui);\n'
            '    audio_exit();')

if old_exit in m:
    m = m.replace(old_exit, new_exit)
    print("OK sauvegarde position")
else:
    print("WARN exit non trouve")
    # Try alternative
    old_exit2 = '    player_ui_exit(&g_player_ui);\n    audio_exit();'
    new_exit2 = ('    g_settings.last_position = audio_get_position();\n'
                 '    settings_save();\n'
                 '    player_ui_exit(&g_player_ui);\n'
                 '    audio_exit();')
    if old_exit2 in m:
        m = m.replace(old_exit2, new_exit2)
        print("OK sauvegarde position (alt)")

with open('source/main.c', 'w') as f:
    m_write = m
    f.write(m_write)
print("OK main.c")
print("Termine! Lance: make 3dsx")
