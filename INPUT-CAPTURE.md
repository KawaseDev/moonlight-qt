# Focus input capture fork

Based on Moonlight Qt v6.2.0. Streams capture keyboard shortcuts when focused. In desktop mouse mode the
pointer can freely leave the stream window; leaving releases input capture. Win+2 and other
system shortcuts are sent to the streaming host while capture is active, provided
the compositor supports keyboard-shortcuts-inhibit (Mango does).

- **Ctrl+Alt+Shift+Z** releases or recaptures mouse and keyboard.
- While released, ordinary keyboard input stays local too.
- Click the video or leave and return focus to recapture.
- Moving the pointer outside the stream or losing focus releases capture and
  raises held remote keys and mouse buttons.
- **Ctrl+Alt+Shift+K** toggles keyboard shortcut capture independently.
- Desktop mouse mode is the default; **Ctrl+Alt+Shift+M** switches to relative
  mouse mode for games. Relative mode captures the pointer until released.
- **Ctrl+Alt+Shift+L** explicitly toggles pointer confinement in desktop mode.
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

It verifies windowed capture, free pointer movement, release on pointer leave,
held-button cleanup, optional pointer confinement, forwarding while captured,
release shortcut behavior, suppression of keys while released, recapture, and
focus loss. Wayland debug output records inhibitor creation and destruction.
This does not replace an end-to-end streaming test with the host.

To return to the distro launcher, remove the two installed commands and
`~/.local/share/applications/com.moonlight_stream.Moonlight.desktop`.

## Native Linux clipboard paste

This fork can transfer **text from this PC to the streamed Linux PC's real
clipboard** over SSH, then send an ordinary Ctrl+V only after acknowledgement.
It does not use Moonlight's Unicode typing mechanism. Images, files, and copying
from the streamed PC back to this PC are not supported by this helper.

- Ctrl+V and Ctrl+Shift+V paste local text after upload succeeds.
- Ctrl+Alt+Shift+V explicitly selects the local clipboard through the same helper.
- After Ctrl+C/Ctrl+X inside the stream, normal paste uses the host clipboard until
  local clipboard contents change or you leave and return to the stream.
- Transfers run outside the stream event loop. Failure sends no paste. Releasing
  capture, switching away, clicking, or typing another key cancels delayed paste.
- The clipboard SSH target is bound to the Moonlight host UUID to prevent transfer
  to a helper belonging to a different PC. Nothing is synchronized automatically.

On the streamed PC, install Python 3, OpenSSH server, and either `wl-clipboard`
(Wayland) or `xclip` (X11). Run `scripts/clipboard-host.py --install` inside the
actual desktop session. It stores that session's display environment and installs
`~/.local/bin/moonlight-clipboard-host`. Rerun installation if the display session
changes. Optional `--authorized-key 'ssh-ed25519 ...'` installs a dedicated SSH key
restricted to this helper; it cannot open a shell or forward connections.

Configure a working key-based SSH alias on this PC, including an IdentityFile and
ProxyJump if needed. Verify the server host key once interactively. Create
`~/.config/moonlight-input-capture/clipboard.json` with mode 600:

```json
{"ssh_target": "moonlight-clipboard", "moonlight_host_uuid": "UUID from Moonlight's host profile"}
```

SSH uses BatchMode and StrictHostKeyChecking; authentication or host-key failure
cannot silently fall back to another paste mechanism. No extra service port is
opened by the clipboard helper. The SSH service itself must be reachable.

Transport tests use the actual uploader and host-helper programs with isolated
SSH/clipboard executables and a dummy SDL clipboard, without touching the user's
clipboard or contacting a remote host:

```sh
cd build/app
cp ../../scripts/clipboard-send.py .
make -f ../../tests/capture-lifecycle.mk clipboard-paste
python3 ../../tests/run-clipboard-test.py
```
