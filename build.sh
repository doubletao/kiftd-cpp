#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=============================="
echo " kiftd-cpp build & deploy"
echo "=============================="

# Step 1: Build frontend
echo
echo "[1/3] Building frontend..."
cd "$SCRIPT_DIR/web"
if [ ! -d node_modules ]; then
    echo "  Installing dependencies..."
    npm install
fi
npm run build
echo "  Frontend build OK."
cd "$SCRIPT_DIR"

# Step 2: Build backend
echo
echo "[2/3] Building backend..."
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(nproc)"
echo "  Backend build OK."

# Step 3: Deploy to bin
echo
echo "[3/3] Deploying to bin..."
BIN_DIR=bin
mkdir -p "$BIN_DIR/data/files"
mkdir -p "$BIN_DIR/data/transcode"

cp -f build/kiftd "$BIN_DIR/"
mkdir -p "$BIN_DIR/web"
cp -r web/dist "$BIN_DIR/web/"

echo "  Deploy OK."

echo
echo "=============================="
echo " Done! Output: bin/"
echo "=============================="
