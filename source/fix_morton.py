#!/usr/bin/env python3
with open('source/player_ui.c', 'r') as f:
    c = f.read()

# Fix morton: swap x and y
old_morton = 'static u32 morton(u32 x,u32 y){u32 r=0;for(u32 i=0;i<4;i++)r|=((x>>i&1)<<(2*i+1))|((y>>i&1)<<(2*i));return r;}'
new_morton = 'static u32 morton(u32 x,u32 y){u32 r=0;for(u32 i=0;i<4;i++)r|=((y>>i&1)<<(2*i+1))|((x>>i&1)<<(2*i));return r;}'

if old_morton in c:
    c = c.replace(old_morton, new_morton)
    print("OK morton corrige")
else:
    print("WARN morton non trouve, recherche variante...")
    # Try to find any morton function
    import re
    m = re.search(r'static u32 morton\([^)]+\)\{[^}]+\}', c)
    if m:
        print("Trouvé:", repr(m.group()))
    else:
        print("Pas trouvé du tout")

# Fix byte order back to correct ABGR
# Current wrong order
wrong_orders = [
    'dst[di]=s[0]; dst[di+1]=s[1]; dst[di+2]=s[2]; dst[di+3]=s[3];',
    'dst[di]=s[3]; dst[di+1]=s[0]; dst[di+2]=s[1]; dst[di+3]=s[2];',
    'dst[di]=s[1]; dst[di+2]=s[2]; dst[di+3]=s[3]; dst[di+3]=s[2];',
]
correct_order = 'dst[di]=s[3]; dst[di+1]=s[2]; dst[di+2]=s[1]; dst[di+3]=s[0];'

replaced = False
for wrong in wrong_orders:
    if wrong in c:
        c = c.replace(wrong, correct_order)
        print(f"OK ordre bytes corrige: {wrong[:40]}")
        replaced = True
        break

if not replaced:
    # Find current byte order line
    import re
    m = re.search(r'dst\[di\]=s\[\d\];[^;]+;[^;]+;[^;]+;', c)
    if m:
        print("Ordre actuel:", repr(m.group()))
        c = c.replace(m.group(), correct_order)
        print("OK corrige")
    else:
        print("WARN ordre bytes non trouve")

with open('source/player_ui.c', 'w') as f:
    f.write(c)
print("Done")
