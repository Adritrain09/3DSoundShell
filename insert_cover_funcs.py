#!/usr/bin/env python3
with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    c = f.read()

cover_funcs = '''
/* ── Cover texture ─────────────────────────────────────────── */
static u32 npow2(u32 v){v--;v|=v>>1;v|=v>>2;v|=v>>4;v|=v>>8;v|=v>>16;return v+1;}
static u32 morton(u32 x,u32 y){u32 r=0;for(u32 i=0;i<4;i++)r|=((x>>i&1)<<(2*i+1))|((y>>i&1)<<(2*i));return r;}

static void upload_cover_tex(PlayerUI *ui, const u8 *rgba, int w, int h)
{
    if(ui->cover_tex_init){C3D_TexDelete(&ui->cover_tex);ui->cover_tex_init=false;}
    u32 tw=npow2((u32)w), th=npow2((u32)h);
    if(!C3D_TexInit(&ui->cover_tex,(u16)tw,(u16)th,GPU_RGBA8)) return;
    u8 *dst=(u8*)ui->cover_tex.data;
    memset(dst,0,tw*th*4);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        u32 tile=((u32)y/8)*(tw/8)+((u32)x/8), mi=morton((u32)x&7,(u32)y&7), di=(tile*64+mi)*4;
        const u8 *s=rgba+(y*w+x)*4;
        dst[di]=s[3]; dst[di+1]=s[2]; dst[di+2]=s[1]; dst[di+3]=s[0];
    }
    C3D_TexSetFilter(&ui->cover_tex,GPU_LINEAR,GPU_LINEAR);
    ui->cover_subtex.width=(u16)w; ui->cover_subtex.height=(u16)h;
    ui->cover_subtex.left=0.f; ui->cover_subtex.top=1.f;
    ui->cover_subtex.right=(float)w/(float)tw;
    ui->cover_subtex.bottom=1.f-(float)h/(float)th;
    ui->cover_tex_init=true;
}

'''

anchor = '// ─── Draw helpers'
if anchor in c:
    c = c.replace(anchor, cover_funcs + anchor, 1)
    print("OK fonctions cover inserees")
else:
    print("ERROR ancre non trouvee")

with open('source/player_ui.c', 'w', encoding='utf-8') as f:
    f.write(c)
