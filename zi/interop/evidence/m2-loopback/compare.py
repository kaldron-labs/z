import os
from pathlib import Path
import subprocess
import time
root=Path('/home/count0/src/z')
out=Path('/tmp/zffb-direct')
cases=[(32768,'direct',1),(32768,'hub',1),(131072,'hub',1),(131072,'direct',1),
       (262144,'direct',1),(262144,'hub',1),(262144,'hub',2),(262144,'direct',2)]
for rate,mode,repeat in cases:
    tag=f'{mode}-{rate}-{repeat}'
    env=dict(os.environ,ZI_LOGDIR=str(out),PYTHONPATH='zi/itest:zum/itest',
             ZTC_LOAD='1',ZTC_LOAD_AGENTS='2',ZTC_LOAD_CLIENTS='32',
             ZTC_LOAD_SUBS='32',ZTC_LOAD_PUBLISHERS='256',ZTC_LOAD_ROUNDS='3',
             ZTC_LOAD_MESSAGES_PS=str(rate),ZTC_LOAD_TURN='64',
             ZDB_MODULE=str(root/'zdb_sqlite/src/.libs/libZdbSL.so'),
             ZDB_CONNECT=str(out/(tag+'.db')))
    script='ztcdirecttest.py' if mode=='direct' else 'ztchubtest.py'
    with (out/(tag+'.log')).open('w') as log:
        run=subprocess.run(['python3',str(root/'ztc/itest'/script)],env=env,
                           stdout=log,stderr=subprocess.STDOUT)
    print(tag,'exit',run.returncode,flush=True)
    for line in (out/(tag+'.log')).read_text().splitlines():
        if line.startswith(('# load','# latency')): print(line,flush=True)
