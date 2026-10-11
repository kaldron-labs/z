from pathlib import Path
import subprocess
out=Path('/tmp/zffb-control-release-measurements')
out.mkdir()
with (out/'commands.txt').open('w') as commands:
 for mode in ('save','wide','ops','wideops','vectors','write','widewrite'):
  for size in (0,64,1024,65536):
   count=1000000 if size<=64 else 100000 if size==1024 else 10000
   if mode=='vectors': count=min(count,100000 if size<=64 else 10000 if size==1024 else 1000)
   args=['taskset','-c','6','/tmp/zifb-release-bench',str(count),str(size),'11']+([] if mode=='save' else [mode])
   commands.write(' '.join(args)+'\n'); commands.flush()
   with (out/f'{mode}-{size}.csv').open('w') as log:
    subprocess.run(args,stdout=log,check=True)
   print(mode,size,'complete',flush=True)
 for name,binary in (('replication','zdb/interop/zdbmsginterop'),('telemetry','ztc/interop/ztcmsginterop')):
  args=['taskset','-c','6',binary,'--bench','100000','11']
  commands.write(' '.join(args)+'\n'); commands.flush()
  with (out/f'{name}.csv').open('w') as log: subprocess.run(args,stdout=log,check=True)
  print(name,'complete',flush=True)
