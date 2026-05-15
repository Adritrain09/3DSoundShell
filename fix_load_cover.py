#!/usr/bin/env python3
with open('source/player_ui.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Find and replace the stub
idx = c.find('void player_ui_load_cover(PlayerUI *ui, const AudioMetadata *meta)')
if idx == -1:
    print("ERROR: function not found")
    exit(1)

# Find the end of the function (closing brace)
end = c.find('\n}', idx) + 2

old = c[idx:end]
print("Found function:")
print(repr(old))

new = '''void player_ui_load_cover(PlayerUI *ui, const AudioMetadata *meta)
{
    ui->cover_loaded=false;
    if(!meta->has_cover||!meta->cover_data||meta->cover_size==0) return;
    int w,h,ch;
    u8 *px=stbi_load_from_memory(meta->cover_data,(int)meta->cover_size,&w,&h,&ch,4);
    if(!px) return;
    u8 *up=px; int uw=w,uh=h; u8 *sc=NULL;
    if(w>128||h>128){
        sc=malloc(128*128*4);
        if(sc){
            for(int y=0;y<128;y++) for(int x=0;x<128;x++){
                int sx=x*w/128,sy=y*h/128;
                memcpy(sc+(y*128+x)*4,px+(sy*w+sx)*4,4);
            }
            up=sc; uw=uh=128;
        }
    }
    upload_cover_tex(ui,up,uw,uh);
    stbi_image_free(px);
    if(sc) free(sc);
    ui->cover_loaded=ui->cover_tex_init;
}'''

c = c[:idx] + new + c[end:]
with open('source/player_ui.c', 'w', encoding='utf-8') as f:
    f.write(c)
print("OK player_ui_load_cover patche")
