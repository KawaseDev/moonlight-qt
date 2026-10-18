#!/usr/bin/env python3
"""Set the streaming user's real Linux clipboard through an authenticated SSH session."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile

CONFIG = Path.home() / '.config/moonlight-input-capture/host.json'
MAX_BYTES = 16 * 1024 * 1024
ACK = b'MOONLIGHT_CLIPBOARD_OK\n'

def install(authorized_key=None):
    # Run this in a terminal in the desktop being streamed, not through SSH.
    wayland = os.environ.get('WAYLAND_DISPLAY')
    tool = shutil.which('wl-copy' if wayland else 'xclip')
    if not tool:
        raise RuntimeError('Install wl-clipboard (Wayland) or xclip (X11), then rerun this command.')
    if not wayland and not os.environ.get('DISPLAY'):
        raise RuntimeError('Run --install inside a terminal on the streamed desktop.')
    env = {key: os.environ[key] for key in ('WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR', 'DISPLAY', 'XAUTHORITY') if key in os.environ}
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    CONFIG.write_text(json.dumps({'tool': tool, 'wayland': bool(wayland), 'env': env}) + '\n')
    CONFIG.chmod(0o600)
    destination = Path.home() / '.local/bin/moonlight-clipboard-host'
    destination.parent.mkdir(parents=True, exist_ok=True)
    if Path(__file__).resolve() != destination.resolve():
        shutil.copyfile(__file__, destination)
    destination.chmod(0o755)
    if authorized_key:
        fields = authorized_key.split()
        if len(fields) < 2 or fields[0] != 'ssh-ed25519':
            raise RuntimeError('Expected an Ed25519 SSH public key.')
        ssh_dir = Path.home() / '.ssh'
        ssh_dir.mkdir(mode=0o700, exist_ok=True)
        keyfile = ssh_dir / 'authorized_keys'
        current = keyfile.read_text() if keyfile.exists() else ''
        command = str(destination).replace('\\', '\\\\').replace('"', '\\"')
        entry = f'restrict,command="{command}" {fields[0]} {fields[1]} moonlight-clipboard\n'
        if entry.strip() not in current.splitlines():
            with keyfile.open('a') as handle:
                handle.write(('\n' if current and not current.endswith('\n') else '') + entry)
        keyfile.chmod(0o600)
    print('Clipboard helper installed. No network listener is opened; transport uses SSH.')
    print('SSH username:', os.environ.get('USER', ''))
    print('Hostname:', socket.gethostname())
    print('Session type:', os.environ.get('XDG_SESSION_TYPE', 'unknown'))
    print('SSH service must be reachable for clipboard transfer.')

def receive(config_path):
    config = json.loads(Path(config_path).read_text())
    data = sys.stdin.buffer.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise RuntimeError('Clipboard exceeds the 16 MiB limit.')
    data.decode('utf-8', errors='strict')
    if b'\0' in data:
        raise RuntimeError('Clipboard text contains a NUL byte.')
    environment = dict(os.environ, **config['env'])
    args = [config['tool'], '--type', 'text/plain;charset=utf-8'] if config['wayland'] else [config['tool'], '-selection', 'clipboard', '-in']
    # Clipboard owners may fork and retain descriptors. A temporary file avoids
    # waiting on a daemon's inherited stderr pipe after the parent has exited.
    with tempfile.TemporaryFile() as errors:
        result = subprocess.run(args, input=data, env=environment, stdout=subprocess.DEVNULL, stderr=errors, timeout=4)
        if result.returncode:
            raise RuntimeError('Desktop clipboard backend failed. Rerun helper --install in the current desktop session.')
    sys.stdout.buffer.write(ACK)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    parser.add_argument('--authorized-key', help='Authorize a dedicated key restricted to this clipboard helper.')
    parser.add_argument('--config', default=str(CONFIG))
    args = parser.parse_args()
    try:
        install(args.authorized_key) if args.install else receive(args.config)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        print('Clipboard helper:', error, file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
