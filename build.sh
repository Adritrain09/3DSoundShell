#!/bin/bash
set -e

echo "=== 3DSoundShell Build Script ==="

# Vérifier devkitPro
if [ -z "$DEVKITPRO" ]; then
    export DEVKITPRO="/opt/devkitpro"
fi
if [ -z "$DEVKITARM" ]; then
    export DEVKITARM="$DEVKITPRO/devkitARM"
fi
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"

echo "[1/4] Clean..."
make clean 2>/dev/null || true
rm -f build/audio.o build/player_ui.o build/main.o

echo "[2/4] Build 3DSX..."
make -j4

if [ $? -ne 0 ]; then
    echo "ERREUR: Build failed!"
    exit 1
fi

echo "[3/4] Build CIA..."
make cia

if [ $? -ne 0 ]; then
    echo "AVERTISSEMENT: CIA build failed (3DSX toujours disponible)"
fi

echo "[4/4] Done!"
echo "  3DSX: 3DSoundShell.3dsx"
echo "  CIA:  3DSoundShell.cia"
ls -lh 3DSoundShell.3dsx 3DSoundShell.cia 2>/dev/null || ls -lh 3DSoundShell.3dsx