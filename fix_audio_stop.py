#!/usr/bin/env python3
# fix_audio_stop.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Voir le audio_stop actuel
idx = a.find('void audio_stop(void)')
if idx >= 0:
    print('audio_stop actuel:')
    print(repr(a[idx:idx+300]))
else:
    print('WARN: audio_stop non trouve')

# Voir audio_exit
idx2 = a.find('void audio_exit(void)')
if idx2 >= 0:
    print('audio_exit actuel:')
    print(repr(a[idx2:idx2+300]))