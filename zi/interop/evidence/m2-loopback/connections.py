import os
from pathlib import Path
import subprocess
root=Path('/home/count0/src/z');out=Path('/tmp/zffb-direct')
for tag,mode,rate,clients,subs,agents in [
    ('direct32-131072-repeat','direct',131072,32,32,2),
    ('direct2-131072','direct',131072,2,512,2),
    ('direct2-262144','direct',262144,2,512,2),
    ('hub32-131072','hub',131072,32,32,32)]:
    env=dict(os.environ,ZI_LOGDIR=str(out),PYTHONPATH='zi/itest:zum/itest',
             ZTC_LOAD='1',ZTC_LOAD_AGENTS=str(agents),ZTC_LOAD_CLIENTS=str(clients),
             ZTC_LOAD_SUBS=str(subs),ZTC_LOAD_PUBLISHERS='256',ZTC_LOAD_ROUNDS='3',
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
