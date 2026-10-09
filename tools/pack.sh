#!/bin/sh
# Pakai: tools/pack.sh <path/libGLES_mali.so hasil build> [folder-output]
set -e
SO="$1"
OUT="${2:-dist}"
[ -f "$SO" ] || { echo "pakai: $0 <libGLES_mali.so> [outdir]"; exit 1; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"
OUTABS=$(cd "$OUT" && pwd)
VER=$(grep '^version=' "$ROOT/module/module.prop" | cut -d= -f2)
ZIP="$OUTABS/DanzGL-$VER.zip"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp -r "$ROOT/module/." "$STAGE/"
mkdir -p "$STAGE/system/vendor/lib64/egl/mt6789"
cp "$SO" "$STAGE/system/vendor/lib64/egl/mt6789/libGLES_mali.so"

rm -f "$ZIP"
(cd "$STAGE" && zip -r9 -X "$ZIP" .)
echo "[pack] $ZIP"
