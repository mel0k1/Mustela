#!/usr/bin/env bash
set -euo pipefail

DEST="${1:-/tmp/deb/sdk}"
COMMIT="8ece20377cd3f0aa0aadb9d61535a703c43f4612"
BASE="https://raw.githubusercontent.com/WebKit/WebKit/${COMMIT}"
DIRS=(
    "Source/WebKit/UIProcess/API/C"
    "Source/WebKit/UIProcess/API/C/win"
    "Source/WebKit/Shared/API/c"
    "Source/WebKit/Shared/API/c/win"
    "Source/WebKit/Shared/API/c/gtk"
    "Source/WebKit/Shared/API/c/mac"
    "Source/WebKit/Shared/API/c/wpe"
    "Source/WebKit/Shared/API/c/playstation"
)
TARGET="$DEST/WebKit"
mkdir -p "$TARGET"

count=0
failed=0
while IFS= read -r name; do
    [ -z "$name" ] && continue
    if [ -f "$TARGET/$name" ]; then
        count=$((count + 1))
        continue
    fi
    fetched=0
    for dir in "${DIRS[@]}"; do
        url="$BASE/$dir/$name"
        if curl -fsSL "$url" -o "$TARGET/$name" 2>/dev/null; then
            if ! head -c 200 "$TARGET/$name" | grep -q "^404"; then
                fetched=1
                break
            fi
        fi
        rm -f "$TARGET/$name"
    done
    if [ "$fetched" -eq 1 ]; then
        count=$((count + 1))
    else
        echo "FAILED: $name" >&2
        failed=$((failed + 1))
    fi
done < scripts/win-headers.txt

echo "fetched $count headers, $failed failed"