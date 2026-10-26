#!/bin/bash
# Builds Better-MPC-HC-<version>-x86_64.AppImage from the Windows installer.
# The AppImage contains a portable Wine, so it runs on Linux without installing anything else.
#
# Usage: linux/build-appimage.sh <path to Better-MPC-HC.<version>.x64.exe> [output folder]
# Needs: wget, tar with xz support and ImageMagick. Downloads are cached in linux/cache.
set -euo pipefail

WINE_VERSION=11.0
WINE_NAME="wine-$WINE_VERSION-amd64-wow64"
WINE_URL="https://github.com/Kron4ek/Wine-Builds/releases/download/$WINE_VERSION/$WINE_NAME.tar.xz"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <path to Better-MPC-HC.<version>.x64.exe> [output folder]" >&2
    exit 1
fi

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_DIR=$(dirname "$SCRIPT_DIR")
INSTALLER=$(readlink -f "$1")
OUT_DIR=$(readlink -f "${2:-$REPO_DIR/bin}")
CACHE_DIR="$SCRIPT_DIR/cache"
WORK_DIR="$SCRIPT_DIR/build"
APPDIR="$WORK_DIR/AppDir"

VERSION=$(basename "$INSTALLER" | sed -n 's/^Better-MPC-HC\.\([0-9.]*[0-9]\)\.x64\.exe$/\1/p')
if [ -z "$VERSION" ]; then
    echo "The installer name has to be Better-MPC-HC.<version>.x64.exe" >&2
    exit 1
fi

download() {
    if [ ! -f "$2" ]; then
        wget -q --show-progress -O "$2.part" "$1"
        mv "$2.part" "$2"
    fi
}

mkdir -p "$CACHE_DIR" "$OUT_DIR"
download "$WINE_URL" "$CACHE_DIR/$WINE_NAME.tar.xz"
download "$APPIMAGETOOL_URL" "$CACHE_DIR/appimagetool-x86_64.AppImage"
download "$RUNTIME_URL" "$CACHE_DIR/runtime-x86_64"
chmod +x "$CACHE_DIR/appimagetool-x86_64.AppImage"

rm -rf "$WORK_DIR"
mkdir -p "$APPDIR/opt"

echo "Extracting Wine $WINE_VERSION..."
tar -xJf "$CACHE_DIR/$WINE_NAME.tar.xz" -C "$APPDIR"
mv "$APPDIR/$WINE_NAME" "$APPDIR/wine"
WINE="$APPDIR/wine/bin/wine"

echo "Installing Better MPC-HC $VERSION with Wine..."
export WINEPREFIX="$WORK_DIR/prefix"
export WINEDEBUG=-all
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
"$WINE" wineboot -i
"$WINE" "$INSTALLER" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOICONS /TASKS= "/DIR=C:\\Better MPC-HC"
"$APPDIR/wine/bin/wineserver" -w
if [ ! -f "$WINEPREFIX/drive_c/Better MPC-HC/Better-MPC-HC64.exe" ]; then
    echo "The installation failed" >&2
    exit 1
fi
cp -a "$WINEPREFIX/drive_c/Better MPC-HC" "$APPDIR/opt/better-mpc-hc"
rm -f "$APPDIR/opt/better-mpc-hc"/unins000.*

echo "Creating the AppImage..."
install -m 755 "$SCRIPT_DIR/AppRun" "$APPDIR/AppRun"
cp "$SCRIPT_DIR/better-mpc-hc.desktop" "$SCRIPT_DIR/defaults.reg" "$APPDIR/"
cp "$SCRIPT_DIR/THIRD-PARTY.txt" "$APPDIR/"

# the largest image of the Windows icon
ICON="$REPO_DIR/src/mpc-hc/res/icon.ico"
FRAME=$(identify -format '%p %w\n' "$ICON" | sort -k2 -n | tail -n 1 | cut -d ' ' -f 1)
convert "$ICON[$FRAME]" -resize 256x256 "$APPDIR/better-mpc-hc.png"
ln -s better-mpc-hc.png "$APPDIR/.DirIcon"

OUTPUT="$OUT_DIR/Better-MPC-HC-$VERSION-x86_64.AppImage"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$CACHE_DIR/appimagetool-x86_64.AppImage" --no-appstream \
    --runtime-file "$CACHE_DIR/runtime-x86_64" "$APPDIR" "$OUTPUT"
echo "Done: $OUTPUT"
