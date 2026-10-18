#!/usr/bin/env python3
"""Upload clipboard text through SSH; acknowledge only after the host owns it."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

MAX_BYTES = 16 * 1024 * 1024

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True)
    parser.add_argument('--host-id', required=True)
    args = parser.parse_args()
    try:
        config = json.loads(Path(args.config).read_text())
        if not args.host_id or config.get('moonlight_host_uuid') != args.host_id:
            raise ValueError('Clipboard helper is not paired with this streaming host.')
        target = config['ssh_target']
        if not isinstance(target, str) or not target or target.startswith('-') or any(c.isspace() for c in target):
            raise ValueError('Invalid SSH target; use an alias from ~/.ssh/config or user@host.')
        data = sys.stdin.buffer.read(MAX_BYTES + 1)
        if len(data) > MAX_BYTES:
            raise ValueError('Clipboard exceeds the 16 MiB limit.')
        data.decode('utf-8', errors='strict')
        command = ['ssh', '-T', '-oBatchMode=yes', '-oConnectTimeout=3', '-oStrictHostKeyChecking=yes', target, '.local/bin/moonlight-clipboard-host']
        result = subprocess.run(command, input=data, capture_output=True, timeout=6)
        if result.returncode != 0 or result.stdout != b'MOONLIGHT_CLIPBOARD_OK\n':
            raise RuntimeError('SSH clipboard transfer failed. Check SSH access and the installed host helper.')
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
