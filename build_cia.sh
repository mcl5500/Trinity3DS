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
$BANNERTOOL makesmdh -i "$RES/icon.png" -s "Trinity3DS" -l "Trinity3DS v1.0.3" -p "Sheldor & id Software" -f visible,allow3d,recordusage -o "$RES/icon.bin"

echo "=== Packaging CIA with makerom ==="
$MAKEROM -f cia -target t -exefslogo \
    -o "$DIR/Trinity3DS.cia" \
    -elf "$DIR/build/3ds/Trinity3DS.elf" \
    -rsf "$RES/build-cia.rsf" \
    -banner "$RES/banner.bin" \
    -icon "$RES/icon.bin" \
    -logo "$RES/logo.bcma.lz" \
    -major 1 -minor 0 -micro 1 \
    -DAPP_VERSION_MAJOR=1

cp "$DIR/build/3ds/Trinity3DS.3dsx" "$DIR/Trinity3DS.3dsx"
cp "$DIR/build/3ds/Trinity3DS.3dsx" "$DIR/Trinity3DS.3dsx"
cp "$RES/icon.bin" "$DIR/Trinity3DS.smdh"

echo "=== Build Complete! ==="
echo "Artifacts:"
ls -lh "$DIR/Trinity3DS.cia" "$DIR/Trinity3DS.3dsx"
