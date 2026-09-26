#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 3 ]]; then echo "Usage: $0 BUILD STAGE QT_ROOT" >&2; exit 2; fi
build=$(cd "$1" && pwd)
mkdir -p "$2"
stage=$(cd "$2" && pwd)
qt=$(cd "$3" && pwd)
[[ "$stage" != / && ! -e "$stage/QSend.app" ]] || { echo 'Stage must be a fresh application directory' >&2; exit 2; }
cmake --install "$build" --config Release --prefix "$stage"
app="$stage/QSend.app"
mkdir -p "$app/Contents/PlugIns/platforms"
cp "$qt/plugins/platforms/libqoffscreen.dylib" "$app/Contents/PlugIns/platforms/"
"$qt/bin/macdeployqt" "$app" -always-overwrite \
    "-executable=$app/Contents/PlugIns/platforms/libqoffscreen.dylib"
# This is an ad-hoc integrity signature, not Developer ID signing/notarization.
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
lipo -verify_arch arm64 x86_64 "$app/Contents/MacOS/QSend"
echo "macOS universal package staged at $stage"
