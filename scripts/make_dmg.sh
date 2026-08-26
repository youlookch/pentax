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
#
#  Signature / notarisation (optionnelles, via variables d'environnement) :
#    CODESIGN_IDENTITY="Developer ID Application: Nom (TEAMID)"
#        -> signature Developer ID avec hardened runtime + horodatage
#           (sans cette variable : signature ad-hoc, avertissement Gatekeeper
#            sur les machines qui téléchargent le DMG)
#    APPLE_ID / APPLE_TEAM_ID / APPLE_APP_PASSWORD
#        -> notarisation du DMG via notarytool + stapling
#           (APPLE_APP_PASSWORD est un "mot de passe d'app" créé sur
#            https://account.apple.com, pas le mot de passe du compte)
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

# --- 4. Signature -----------------------------------------------------------
if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    echo "Signature Developer ID : $CODESIGN_IDENTITY"
    SIGN_FLAGS=(--force --timestamp --options runtime --sign "$CODESIGN_IDENTITY")
else
    echo "CODESIGN_IDENTITY non défini : signature ad-hoc."
    SIGN_FLAGS=(--force --sign -)
fi

# Signature de l'intérieur vers l'extérieur (--deep est déprécié) : chaque
# dylib et chaque driver d'abord, le bundle complet ensuite.
while IFS= read -r -d '' lib; do
    codesign "${SIGN_FLAGS[@]}" "$lib"
done < <(find "$APP/Contents/Frameworks" "$RES/camlibs" "$RES/iolibs" \
              \( -name '*.dylib' -o -name '*.so' \) -print0)

codesign "${SIGN_FLAGS[@]}" "$APP"
codesign --verify --strict --verbose=2 "$APP"

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

if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --timestamp --sign "$CODESIGN_IDENTITY" "$DMG"
fi

# --- 6. Notarisation Apple (optionnelle) ------------------------------------
if [[ -n "${CODESIGN_IDENTITY:-}" && -n "${APPLE_ID:-}" \
      && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
    echo "Notarisation du DMG (quelques minutes)..."
    xcrun notarytool submit "$DMG" \
        --apple-id "$APPLE_ID" \
        --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" \
        --wait
    xcrun stapler staple "$DMG"
    echo "DMG notarisé et staplé : ouvrable sans avertissement Gatekeeper."
else
    echo "Notarisation ignorée (APPLE_ID/APPLE_TEAM_ID/APPLE_APP_PASSWORD absents)."
fi

echo ""
echo "OK : $DMG"
