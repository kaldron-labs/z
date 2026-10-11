import hashlib,json,os,select,subprocess
from pathlib import Path
root=Path('/home/count0/src/z')
prefix='/tmp/zffb-envelope-repair-memory'
stage=Path(prefix+'.stage')
Path(prefix+'.pid').write_text(str(os.getpid())+'\n')
try:
    if Path('/tmp/zffb-envelope-repair-check.status').read_text().strip()!='0':
        raise RuntimeError('functional acceptance did not succeed')
    snapshot=json.loads(Path('/tmp/zffb-envelope-repair-sources.json').read_text())
    for name,digest in snapshot.items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:
            raise RuntimeError('source changed: '+name)
    programs=['zf/test/ZfFB'+name+'Test' for name in
        ('Wire','Builder','Load','Value','View','Union','Stream','Schema')]
    programs+=['zf/test/ZfSkipTest','zi/test/ZiFBTest','zdb/test/ZdbSagaTest',
        'zdb_pq/test/ZdbPQTest','zdb_sqlite/test/ZdbSLTest',
        'ztc/test/ZtcHubRxTest','zum/test/ZumTest']
    for program in programs:
        directory,target=program.rsplit('/',1)
        subprocess.run(['make','-C',directory,'-q',target],cwd=root,check=True)
        stage.write_text(program+'\n')
        with open(prefix+'-'+target+'.tap','w') as output:
            subprocess.run(['./libtool','exec','valgrind','--error-exitcode=99',
                '--leak-check=full','--show-leak-kinds=all',
                '--errors-for-leak-kinds=definite,indirect,possible',
                '--log-file='+prefix+'-'+target+'.valgrind',program],
                cwd=root,stdout=output,stderr=subprocess.STDOUT,check=True)
    stage.write_text('complete\n'); Path(prefix+'.status').write_text('0\n')
except BaseException as error:
    Path(prefix+'.status').write_text(str(getattr(error,'returncode',1))+'\n')
    raise
