#!/usr/bin/env python3
# fix_audio_exit.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Corriger l'ordre dans audio_exit
old_exit = (
    'void audio_exit(void)\n'
    '{\n'
    '    s_thread_run = false;\n'
    '    free_cover();\n'
    '    if (s_thread) {\n'
    '        threadJoin(s_thread, U64_MAX);\n'
    '        threadFree(s_thread);\n'
    '        s_thread = NULL;\n'
    '    }\n'
    '    audio_stop();\n'
    '    mpg123_exit();\n'
    '    if (s_pcm_buf) { linearFree(s_pcm_buf); s_pcm_buf = NULL; }\n'
    '    ndspExit();\n'
    '}'
)

new_exit = (
    'void audio_exit(void)\n'
    '{\n'
    '    /* 1. Signaler au thread de stopper */\n'
    '    s_thread_run = false;\n'
    '    s_state = AUDIO_STOPPED;\n'
    '    /* 2. Vider les buffers NDSP */\n'
    '    ndspChnWaveBufClear(NDSP_CHANNEL);\n'
    '    /* 3. Attendre que le thread finisse */\n'
    '    if (s_thread) {\n'
    '        threadJoin(s_thread, 2000000000ULL); /* 2sec max */\n'
    '        threadFree(s_thread);\n'
    '        s_thread = NULL;\n'
    '    }\n'
    '    /* 4. Fermer les decodeurs */\n'
    '    if (s_vorbis)   { stb_vorbis_close(s_vorbis); s_vorbis = NULL; }\n'
    '    if (s_flac)     { drflac_close(s_flac);        s_flac   = NULL; }\n'
    '    if (s_wav_open) { drwav_uninit(&s_wav);         s_wav_open = false; }\n'
    '    if (s_mpg)      { mpg123_close(s_mpg); mpg123_delete(s_mpg); s_mpg = NULL; }\n'
    '    if (s_opus)     { op_free(s_opus);              s_opus   = NULL; }\n'
    '    /* 5. Liberer memoire */\n'
    '    free_cover();\n'
    '    mpg123_exit();\n'
    '    if (s_pcm_buf) { linearFree(s_pcm_buf); s_pcm_buf = NULL; }\n'
    '    ndspExit();\n'
    '}'
)

if old_exit in a:
    a = a.replace(old_exit, new_exit)
    print('OK: audio_exit corrige')
else:
    print('WARN: pattern non trouve, cherche...')
    idx = a.find('void audio_exit')
    if idx >= 0:
        print(repr(a[idx:idx+400]))

# Corriger aussi audio_stop - ne pas fermer les decodeurs ici
# car audio_exit le fait deja
old_stop_end = (
    '    if (s_vorbis)   { stb_vorbis_close(s_vorbis); s_vorbis = NULL; }\n'
    '    if (s_flac)     { drflac_close(s_flac);        s_flac   = NULL; }\n'
    '    if (s_wav_open) { drwav_uninit(&s_wav);         s_wav_open = false; }\n'
    '    if (s_mpg)      { mpg123_close(s_mpg); mpg123_delete(s_mpg); s_mpg = NULL; }\n'
    '    if (s_opus)     { op_free(s_opus);              s_opus   = NULL; }\n'
    '\n'
    '    s_samples_played = 0;\n'
    '    s_fmt            = FMT_UNKNOWN;\n'
    '    s_sample_rate    = SAMPLE_RATE;\n'
    '    memset(s_wbufs, 0, sizeof(s_wbufs));\n'
    '    s_buf_idx = 0;\n'
    '}'
)

if old_stop_end in a:
    print('OK: decodeurs dans audio_stop trouves')
else:
    print('INFO: audio_stop - cherche decodeurs')
    idx = a.find('stb_vorbis_close')
    if idx >= 0:
        print(repr(a[max(0,idx-50):idx+300]))

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# Corriger main.c - enlever le double audio_exit
with open('source/main.c', 'r', encoding='utf-8') as f:
    m = f.read()

# Verifier le cleanup
idx = m.find('Cleanup')
print('\nCleanup actuel:')
print(m[idx:idx+400])

# Corriger - audio_exit ne doit apparaitre qu'une fois
old_cleanup = (
    '    audio_stop();\n'
    '    audio_exit();\n'
    '    g_settings.last_position = audio_get_position();\n'
    '    settings_save();\n'
    '    player_ui_exit(&g_player_ui);\n'
    '    audio_exit();\n'
)
new_cleanup = (
    '    audio_exit(); /* stops + cleans everything */\n'
    '    g_settings.last_position = 0;\n'
    '    settings_save();\n'
    '    player_ui_exit(&g_player_ui);\n'
)

if old_cleanup in m:
    m = m.replace(old_cleanup, new_cleanup)
    print('OK: double audio_exit supprime')
else:
    old_cleanup2 = (
        '    audio_stop();\n'
        '    g_settings.last_position = audio_get_position();\n'
        '    settings_save();\n'
        '    player_ui_exit(&g_player_ui);\n'
        '    audio_exit();\n'
    )
    new_cleanup2 = (
        '    audio_exit(); /* stops + cleans everything */\n'
        '    g_settings.last_position = 0;\n'
        '    settings_save();\n'
        '    player_ui_exit(&g_player_ui);\n'
    )
    if old_cleanup2 in m:
        m = m.replace(old_cleanup2, new_cleanup2)
        print('OK: cleanup corrige (v2)')
    else:
        print('WARN: cleanup pattern non trouve')

with open('source/main.c', 'w', encoding='utf-8') as f:
    f.write(m)

print('\nTermine! Lance: make clean && make 2>&1 | grep error:')