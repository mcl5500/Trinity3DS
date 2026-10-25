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

# Ensure directories exist
curl -s -X POST -H "$HDR" -d "path=/3ds/PrBoom-Plus" "http://$IP/api/folder" >/dev/null || true
curl -s -X POST -H "$HDR" -d "path=/cias" "http://$IP/api/folder" >/dev/null || true

# 1. Upload .3dsx
if [ -f "$DIR/Boom3DS-Dualscreen.3dsx" ]; then
    echo "--> Uploading Boom3DS-Dualscreen.3dsx to /3ds/PrBoom-Plus/prboom-plus.3dsx..."
    curl -s -X PUT -H "$HDR" \
      --data-binary @"$DIR/Boom3DS-Dualscreen.3dsx" \
      "http://$IP/api/file?path=%2F3ds%2FPrBoom-Plus%2Fprboom-plus.3dsx&replace=1" >/dev/null
    echo "    Upload complete!"
fi

# 2. Upload .cia
if [ -f "$DIR/Boom3DS-Dualscreen.cia" ]; then
    echo "--> Uploading Boom3DS-Dualscreen.cia to /cias/..."
    curl -s -X PUT -H "$HDR" \
      --data-binary @"$DIR/Boom3DS-Dualscreen.cia" \
      "http://$IP/api/file?path=%2Fcias%2FBoom3DS-Dualscreen.cia&replace=1" >/dev/null
    echo "    Upload complete!"

    echo "--> Triggering CIA install on 3DS..."
    curl -s -X POST -H "$HDR" \
      -d "paths=/cias/Boom3DS-Dualscreen.cia" \
      "http://$IP/api/install" >/dev/null
    echo "    Installation command sent to 3DS!"
fi

echo "=== Deployment finished successfully! ==="
