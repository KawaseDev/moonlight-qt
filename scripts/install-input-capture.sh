#!/usr/bin/env bash
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
install_dir="$HOME/.local/lib/moonlight-input-capture"
bin_dir="$HOME/.local/bin"
application_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
install -Dm755 "$source_dir/build/app/moonlight" "$install_dir/moonlight"
install -Dm755 "$source_dir/scripts/clipboard-send.py" "$install_dir/clipboard-send.py"
mkdir -p "$bin_dir" "$application_dir"
cat > "$bin_dir/moonlight-input-capture" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail
# Shortcut inhibition must target a native Wayland surface on Wayland desktops.
if [[ "${XDG_SESSION_TYPE:-}" == wayland ]]; then
    export QT_QPA_PLATFORM=wayland
    export SDL_VIDEODRIVER=wayland
fi
exec "$HOME/.local/lib/moonlight-input-capture/moonlight" "$@"
WRAPPER
chmod 755 "$bin_dir/moonlight-input-capture"
ln -sfn moonlight-input-capture "$bin_dir/moonlight"
cat > "$application_dir/com.moonlight_stream.Moonlight.desktop" <<DESKTOP
[Desktop Entry]
Name=Moonlight (Input Capture)
Comment=Stream your PC with focus-based keyboard and mouse capture; Ctrl+Alt+Shift+Z releases input
Exec="$bin_dir/moonlight-input-capture"
Icon=moonlight
Terminal=false
Type=Application
Categories=Qt;Game;Network;RemoteAccess;
Keywords=moonlight;sunshine;remote play;input capture;
DESKTOP
if command -v update-desktop-database >/dev/null; then
    update-desktop-database "$application_dir"
fi
printf 'Installed %s\n' "$bin_dir/moonlight-input-capture"
