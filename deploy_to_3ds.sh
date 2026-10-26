#!/usr/bin/env bash
set -e

IP="192.168.178.59"
HDR="X-Requested-With: 3dsDL"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Connecting to 3DS at $IP ==="
STATUS=$(curl -s --connect-timeout 3 -H "$HDR" "http://$IP/api/status" || true)
if [ -z "$STATUS" ]; then
    echo "Error: 3DS not reachable at http://$IP. Make sure 3dsDL is running!"
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
            echo "Access granted! Starting transfer..."
            break
        fi
        sleep 1
    done
fi

# Ensure folders exist
curl -s -X POST -H "$HDR" -d "path=/3ds/Trinity3DS" "http://$IP/api/folder" >/dev/null || true
curl -s -X POST -H "$HDR" -d "path=/3ds/Trinity3DS/wads" "http://$IP/api/folder" >/dev/null || true
curl -s -X POST -H "$HDR" -d "path=/cias" "http://$IP/api/folder" >/dev/null || true

# 1. Upload .3dsx and .smdh to /3ds/Trinity3DS/
if [ -f "$DIR/Trinity3DS.3dsx" ]; then
    echo "--> Uploading Trinity3DS.3dsx to /3ds/Trinity3DS/Trinity3DS.3dsx..."
    curl -s -X PUT -H "$HDR" \
      --data-binary @"$DIR/Trinity3DS.3dsx" \
      "http://$IP/api/file?path=%2F3ds%2FBoom3DS%2FTrinity3DS.3dsx&replace=1" >/dev/null
    echo "    Upload complete!"
fi

if [ -f "$DIR/Trinity3DS.smdh" ]; then
    echo "--> Uploading Trinity3DS.smdh to /3ds/Trinity3DS/Trinity3DS.smdh..."
    curl -s -X PUT -H "$HDR" \
      --data-binary @"$DIR/Trinity3DS.smdh" \
      "http://$IP/api/file?path=%2F3ds%2FBoom3DS%2FTrinity3DS.smdh&replace=1" >/dev/null
    echo "    Upload complete!"
fi

if [ -f "$DIR/prboom-plus.wad" ]; then
    echo "--> Uploading prboom-plus.wad to /3ds/Trinity3DS/prboom-plus.wad..."
    curl -s -X PUT -H "$HDR" \
      --data-binary @"$DIR/prboom-plus.wad" \
      "http://$IP/api/file?path=%2F3ds%2FBoom3DS%2Fprboom-plus.wad&replace=1" >/dev/null
    echo "    Upload complete!"
fi

if [ -f "$DIR/launcher.ogg" ]; then
    echo "--> Uploading Launcher Music (Doom 1 E1M1) to /3ds/Trinity3DS/launcher.ogg..."
    curl -s -X PUT -H "$HDR" \
      --data-binary @"$DIR/launcher.ogg" \
      "http://$IP/api/file?path=%2F3ds%2FBoom3DS%2Flauncher.ogg&replace=1" >/dev/null
    echo "    Upload complete!"
fi



# 2. Upload and install CIA
if [ -f "$DIR/Trinity3DS.cia" ]; then
    echo "--> Uploading Trinity3DS.cia to /cias/..."
    curl -s -X PUT -H "$HDR" \
      --data-binary @"$DIR/Trinity3DS.cia" \
      "http://$IP/api/file?path=%2Fcias%2FTrinity3DS.cia&replace=1" >/dev/null
    echo "    Upload complete!"

    echo "--> Triggering CIA install on 3DS..."
    curl -s -X POST -H "$HDR" \
      -d "paths=/cias/Trinity3DS.cia" \
      "http://$IP/api/install" >/dev/null
    echo "    Installation command sent to 3DS!"
fi

echo "=== Deployment finished successfully! ==="
