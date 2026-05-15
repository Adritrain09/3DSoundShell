#!/bin/bash
make clean && make && \
smdhtool --create "3DSoundShell" "Music Player for 3DS" "Adritrain" 3DSoundShell.png 3DSoundShell.smdh && \
3dsxtool 3DSoundShell.elf 3DSoundShell.3dsx --romfs=romfs --smdh=3DSoundShell.smdh && \
echo "OK! 3DSoundShell.3dsx pret!" || echo "ERREUR de compilation"
