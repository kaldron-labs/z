import hashlib,json,os,shlex,subprocess
from pathlib import Path
root=Path('/home/count0/src/z'); prefix='/tmp/zffb-final-parity-perf'; stage=Path(prefix+'.stage')
def run(name,command,environment=None):
 stage.write_text(name+'\n')
 with open(prefix+'-'+name+'.csv','w') as output:subprocess.run(command,cwd=root,env=environment,stdout=output,stderr=subprocess.STDOUT,check=True)
try:
 if Path('/tmp/zffb-final-parity-memory.status').read_text().strip()!='0':raise RuntimeError('memory gate did not succeed')
 for name,digest in json.loads(Path('/tmp/zffb-final-parity-sources.json').read_text()).items():
  if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('source changed: '+name)
 subprocess.run(['make','-C','zi/interop','-q','-o','fb-deps','zifbiobench'],cwd=root,check=True)
 assert hashlib.sha256(Path('/tmp/zffb-reflection-25.12.19.cpp').read_bytes()).hexdigest()=='c4b7a46d0d65d8502ff9c1dd8d999b17722f8afeeeec3b314dec2d9c79fdabbf'
 link=Path('/tmp/zffb-typed-single-link.log').read_text().splitlines()[0]
 if '-O3' not in link or '-DNDEBUG' not in link or '/tmp/zffb-reflection-25.12.19.o' not in link:raise RuntimeError('wrong release comparator')
 with open(prefix+'-link.log','w') as output:
  output.write(link+'\n');output.flush();subprocess.run(shlex.split(link),cwd=root/'zi/interop',stdout=output,stderr=subprocess.STDOUT,check=True)
 env=dict(os.environ);env.pop('LD_PRELOAD',None);env.pop('ZIFB_BENCH_POOL',None)
 for size,count in ((0,1000000),(64,1000000),(1024,100000),(65536,10000)):
  for mode in ('save','wide','ops','wideops','vectors','write','widewrite'):
   command=['./libtool','exec','taskset','-c','6','/tmp/zifb-audit-release-bench',str(count),str(size),'7']
   if mode!='save':command.append(mode)
   run(mode+'-'+str(size),command,env)
 for module,binary in [('zdb','zdbmsginterop'),('ztc','ztcmsginterop')]:
  run(module+'-messages',['./libtool','exec','taskset','-c','6',module+'/interop/'+binary,'--bench','1000000','7'],env)
 for mode in ('save','wide'):
  command=['./libtool','exec','taskset','-c','6','/tmp/zifb-audit-release-bench','1000000','64','7']
  if mode!='save':command.append(mode)
  run('pool-'+mode+'-64',command,dict(env,ZIFB_BENCH_POOL='4'))
 stage.write_text('complete\n');Path(prefix+'.status').write_text('0\n')
except BaseException as error:
 Path(prefix+'.status').write_text(str(getattr(error,'returncode',1))+'\n');raise
