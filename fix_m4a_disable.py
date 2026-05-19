#!/usr/bin/env python3
# fix_m4a_disable.py

with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()

# Supprimer M4A de detect_fmt
a = a.replace(
    '    if (!strcasecmp(e, "m4a")) return FMT_M4A;\n',
    '    /* M4A desactive - crash */\n'
)

# Supprimer FMT_M4A de l enum
a = a.replace(
    'FMT_UNKNOWN, FMT_OGG, FMT_WAV, FMT_FLAC, FMT_MP3, FMT_OPUS, FMT_M4A',
    'FMT_UNKNOWN, FMT_OGG, FMT_WAV, FMT_FLAC, FMT_MP3, FMT_OPUS'
)

# Supprimer case FMT_M4A dans decode_chunk
import re
a = re.sub(
    r'    case FMT_M4A:\n    case FMT_MP3:',
    '    case FMT_MP3:',
    a
)
a = re.sub(
    r'    case FMT_MP3:\n    case FMT_M4A:',
    '    case FMT_MP3:',
    a
)

# Supprimer case FMT_M4A dans audio_load
a = re.sub(
    r'    case FMT_M4A: \{.*?break;\n    \}\n    case FMT_OPUS:',
    '    case FMT_OPUS:',
    a, flags=re.DOTALL
)

with open('source/audio.c', 'w', encoding='utf-8') as f:
    f.write(a)

# WAV cover - debug: afficher les premiers chunks
print('M4A desactive')
print('Verifie WAV cover:')
with open('source/audio.c', 'r', encoding='utf-8') as f:
    a = f.read()
idx = a.find('extract_wav_cover')
print(repr(a[idx:idx+400]))

print('\nLance: make clean && make 2>&1 | grep error:')