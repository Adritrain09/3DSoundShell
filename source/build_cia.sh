#!/bin/bash
# ─────────────────────────────────────────────────────────────────
#  build_cia.sh — Script de compilation et packaging 3DSoundShell
#  Prérequis: devkitPro, bannertool, makerom dans le PATH
# ─────────────────────────────────────────────────────────────────

set -e
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

echo -e "${YELLOW}=== 3DSoundShell Build Script ===${NC}"

# ─── Vérifications ───────────────────────────────────────────
check_tool() {
    if ! command -v "$1" &>/dev/null; then
        echo -e "${RED}[ERREUR] $1 introuvable. Vérifiez votre installation devkitPro.${NC}"
        exit 1
    fi
    echo -e "${GREEN}[OK]${NC} $1 trouvé"
}

echo "Vérification des outils..."
check_tool arm-none-eabi-gcc
check_tool makerom
check_tool bannertool

# ─── Source les variables devkitPro ──────────────────────────
if [ -f /opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc ]; then
    export DEVKITPRO=/opt/devkitpro
    export DEVKITARM=/opt/devkitpro/devkitARM
    export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
fi

# ─── Vérifier les assets romfs ───────────────────────────────
echo ""
echo "Vérification des assets romfs..."
mkdir -p romfs/gfx romfs/audio romfs/themes/{default,dark,light,purple,forest}

# Créer des assets placeholder si absents
if [ ! -f romfs/gfx/icon.png ]; then
    echo -e "${YELLOW}[WARN] romfs/gfx/icon.png manquant → placeholder créé${NC}"
    # Créer une icône 48x48 basique (noir avec texte)
    # En production, remplacez par une vraie icône PNG 48x48
    convert -size 48x48 xc:"#121218" \
        -fill "#50A0FF" -gravity Center \
        -font DejaVu-Sans -pointsize 10 \
        -annotate 0 "3DS\nSound" \
        romfs/gfx/icon.png 2>/dev/null || \
    echo "  (imagemagick absent — créez manuellement romfs/gfx/icon.png 48x48)"
fi

if [ ! -f romfs/gfx/banner.png ]; then
    echo -e "${YELLOW}[WARN] romfs/gfx/banner.png manquant → placeholder créé${NC}"
    # Bannière 256x128
    convert -size 256x128 xc:"#121218" \
        -fill "#50A0FF" -gravity Center \
        -font DejaVu-Sans -pointsize 20 \
        -annotate 0 "3DSoundShell" \
        romfs/gfx/banner.png 2>/dev/null || \
    echo "  (imagemagick absent — créez manuellement romfs/gfx/banner.png 256x128)"
fi

if [ ! -f romfs/audio/banner.bcwav ]; then
    echo -e "${YELLOW}[WARN] romfs/audio/banner.bcwav manquant${NC}"
    echo "  Créez un fichier BCWAV (son de démarrage) ou utilisez un silence."
    # Créer un silence BCWAV minimal
    python3 -c "
import struct, os
os.makedirs('romfs/audio', exist_ok=True)
# Minimal BCWAV (silence 0.5s, 16-bit PCM, 8000Hz, mono)
samples = b'\x00\x00' * 4000
header = b'CWAV' + struct.pack('<HHII', 0xFEFF, 0x0040, 1, len(samples)+0x40)
with open('romfs/audio/banner.bcwav','wb') as f:
    f.write(header + b'\x00'*56 + samples)
print('Silence BCWAV créé.')
" 2>/dev/null || echo "  (python3 absent — créez manuellement banner.bcwav)"
fi

# ─── Télécharger les libs header-only si absentes ─────────────
echo ""
echo "Vérification des headers tiers..."
mkdir -p include

if [ ! -f include/dr_flac.h ]; then
    echo "Téléchargement dr_flac.h..."
    curl -sL "https://raw.githubusercontent.com/mackron/dr_libs/master/dr_flac.h" \
        -o include/dr_flac.h || echo "  Téléchargez manuellement dr_flac.h dans include/"
fi
if [ ! -f include/dr_wav.h ]; then
    echo "Téléchargement dr_wav.h..."
    curl -sL "https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h" \
        -o include/dr_wav.h || echo "  Téléchargez manuellement dr_wav.h dans include/"
fi
if [ ! -f include/stb_vorbis.h ]; then
    echo "Téléchargement stb_vorbis.h..."
    curl -sL "https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c" \
        -o include/stb_vorbis.h || echo "  Téléchargez manuellement stb_vorbis.c dans include/stb_vorbis.h"
fi

# ─── Installer les paquets 3DS ───────────────────────────────
echo ""
echo "Installation des paquets devkitPro 3DS..."
dkp-pacman -S --noconfirm \
    3ds-dev \
    3ds-citro2d \
    3ds-citro3d \
    3ds-libmpg123 \
    3ds-libvorbisidec \
    3ds-flac \
    3ds-opusfile \
    3ds-libopus 2>/dev/null || echo "  (dkp-pacman absent ou paquets déjà installés)"

# ─── Compilation ─────────────────────────────────────────────
echo ""
echo -e "${YELLOW}Compilation en cours...${NC}"
mkdir -p build
make -j$(nproc) all

echo ""
echo -e "${GREEN}=== Compilation terminée! ===${NC}"

if [ -f 3DSoundShell.cia ]; then
    echo -e "${GREEN}✅  3DSoundShell.cia généré!${NC}"
    echo ""
    echo "Installation sur 3DS:"
    echo "  1. Copiez 3DSoundShell.cia sur votre carte SD (racine ou dossier)"
    echo "  2. Sur la 3DS: ouvrez FBI → Titles → Install CIA from SD"
    echo "  3. Sélectionnez 3DSoundShell.cia"
    echo ""
    echo "Mettez vos fichiers audio dans sdmc:/Music/"
    echo "Thèmes personnalisés: sdmc:/3DSoundShell/themes/montheme.ini"
else
    echo -e "${YELLOW}3DSoundShell.3dsx généré (pas de .cia — vérifiez makerom)${NC}"
    echo "Vous pouvez quand même lancer le .3dsx via Homebrew Launcher."
fi
