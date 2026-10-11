import os
import select
import subprocess
import tempfile
from pathlib import Path

root = Path('/home/count0/src/z')
prefix = '/tmp/zffb-final-parity-check'
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
    if Path('/tmp/zffb-final-parity-refresh.status').read_text().strip() != '0':
        raise RuntimeError('refresh did not succeed')
    run('dependencies', ['python3', '/tmp/zffb-final-parity-deps.py'])
    run('dependent-size', ['python3', '/tmp/zffb-final-parity-size.py'])
    run('zu-current', ['make', '-C', 'zu/test', '-q', 'ZuTLTest', 'ZuStructTest'])
    run('zu-tests', ['prove', 'zu/test/ZuTLTest', 'zu/test/ZuStructTest'])
    run('zm-current', ['make', '-C', 'zm/test', '-q', 'ZmSchedTest'])
    run('zm-scheduler', ['prove', 'zm/test/ZmSchedTest'])
    for module in ('zf', 'zdb', 'zdb_pq', 'zdb_sqlite', 'ztc', 'zum', 'zdf'):
        suite(module + '/test')
    run('zi-current', ['make', '-C', 'zi/test', '-q', 'ZiFBTest'])
    run('zi-test', ['prove', 'zi/test/ZiFBTest'])
    suite('zdb/itest')
    run('zdb-example-current', ['make', '-C', 'zdb/example', '-q', 'zdbsaga'])
    run('zdb-saga-example', ['./libtool', 'exec', 'zdb/example/zdbsaga'])
    suite('zdb_sqlite/itest')
    pg = Path('/tmp/zffb-audit-pg.btXXED')
    ctl = ['/home/count0/postgres/bin/pg_ctl', '-D', str(pg / 'data')]
    pg_env = dict(env, LD_LIBRARY_PATH=str(root / 'zu/src/.libs'))
    run('pg-start', ctl + ['-l', str(pg / 'server.log'), '-w', 'start',
        '-o', "-k /tmp/zffb-audit-pg.btXXED/socket -p 55439 -h ''"], pg_env)
    try:
        suite('zdb_pq/itest', dict(env,
            ZDB_MODULE=str(root / 'zdb_pq/src/.libs/libZdbPQ.so'),
            ZDB_CONNECT='host=/tmp/zffb-audit-pg.btXXED/socket port=55439 dbname=zffb_audit'))
    finally:
        run('pg-stop', ctl + ['-w', 'stop', '-m', 'fast'], pg_env)
    suite('ztc/itest', dict(env,
        ZDB_MODULE=str(root / 'zdb_sqlite/src/.libs/libZdbSL.so')))
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
