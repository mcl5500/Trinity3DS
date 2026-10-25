#!/usr/bin/env bash
set -e

IP="192.168.178.59"
HDR="X-Requested-With: 3dsDL"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Connecting to 3DS at $IP ==="
STATUS=$(curl -s --connect-timeout 3 -H "$HDR" "http://$IP/api/status" || true)
if [ -z "$STATUS" ]; then
    echo "Error: 3DS not reachable at http://$IP."
    exit 1
fi

ACCESS=$(echo "$STATUS" | jq -r '.access // empty')
if [ "$ACCESS" != "allowed" ]; then
    echo "Requesting access on 3DS screen... Please tap 'Allow' on your 3DS!"
    curl -s -H "$HDR" "http://$IP/api/list?path=%2Fcias" >/dev/null || true
    while true; do
        STATUS=$(curl -s -H "$HDR" "http://$IP/api/status")
        ACCESS=$(echo "$STATUS" | jq -r '.access // empty')
        if [ "$ACCESS" = "allowed" ]; then
            echo "Access granted! Proceeding with migration and setup..."
            break
        fi
        sleep 1
    done
fi

echo "--> Creating target directories on 3DS..."
curl -s -X POST -H "$HDR" -d "path=/3ds/Boom3DS" "http://$IP/api/folder" >/dev/null || true
curl -s -X POST -H "$HDR" -d "path=/3ds/Boom3DS/wads" "http://$IP/api/folder" >/dev/null || true
curl -s -X POST -H "$HDR" -d "path=/cias" "http://$IP/api/folder" >/dev/null || true

echo "--> Uploading Boom3DS.3dsx and Boom3DS.smdh to /3ds/Boom3DS/..."
curl -s -X PUT -H "$HDR" \
  --data-binary @"$DIR/Boom3DS.3dsx" \
  "http://$IP/api/file?path=%2F3ds%2FBoom3DS%2FBoom3DS.3dsx&replace=1" >/dev/null
curl -s -X PUT -H "$HDR" \
  --data-binary @"$DIR/Boom3DS.smdh" \
  "http://$IP/api/file?path=%2F3ds%2FBoom3DS%2FBoom3DS.smdh&replace=1" >/dev/null

echo "--> Uploading Boom3DS-Dualscreen.cia to /cias/..."
curl -s -X PUT -H "$HDR" \
  --data-binary @"$DIR/Boom3DS-Dualscreen.cia" \
  "http://$IP/api/file?path=%2Fcias%2FBoom3DS-Dualscreen.cia&replace=1" >/dev/null

echo "--> Installing CIA..."
curl -s -X POST -H "$HDR" \
  -d "path=/cias/Boom3DS-Dualscreen.cia" \
  "http://$IP/api/install" >/dev/null

echo "--> Uploading base IWADs (doom2.wad, doomu.wad) to /3ds/Boom3DS/wads/..."
SCRATCH="/home/sh3ld0r/.gemini/antigravity/brain/5e0615af-ce1f-4a9b-829e-616ea3d943ea/scratch"
for wad in "$SCRATCH/doom2.wad" "$SCRATCH/doomu.wad"; do
    if [ -f "$wad" ]; then
        fname=$(basename "$wad")
        echo "    Uploading $fname..."
        curl -s -X PUT -H "$HDR" \
          --data-binary @"$wad" \
          "http://$IP/api/file?path=%2F3ds%2FBoom3DS%2Fwads%2F${fname}&replace=1" >/dev/null
    fi
done

echo "--> Uploading popular Megawads to /3ds/Boom3DS/wads/..."
WAD_DIR="/home/sh3ld0r/Downloads/hakchi_extracted/wads"
for wad in "$WAD_DIR"/*; do
    if [ -f "$wad" ]; then
        fname=$(basename "$wad")
        echo "    Uploading $fname..."
        curl -s -X PUT -H "$HDR" \
          --data-binary @"$wad" \
          "http://$IP/api/file?path=%2F3ds%2FBoom3DS%2Fwads%2F${fname}&replace=1" >/dev/null
    fi
done

echo "--> Deleting old /3ds/PrBoom-Plus/ files on SD card..."
for old_file in "/3ds/PrBoom-Plus/prboom-plus.3dsx" \
               "/3ds/PrBoom-Plus/prboom-plus.smdh" \
               "/3ds/PrBoom-Plus/doom2.wad" \
               "/3ds/PrBoom-Plus/doomu.wad" \
               "/3ds/PrBoom-Plus/freedoom2.wad" \
               "/3ds/PrBoom-Plus/last_wad.txt" \
               "/3ds/PrBoom-Plus/log.txt" \
               "/3ds/PrBoom-Plus/prboom-plus.wad" \
               "/3ds/PrBoom-Plus/prboom-plus.cfg" \
               "/3ds/PrBoom-Plus"; do
    curl -s -X POST -H "$HDR" -d "path=${old_file}" "http://$IP/api/delete" >/dev/null || true
done

echo "=== All operations completed successfully! ==="
