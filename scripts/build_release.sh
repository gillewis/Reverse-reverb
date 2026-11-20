#!/bin/bash
# Builds Reverse Reverb (VST3 + AU + Standalone, universal) and assembles the release folder
# with a macOS installer package.
#
#   ./scripts/build_release.sh                 unsigned build (fine for testing on your own Mac)
#   SIGN_ID_APP="Developer ID Application: Your Name (TEAMID)" \
#   SIGN_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)" \
#   NOTARY_PROFILE="your-notarytool-profile" ./scripts/build_release.sh
#                                             signed + notarised build, ready to sell
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION=$(sed -n 's/^project(ReverseReverb VERSION \([0-9.]*\)).*/\1/p' CMakeLists.txt)
BUNDLE_ID=$(sed -n 's/^set(RR_BUNDLE_ID *"\(.*\)")/\1/p' CMakeLists.txt)
NAME="Reverse Reverb"
OUT="$ROOT/Release/$NAME $VERSION"
STAGE="$ROOT/build-release/stage"

echo "== Building $NAME $VERSION"
cmake -B build-release -G Xcode -DRR_BUILD_TESTS=OFF -DRR_COPY_PLUGINS=OFF -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" > /dev/null
cmake --build build-release --config Release \
      --target ReverseReverb_VST3 ReverseReverb_AU ReverseReverb_Standalone -- -quiet

ART="build-release/ReverseReverb_artefacts/Release"
rm -rf "$STAGE" "$OUT"
mkdir -p "$STAGE/vst3" "$STAGE/au" "$STAGE/app" "$OUT/Mac/Manual Install"
cp -R "$ART/VST3/$NAME.vst3"          "$STAGE/vst3/"
cp -R "$ART/AU/$NAME.component"       "$STAGE/au/"
cp -R "$ART/Standalone/$NAME.app"     "$STAGE/app/"

if [[ -n "${SIGN_ID_APP:-}" ]]; then
  echo "== Code signing"
  for b in "$STAGE/vst3/$NAME.vst3" "$STAGE/au/$NAME.component" "$STAGE/app/$NAME.app"; do
    codesign --force --deep --timestamp --options runtime --sign "$SIGN_ID_APP" "$b"
  done
fi

echo "== Building installer"
PKGS="build-release/pkgs"; rm -rf "$PKGS"; mkdir -p "$PKGS"
pkgbuild --root "$STAGE/vst3" --install-location "/Library/Audio/Plug-Ins/VST3" \
         --identifier "$BUNDLE_ID.vst3" --version "$VERSION" "$PKGS/vst3.pkg" > /dev/null
pkgbuild --root "$STAGE/au"   --install-location "/Library/Audio/Plug-Ins/Components" \
         --identifier "$BUNDLE_ID.au"   --version "$VERSION" "$PKGS/au.pkg" > /dev/null
pkgbuild --root "$STAGE/app"  --install-location "/Applications" \
         --identifier "$BUNDLE_ID.app"  --version "$VERSION" "$PKGS/app.pkg" > /dev/null

cat > "$PKGS/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <license file="EULA.txt"/>
    <readme file="README.txt"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>
    <choice id="vst3" title="VST3 plug-in" description="For Cubase, Nuendo, Ableton Live, Studio One, Reaper, FL Studio, Bitwig and others.">
        <pkg-ref id="$BUNDLE_ID.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit plug-in" description="For Logic Pro, GarageBand, Ableton Live and others.">
        <pkg-ref id="$BUNDLE_ID.au"/>
    </choice>
    <choice id="app" title="Standalone app" description="Use Reverse Reverb on its own, outside a DAW.">
        <pkg-ref id="$BUNDLE_ID.app"/>
    </choice>
    <pkg-ref id="$BUNDLE_ID.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$BUNDLE_ID.au" version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="$BUNDLE_ID.app" version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
EOF

RES="build-release/pkg-resources"; rm -rf "$RES"; mkdir -p "$RES"
cp "$ROOT/Distribution/EULA.txt" "$ROOT/Distribution/README.txt" "$RES/"

PKG="$OUT/Mac/$NAME $VERSION Installer.pkg"
SIGN_ARGS=()
[[ -n "${SIGN_ID_INSTALLER:-}" ]] && SIGN_ARGS=(--sign "$SIGN_ID_INSTALLER" --timestamp)
productbuild --distribution "$PKGS/distribution.xml" --package-path "$PKGS" \
             --resources "$RES" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$PKG" > /dev/null

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
  echo "== Notarising (this can take a few minutes)"
  xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$PKG"
fi

echo "== Assembling release folder"
cp -R "$STAGE/vst3/$NAME.vst3" "$STAGE/au/$NAME.component" "$STAGE/app/$NAME.app" "$OUT/Mac/Manual Install/"
cp "$ROOT/Distribution/README.txt" "$ROOT/Distribution/EULA.txt" "$OUT/"
[[ -f "$ROOT/Distribution/User Guide.pdf" ]] && cp "$ROOT/Distribution/User Guide.pdf" "$OUT/"

( cd "$ROOT/Release" && rm -f "$NAME $VERSION Mac.zip" && ditto -c -k --keepParent "$NAME $VERSION" "$NAME $VERSION Mac.zip" )

if [[ -z "${SIGN_ID_APP:-}" ]]; then
  echo
  echo "NOTE: this build is NOT signed or notarised. It will run on this Mac, but other"
  echo "      people's Macs will block it. See SELLING.md, step 2."
fi
echo "Done: $OUT"
