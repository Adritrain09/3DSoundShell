#!/usr/bin/env python3

# ── Fix player_ui.c : TextBuf global ──────────────────────────
with open('source/player_ui.c','r') as f:
    p = f.read()

# Ajouter le buffer global apres les includes
old_includes_end = '#include <math.h>\n#include <citro2d.h>'
new_includes_end = '''#include <math.h>
#include <citro2d.h>

cat > fix_textbuf.py << 'PYEOF'
#!/usr/bin/env python3

# ── Fix player_ui.c : TextBuf global ──────────────────────────
with open('source/player_ui.c','r') as f:
    p = f.read()

# Ajouter le buffer global apres les includes
old_includes_end = '#include <math.h>\n#include <citro2d.h>'
new_includes_end = '''#include <math.h>
#include <citro2d.h>

/* Buffer texte global - alloue une seule fois */
static C2D_TextBuf s_textbuf = NULL;
static void textbuf_ensure(void){
    if(!s_textbuf) s_textbuf = C2D_TextBufNew(2048);
}
static void draw_text_safe(float x,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
}
static void draw_text_ctr_safe(float cx,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    float tw=0,th=0;
    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);
    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);
}'''

if old_includes_end in p:
    p = p.replace(old_includes_end, new_includes_end)
    print("OK: buffer global ajoute")
else:
    print("WARN: includes end non trouve, cherche alternative...")
    # Alternative
    old2 = '#include <citro2d.h>\n'
    if old2 in p:
        p = p.replace(old2, old2 + '''
static C2D_TextBuf s_textbuf = NULL;
static void textbuf_ensure(void){
    if(!s_textbuf) s_textbuf = C2D_TextBufNew(2048);
}
static void draw_text_safe(float x,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
}
static void draw_text_ctr_safe(float cx,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    float tw=0,th=0;
    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);
    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);
}
''', 1)
        print("OK: buffer global ajoute (alt)")

# Remplacer les 2 fonctions draw_text locales par les safe
old_draw = '''static void draw_text(float x,float y,float sz,u32 c,const char *t)
{
    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(512);
    C2D_TextParse(&tx,tb,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
    C2D_TextBufDelete(tb);
}'''
new_draw = '''static void draw_text(float x,float y,float sz,u32 c,const char *t)
{ draw_text_safe(x,y,sz,c,t); }'''

if old_draw in p:
    p = p.replace(old_draw, new_draw)
    print("OK: draw_text remplace")
else:
    print("WARN: draw_text non trouve exactement")

old_ctr = '''static void draw_text_centered(float cx,float y,float sz,u32 c,const char *t)
{
    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(512);
    C2D_TextParse(&tx,tb,t);
    C2D_TextOptimize(&tx);
    float tw=0,th=0;
    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);
    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);
    C2D_TextBufDelete(tb);
}'''
new_ctr = '''static void draw_text_centered(float cx,float y,float sz,u32 c,const char *t)
{ draw_text_ctr_safe(cx,y,sz,c,t); }'''

if old_ctr in p:
    p = p.replace(old_ctr, new_ctr)
    print("OK: draw_text_centered remplace")
else:
    print("WARN: draw_text_centered non trouve exactement")

# Ajouter liberation dans player_ui_exit
old_exit = '''void player_ui_exit(PlayerUI *ui)
{
    if(ui->cover_tex_init){
        C3D_TexDelete(&ui->cover_tex);
        ui->cover_tex_init=false;
    }
}'''
new_exit = '''void player_ui_exit(PlayerUI *ui)
{
    if(ui->cover_tex_init){
        C3D_TexDelete(&ui->cover_tex);
        ui->cover_tex_init=false;
    }
    if(s_textbuf){
        C2D_TextBufDelete(s_textbuf);
        s_textbuf=NULL;
    }
}'''

if old_exit in p:
    p = p.replace(old_exit, new_exit)
    print("OK: exit textbuf libere")
else:
    print("WARN: exit non trouve")


cat > fix_textbuf.py << 'PYEOF'
#!/usr/bin/env python3

# ── Fix player_ui.c : TextBuf global ──────────────────────────
with open('source/player_ui.c','r') as f:
    p = f.read()

# Ajouter le buffer global apres les includes
old_includes_end = '#include <math.h>\n#include <citro2d.h>'
new_includes_end = '''#include <math.h>
#include <citro2d.h>

