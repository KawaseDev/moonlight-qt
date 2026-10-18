#!/usr/bin/env python3
"""Run real transport/helper code with isolated SSH and clipboard executables."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='moonlight-clipboard-test-') as directory:
    temp = Path(directory)
    bindir = temp / 'bin'
    bindir.mkdir()
    configdir = temp / '.config/moonlight-input-capture'
    configdir.mkdir(parents=True)
    (configdir / 'clipboard.json').write_text(json.dumps({'ssh_target': 'test-stream-host', 'moonlight_host_uuid': 'test-host-uuid'}))
    copier = bindir / 'wl-copy'
    copier.write_text('''#!/usr/bin/env python3
import os,sys,time
from pathlib import Path
if os.environ.get('TEST_CLIPBOARD_FAIL'): sys.exit(1)
time.sleep(.1)
Path(os.environ['TEST_CLIPBOARD_OUTPUT']).write_bytes(sys.stdin.buffer.read())
''')
    copier.chmod(0o755)
    (configdir / 'host.json').write_text(json.dumps({'tool': str(copier), 'wayland': True, 'env': {}}))
    ssh = bindir / 'ssh'
    ssh.write_text('''#!/usr/bin/env python3
import os,sys
assert sys.argv[-2:] == ['test-stream-host', '.local/bin/moonlight-clipboard-host']
os.execv(sys.executable,[sys.executable,os.environ['TEST_HOST_SCRIPT'],'--config',os.environ['TEST_HOST_CONFIG']])
''')
    ssh.chmod(0o755)
    env = dict(os.environ, HOME=str(temp), PATH=str(bindir)+os.pathsep+os.environ['PATH'],
               QT_QPA_PLATFORM='offscreen', SDL_VIDEODRIVER='dummy',
               TEST_HOST_SCRIPT=str(root/'scripts/clipboard-host.py'),
               TEST_HOST_CONFIG=str(configdir/'host.json'),
               TEST_CLIPBOARD_OUTPUT=str(temp/'host-clipboard.txt'))
    # The built test and installed application locate transport next to themselves.
    subprocess.run([str(root/'build/app/clipboard-paste')], env=env, check=True)
