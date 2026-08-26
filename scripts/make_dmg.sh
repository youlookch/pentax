#!/usr/bin/env bash
# ============================================================================
#  Construit "Pentax 645Z Tether.app" autonome et le paquette en .dmg (macOS).
#
#  Prérequis : brew install cmake ninja pkg-config libgphoto2 dylibbundler
#
#  Le bundle produit est autonome : les dylibs de libgphoto2/libusb sont
#  recopiées dans Contents/Frameworks et les drivers (camlibs/iolibs) dans
#  Contents/Resources — l'application les retrouve via CAMLIBS/IOLIBS
#  (voir Source/Main.cpp). Le DMG fonctionne donc sur un Mac sans Homebrew.
#
#  Usage : ./scripts/make_dmg.sh            -> dist/PentaxTether-<version>.dmg
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "$(uname)" != "Darwin" ]]; then
    echo "Erreur : ce script doit être exécuté sur macOS (hdiutil requis)." >&2
    exit 1
fi

APP_NAME="Pentax 645Z Tether"
VERSION="$(sed -n 's/^project(PentaxTether VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
BUILD_DIR="build"
DIST_DIR="dist"
APP="$BUILD_DIR/PentaxTether_artefacts/Release/$APP_NAME.app"

# --- 1. Compilation Release -------------------------------------------------
cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

# --- 2. Embarquer les drivers libgphoto2 dans le bundle ---------------------
CAMLIB_DIR="$(pkg-config --variable=driverdir libgphoto2)"
IOLIB_DIR="$(pkg-config --variable=driverdir libgphoto2_port)"

RES="$APP/Contents/Resources"
rm -rf "$RES/camlibs" "$RES/iolibs"
mkdir -p "$RES/camlibs" "$RES/iolibs"
cp "$CAMLIB_DIR"/*.so "$RES/camlibs/"
cp "$IOLIB_DIR"/*.so "$RES/iolibs/"

# --- 3. Recopier les dylibs et réécrire tous les chemins de liaison ---------
# dylibbundler traite l'exécutable ET chaque driver (les .so référencent eux
# aussi libgphoto2_port, libusb, etc.).
FIX_ARGS=(-x "$APP/Contents/MacOS/$APP_NAME")
for so in "$RES"/camlibs/*.so "$RES"/iolibs/*.so; do
    FIX_ARGS+=(-x "$so")
done

dylibbundler -od -b "${FIX_ARGS[@]}" \
             -d "$APP/Contents/Frameworks" \
             -p '@executable_path/../Frameworks'

# --- 4. Signature ad-hoc (le bundle a été modifié) --------------------------
codesign --force --deep --sign - "$APP"

# --- 5. Création du DMG -----------------------------------------------------
STAGING="$DIST_DIR/dmg-staging"
rm -rf "$STAGING"
mkdir -p "$STAGING"
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"

DMG="$DIST_DIR/PentaxTether-${VERSION:-1.0.0}.dmg"
rm -f "$DMG"
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGING" -ov -format UDZO "$DMG"
rm -rf "$STAGING"

echo ""
echo "OK : $DMG"