/* Buffer texte global - alloue une seule fois */
static C2D_TextBuf s_textbuf = NULL;
static void textbuf_ensure(void){
    if(!s_textbuf) s_textbuf = C2D_TextBufNew(2048);
}
static void draw_text_safe(float x,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
}
static void draw_text_ctr_safe(float cx,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    float tw=0,th=0;
    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);
    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);
}'''

if old_includes_end in p:
    p = p.replace(old_includes_end, new_includes_end)
    print("OK: buffer global ajoute")
else:
    print("WARN: includes end non trouve, cherche alternative...")








cat > fix_textbuf.py << 'PYEOF'
#!/usr/bin/env python3

# ── Fix player_ui.c : TextBuf global ──────────────────────────
with open('source/player_ui.c','r') as f:
    p = f.read()

# Ajouter le buffer global apres les includes
old_includes_end = '#include <math.h>\n#include <citro2d.h>'
new_includes_end = '''#include <math.h>
#include <citro2d.h>

/* Buffer texte global - alloue une seule fois */
static C2D_TextBuf s_textbuf = NULL;
static void textbuf_ensure(void){
    if(!s_textbuf) s_textbuf = C2D_TextBufNew(2048);
}
static void draw_text_safe(float x,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
}
static void draw_text_ctr_safe(float cx,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    float tw=0,th=0;
    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);
    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);
}'''

if old_includes_end in p:
    p = p.replace(old_includes_end, new_includes_end)
    print("OK: buffer global ajoute")
else:
    print("WARN: includes end non trouve, cherche alternative...")
    # Alternative
    old2 = '#include <citro2d.h>\n'
    if old2 in p:
        p = p.replace(old2, old2 + '''
static C2D_TextBuf s_textbuf = NULL;
static void textbuf_ensure(void){
    if(!s_textbuf) s_textbuf = C2D_TextBufNew(2048);
}
static void draw_text_safe(float x,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
}
static void draw_text_ctr_safe(float cx,float y,float sz,u32 c,const char *t){
    if(!t||!t[0]) return;
    textbuf_ensure();
    C2D_TextBufClear(s_textbuf);
    C2D_Text tx;
    C2D_TextParse(&tx,s_textbuf,t);
    C2D_TextOptimize(&tx);
    float tw=0,th=0;
    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);
    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);
}
''', 1)
        print("OK: buffer global ajoute (alt)")

# Remplacer les 2 fonctions draw_text locales par les safe
old_draw = '''static void draw_text(float x,float y,float sz,u32 c,const char *t)
{
    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(512);
    C2D_TextParse(&tx,tb,t);
    C2D_TextOptimize(&tx);
    C2D_DrawText(&tx,C2D_AlignLeft|C2D_WithColor,x,y,0,sz,sz,c);
    C2D_TextBufDelete(tb);
}'''
new_draw = '''static void draw_text(float x,float y,float sz,u32 c,const char *t)
{ draw_text_safe(x,y,sz,c,t); }'''

if old_draw in p:
    p = p.replace(old_draw, new_draw)
    print("OK: draw_text remplace")
else:
    print("WARN: draw_text non trouve exactement")

old_ctr = '''static void draw_text_centered(float cx,float y,float sz,u32 c,const char *t)
{
    C2D_Text tx; C2D_TextBuf tb=C2D_TextBufNew(512);
    C2D_TextParse(&tx,tb,t);
    C2D_TextOptimize(&tx);
    float tw=0,th=0;
    C2D_TextGetDimensions(&tx,sz,sz,&tw,&th);
    C2D_DrawText(&tx,C2D_WithColor,cx-tw/2.f,y,0,sz,sz,c);
    C2D_TextBufDelete(tb);
}'''
new_ctr = '''static void draw_text_centered(float cx,float y,float sz,u32 c,const char *t)
{ draw_text_ctr_safe(cx,y,sz,c,t); }'''

if old_ctr in p:
    p = p.replace(old_ctr, new_ctr)
    print("OK: draw_text_centered remplace")
else:
    print("WARN: draw_text_centered non trouve exactement")

# Ajouter liberation dans player_ui_exit
old_exit = '''void player_ui_exit(PlayerUI *ui)
{
    if(ui->cover_tex_init){
        C3D_TexDelete(&ui->cover_tex);
        ui->cover_tex_init=false;
    }
}'''
new_exit = '''void player_ui_exit(PlayerUI *ui)
{
    if(ui->cover_tex_init){
        C3D_TexDelete(&ui->cover_tex);
        ui->cover_tex_init=false;
    }
    if(s_textbuf){
        C2D_TextBufDelete(s_textbuf);
        s_textbuf=NULL;
    }
}'''

if old_exit in p:
    p = p.replace(old_exit, new_exit)
    print("OK: exit textbuf libere")
else:
    print("WARN: exit non trouve")

with open('source/player_ui.c','w') as f:
    f.write(p)
print("Termine player_ui.c")
