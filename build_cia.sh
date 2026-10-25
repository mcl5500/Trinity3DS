#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=== Building Doom 3DS (.3dsx and .elf) ==="
docker run --rm -v "$DIR":/workspace -w /workspace/build/3ds devkitpro/devkitarm:latest bash -c "source /opt/devkitpro/3dsvars.sh && make -j\$(nproc)"

echo "=== Locating 3DS Packaging Tools ==="
BANNERTOOL=""
MAKEROM=""

if command -v bannertool >/dev/null 2>&1; then
    BANNERTOOL=bannertool
elif [ -f /home/sh3ld0r/Schreibtisch/PocketVoxel3D/tools/bannertool ]; then
    BANNERTOOL=/home/sh3ld0r/Schreibtisch/PocketVoxel3D/tools/bannertool
fi

if command -v makerom >/dev/null 2>&1; then
    MAKEROM=makerom
elif [ -f /home/sh3ld0r/Schreibtisch/PocketVoxel3D/tools/makerom ]; then
    MAKEROM=/home/sh3ld0r/Schreibtisch/PocketVoxel3D/tools/makerom
fi

if [ -z "$BANNERTOOL" ] || [ -z "$MAKEROM" ]; then
    echo "Error: bannertool or makerom not found."
    exit 1
fi

RES="$DIR/build/3ds/res"

echo "=== Creating Banner and SMDH ==="
$BANNERTOOL makebanner -i "$RES/banner.png" -a "$RES/banner.wav" -o "$RES/banner.bin"
$BANNERTOOL makesmdh -i "$RES/icon.png" -s "Boom3DS" -l "Boom3DS Dualscreen v0.7.3" -p "Sheldor & id Software" -f visible,allow3d,recordusage -o "$RES/icon.bin"

echo "=== Packaging CIA with makerom ==="
$MAKEROM -f cia -target t -exefslogo \
    -o "$DIR/Boom3DS-Dualscreen.cia" \
    -elf "$DIR/build/3ds/PrBoom-Plus.elf" \
    -rsf "$RES/build-cia.rsf" \
    -banner "$RES/banner.bin" \
    -icon "$RES/icon.bin" \
    -logo "$RES/logo.bcma.lz"

cp "$DIR/build/3ds/PrBoom-Plus.3dsx" "$DIR/Boom3DS-Dualscreen.3dsx"

echo "=== Build Complete! ==="
echo "Artifacts:"
ls -lh "$DIR/Boom3DS-Dualscreen.cia" "$DIR/Boom3DS-Dualscreen.3dsx"
