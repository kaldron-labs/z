import os
import select
import subprocess
from pathlib import Path

root = Path('/home/count0/src/z')
status = Path('/tmp/zffb-final-parity-refresh.status')
stage = Path('/tmp/zffb-final-parity-refresh.stage')
Path('/tmp/zffb-final-parity-refresh.pid').write_text(str(os.getpid()))
try:
    jobs = [('tree', ['make', '-j4']),
            ('dashboard', ['make', '-C', 'zdash', '-j4'])]
    jobs += [(module + '-interop', ['make', '-C', module + '/interop', '-j4'])
             for module in ('zf', 'zi', 'zdb', 'ztc')]
    for name, command in jobs:
        stage.write_text(name + '\n')
        with open('/tmp/zffb-final-parity-' + name + '-build.log', 'w') as log:
            subprocess.run(command, cwd=root, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
    stage.write_text('complete\n')
    status.write_text('0\n')
except BaseException as error:
    status.write_text(str(getattr(error, 'returncode', 1)) + '\n')
    raise
