import os
import select
import subprocess
import tempfile
from pathlib import Path

root = Path('/home/count0/src/z')
prefix = '/tmp/zffb-envelope-repair-check'
stage = Path(prefix + '.stage')
Path(prefix + '.pid').write_text(str(os.getpid()) + '\n')
env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')

def run(name, command, environment=env):
    stage.write_text(name + '\n')
    with open(prefix + '-' + name + '.log', 'w') as log:
        subprocess.run(command, cwd=root, env=environment, stdout=log,
                       stderr=subprocess.STDOUT, check=True)

def suite(directory, environment=env):
    name = directory.replace('/', '-')
    run(name + '-current', ['make', '-C', directory, '-q', 'all'], environment)
    run(name, ['make', '-C', directory, 'test'], environment)

try:
    suite('zum/itest')
    suite('zgtk/test')
    with tempfile.TemporaryDirectory(prefix='zffb-final-parity-zdf-') as fixture:
        suite('zdf/itest', dict(env,
            ZDB_MODULE=str(root / 'zdb_sqlite/src/.libs/libZdbSL.so'),
            ZDB_CONNECT=str(Path(fixture) / 'data.db')))
    suite('zdash/test')
    suite('zdash/itest', dict(env,
        ZDB_MODULE=str(root / 'zdb_sqlite/src/.libs/libZdbSL.so')))
    run('interop', ['prove', 'zf/interop/zffbinterop',
        'zdb/interop/zdbmsginterop', 'ztc/interop/ztcmsginterop'])
    stage.write_text('complete\n')
    Path(prefix + '.status').write_text('0\n')
except BaseException as error:
    Path(prefix + '.status').write_text(str(getattr(error, 'returncode', 1)) + '\n')
    raise
