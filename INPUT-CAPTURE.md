# Focus input capture fork

Based on Moonlight Qt v6.2.0. Streams capture keyboard shortcuts and confine the
pointer when focused, including in windowed desktop mouse mode. Win+2 and other
system shortcuts are sent to the streaming host while capture is active, provided
the compositor supports keyboard-shortcuts-inhibit (Mango does).

- **Ctrl+Alt+Shift+Z** releases or recaptures mouse and keyboard.
- While released, ordinary keyboard input stays local too.
- Click the video or leave and return focus to recapture.
- Focus loss releases capture and raises held remote keys.
- **Ctrl+Alt+Shift+K** toggles keyboard shortcut capture independently.
- **Ctrl+Alt+Shift+L** toggles pointer confinement independently.
- Existing Moonlight shortcuts, including Ctrl+Alt+Shift+Q to disconnect, remain.

New profiles default to capturing system keys Always. For existing profiles, set
Settings > Capture system keyboard shortcuts to Always. Host pairing and other
settings use the existing Moonlight profile.

On Wayland the installed wrapper forces native Qt and SDL Wayland backends so the
shortcut inhibitor belongs to the stream's real Wayland surface. Compositor-owned
emergency shortcuts and Linux virtual-terminal switching may remain reserved.

## Build and install on Arch Linux

Use Qt 6, SDL2 (or sdl2-compat), SDL2_ttf, FFmpeg, OpenSSL, Opus, Wayland, and Vulkan
headers. Initialize submodules recursively, then:

```sh
mkdir -p build
cd build
qmake6 ../moonlight-qt.pro CONFIG+=release
make -j8 release
cd ..
./scripts/install-input-capture.sh
```

The application installs in `~/.local/lib/moonlight-input-capture/`, with commands
`~/.local/bin/moonlight-input-capture` and `~/.local/bin/moonlight`. The user desktop
entry overrides the system Moonlight launcher. The distro binary is still at
`/usr/bin/moonlight`.

## Regression check

The test links the production input handler and wraps the network keyboard send
functions, so it cannot send test keystrokes to a host. It opens a temporary real
Wayland window; give that window keyboard focus within five seconds.

```sh
cd build/app
make -f ../../tests/capture-lifecycle.mk capture-lifecycle
QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=wayland WAYLAND_DEBUG=1 ./capture-lifecycle
```

It verifies windowed capture, pointer confinement, forwarding while captured,
release shortcut behavior, suppression of keys while released, recapture, and
focus loss. Wayland debug output records inhibitor creation and destruction.
This does not replace an end-to-end streaming test with the host.

To return to the distro launcher, remove the two installed commands and
`~/.local/share/applications/com.moonlight_stream.Moonlight.desktop`.
