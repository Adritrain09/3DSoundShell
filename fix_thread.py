#!/usr/bin/env python3
# fix_thread.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Fix 1: Reduire timeout threadJoin
old_join = '        threadJoin(s_thread, 2000000000ULL); /* 2sec max */'
new_join = '        threadJoin(s_thread, 500000000ULL); /* 0.5sec max */'
if old_join in a:
    a = a.replace(old_join, new_join)
    print('OK: threadJoin timeout 0.5sec')
else:
    print('WARN: threadJoin non trouve')
    idx = a.find('threadJoin')
    if idx >= 0:
        print(repr(a[idx:idx+80]))

# Fix 2: Thread sort immediatement si s_thread_run=false
old_thread_start = (
    'static void audio_thread(void *arg)\n'
    '{\n'
    '    (void)arg;\n'
    '    while (s_thread_run) {'
)
new_thread_start = (
    'static void audio_thread(void *arg)\n'
    '{\n'
    '    (void)arg;\n'
    '    while (s_thread_run) {\n'
    '        if (!s_thread_run) break; /* verif immediate */'
)
if old_thread_start in a:
    a = a.replace(old_thread_start, new_thread_start)
    print('OK: check thread_run immediat')
else:
    print('WARN: thread start non trouve')

# Fix 3: Reduire sleep du thread stopped de 33ms a 8ms
old_sleep = '            svcSleepThread(33000000LL); /* 33ms */'
new_sleep = '            svcSleepThread(8000000LL); /* 8ms - sort vite si stop */'
if old_sleep in a:
    a = a.replace(old_sleep, new_sleep)
    print('OK: sleep thread reduit 8ms')
else:
    print('WARN: sleep 33ms non trouve')
    idx = a.find('33000000')
    if idx >= 0:
        print(repr(a[max(0,idx-30):idx+50]))

# Fix 4: audio_exit - signaler AVANT de vider les buffers
old_exit_start = (
    'void audio_exit(void)\n'
    '{\n'
    '    /* 1. Signaler au thread de stopper */\n'
    '    s_thread_run = false;\n'
    '    s_state = AUDIO_STOPPED;\n'
    '    /* 2. Vider les buffers NDSP */\n'
    '    ndspChnWaveBufClear(NDSP_CHANNEL);\n'
    '    /* 3. Attendre que le thread finisse */\n'
    '    if (s_thread) {\n'
    '        threadJoin(s_thread, 500000000ULL); /* 0.5sec max */'
)
new_exit_start = (
    'void audio_exit(void)\n'
    '{\n'
    '    /* 1. Signaler IMMEDIATEMENT au thread */\n'
    '    s_thread_run = false;\n'
    '    s_state = AUDIO_STOPPED;\n'
    '    /* 2. Petit sleep pour que le thread voie le flag */\n'
    '    svcSleepThread(50000000ULL); /* 50ms */\n'
    '    /* 3. Vider les buffers NDSP */\n'
    '    ndspChnWaveBufClear(NDSP_CHANNEL);\n'
    '    /* 4. Attendre que le thread finisse */\n'
    '    if (s_thread) {\n'
    '        threadJoin(s_thread, 500000000ULL); /* 0.5sec max */'
)
if old_exit_start in a:
    a = a.replace(old_exit_start, new_exit_start)
    print('OK: audio_exit sleep 50ms avant join')
else:
    print('WARN: audio_exit start non trouve')
    idx = a.find('void audio_exit')
    if idx >= 0:
        print(repr(a[idx:idx+300]))

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# Verification
print('\n=== VERIFICATION ===')
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()
print('timeout 500ms:', '500000000ULL' in a)
print('sleep 50ms exit:', '50000000ULL' in a)
print('sleep 8ms thread:', '8000000LL' in a)
print('\nLance: make clean && make 2>&1 | grep error:')