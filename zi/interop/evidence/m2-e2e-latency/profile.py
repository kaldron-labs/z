import os
import subprocess
import sys
from pathlib import Path
root = Path('/home/count0/src/z')
sys.path[:0] = [str(root / p) for p in ('ztc/itest', 'zi/itest', 'zum/itest')]
import ztchubtest
original = subprocess.Popen

def launch(args, *a, **kw):
    if isinstance(args, list) and args and str(args[0]).endswith('/ztchubloadtest'):
        args = [str(root/'libtool'), 'exec', 'gprofng', 'collect', 'app',
                '-p', 'on', '-F', 'off', '-a', 'off', '-o',
                '/tmp/zffb-latency/peer.er', *args]
    return original(args, *a, **kw)

subprocess.Popen = launch
os.environ.update(ZDB_MODULE=str(root/'zdb_sqlite/src/.libs/libZdbSL.so'),
    ZDB_CONNECT='/tmp/zffb-latency/profile.db', ZI_LOGDIR='/tmp/zffb-latency',
    ZTC_LOAD='1', ZTC_LOAD_AGENTS='2', ZTC_LOAD_CLIENTS='32', ZTC_LOAD_SUBS='32',
    ZTC_LOAD_PUBLISHERS='256', ZTC_LOAD_ROUNDS='3', ZTC_LOAD_MESSAGES_PS='262144')
ztchubtest.main()
