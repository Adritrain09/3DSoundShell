#!/usr/bin/env python3
with open('source/main.c', 'r') as f:
    c = f.read()

c = c.replace(
    '    u32 held = g_input.held;\n    u32 held = g_input.held;',
    '    u32 held = g_input.held;'
)

with open('source/main.c', 'w') as f:
    f.write(c)
print("OK")
