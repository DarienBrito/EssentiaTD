#!/usr/bin/env bash
# Build an unsigned per-user .pkg installer for the EssentiaTD plugins.
#
# Usage: build-pkg.sh <version> <plugins-dir> <output-dir>
#   <version>      release version, e.g. 1.2.0 or 1.2.0-beta
#   <plugins-dir>  directory containing the 5 .plugin bundles
#   <output-dir>   where EssentiaTD-<version>.pkg is written
#
# The pkg installs into the current user's home (no admin password) at:
#   ~/Library/Application Support/Derivative/TouchDesigner099/Plugins/Essentia/
set -euo pipefail

VERSION="${1:?usage: build-pkg.sh <version> <plugins-dir> <output-dir>}"
PLUGIN_DIR="${2:?missing plugins dir}"
OUT_DIR="${3:?missing output dir}"

IDENTIFIER="com.darienbrito.essentiatd"

# pkg receipts want a numeric dotted version; strip pre-release suffixes
# (1.1.5-beta -> 1.1.5)
PKG_VERSION="$(echo "$VERSION" | grep -oE '^[0-9]+(\.[0-9]+)*' || true)"
PKG_VERSION="${PKG_VERSION:-0.0.0}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGING="$WORK/root"
mkdir -p "$STAGING" "$OUT_DIR"

cp -R "$PLUGIN_DIR"/*.plugin "$STAGING/"

# Bundles are relocatable by default: Installer would "upgrade" a stray copy
# found elsewhere on disk instead of installing into Plugins. Pin them.
pkgbuild --analyze --root "$STAGING" "$WORK/components.plist"
python3 - "$WORK/components.plist" <<'EOF'
import plistlib, sys
path = sys.argv[1]
with open(path, 'rb') as f:
    components = plistlib.load(f)
for c in components:
    c['BundleIsRelocatable'] = False
with open(path, 'wb') as f:
    plistlib.dump(components, f)
EOF

# Install location resolves relative to the user's home because the
# distribution enables only the currentUserHome domain. The TouchDesigner099
# folder (not "TouchDesigner") is the macOS plugin search path; the Essentia
# subfolder keeps our bundles tidy (TD scans subdirectories).
pkgbuild \
  --root "$STAGING" \
  --component-plist "$WORK/components.plist" \
  --identifier "$IDENTIFIER" \
  --version "$PKG_VERSION" \
  --install-location "/Library/Application Support/Derivative/TouchDesigner099/Plugins/Essentia" \
  "$WORK/EssentiaTD-component.pkg"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>EssentiaTD ${VERSION}</title>
    <options customize="never" require-scripts="false"/>
    <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
    <choices-outline>
        <line choice="default"/>
    </choices-outline>
    <choice id="default" title="EssentiaTD Plugins">
        <pkg-ref id="${IDENTIFIER}"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}" version="${PKG_VERSION}">EssentiaTD-component.pkg</pkg-ref>
</installer-gui-script>
EOF

# unversioned filename keeps the releases/latest/download permalink stable
# (version lives in the pkg metadata and installer title)
productbuild \
  --distribution "$WORK/distribution.xml" \
  --package-path "$WORK" \
  "$OUT_DIR/EssentiaTD.pkg"

echo "Built $OUT_DIR/EssentiaTD.pkg"
