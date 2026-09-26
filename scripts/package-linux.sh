#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 3 ]]; then echo "Usage: $0 BUILD STAGE QT_ROOT" >&2; exit 2; fi
build=$(cd "$1" && pwd)
mkdir -p "$2"
stage=$(cd "$2" && pwd)
qt=$(cd "$3" && pwd)
[[ "$stage" != / && ! -e "$stage/bin/QSend" ]] || { echo 'Stage must be a fresh application directory' >&2; exit 2; }
cmake --install "$build" --config Release --prefix "$stage"
# Qt's deployment API copies non-system dependencies and plugins. Keep the
# offscreen platform available for the same packaged-app test used in CI.
mkdir -p "$stage/plugins/platforms"
cp "$qt/plugins/platforms/libqoffscreen.so" "$stage/plugins/platforms/"
cat > "$stage/bin/qt.conf" <<'CONF'
[Paths]
Prefix=..
Libraries=lib
Plugins=plugins
CONF
cat > "$stage/run-qsend.sh" <<'LAUNCH'
#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$root/plugins"
exec "$root/bin/QSend" "$@"
LAUNCH
chmod +x "$stage/run-qsend.sh" "$stage/bin/QSend"
[[ -f "$stage/plugins/platforms/libqxcb.so" ]]
[[ -f "$stage/plugins/tls/libqopensslbackend.so" ]]
echo "Linux package staged at $stage"
