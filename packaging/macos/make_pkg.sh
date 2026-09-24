#!/usr/bin/env bash
# Builds a macOS installer package containing the VST3, AU and Standalone app.
# Usage: make_pkg.sh <version> <artefacts dir (…/Swarmness_artefacts/Release)> <output dir>
set -euo pipefail

VERSION="$1"
ART="$(cd "$2" && pwd)"
OUT="$3"
ID="com.OpenAudio.Swarmness"

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

stage() {  # stage <name> <bundle> <install location>
  mkdir -p "$WORK/$1/root"
  cp -R "$2" "$WORK/$1/root/"
  # Never relocate: always install to the standard plug-in folders.
  pkgbuild --analyze --root "$WORK/$1/root" "$WORK/$1.plist"
  /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$WORK/$1.plist"
  pkgbuild --root "$WORK/$1/root" --component-plist "$WORK/$1.plist" --identifier "$ID.$1" --version "$VERSION" \
           --install-location "$3" "$WORK/$1.pkg"
}

stage vst3       "$ART/VST3/Swarmness.vst3"       "/Library/Audio/Plug-Ins/VST3"
stage au         "$ART/AU/Swarmness.component"    "/Library/Audio/Plug-Ins/Components"
stage standalone "$ART/Standalone/Swarmness.app"  "/Applications"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Swarmness ${VERSION}</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline>
    <line choice="vst3"/>
    <line choice="au"/>
    <line choice="standalone"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plug-in" description="Installs Swarmness.vst3 to /Library/Audio/Plug-Ins/VST3">
    <pkg-ref id="${ID}.vst3"/>
  </choice>
  <choice id="au" title="Audio Unit plug-in" description="Installs Swarmness.component to /Library/Audio/Plug-Ins/Components">
    <pkg-ref id="${ID}.au"/>
  </choice>
  <choice id="standalone" title="Standalone app" description="Installs Swarmness.app to /Applications">
    <pkg-ref id="${ID}.standalone"/>
  </choice>
  <pkg-ref id="${ID}.vst3" version="${VERSION}">vst3.pkg</pkg-ref>
  <pkg-ref id="${ID}.au" version="${VERSION}">au.pkg</pkg-ref>
  <pkg-ref id="${ID}.standalone" version="${VERSION}">standalone.pkg</pkg-ref>
</installer-gui-script>
XML

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" \
             "$OUT/Swarmness-${VERSION}-macOS-Universal.pkg"
echo "Created $OUT/Swarmness-${VERSION}-macOS-Universal.pkg"
