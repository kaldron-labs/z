import json,re,subprocess
from pathlib import Path
root=Path('/home/count0/src/z');findings=[];libraries={};depfiles=0
modules=[p for p in root.iterdir() if p.is_dir() and (p/'src/Makefile').exists()]
for module in sorted(modules):
 for candidate in sorted((module/'src/.libs').glob('lib*.so')):
  path=candidate.resolve()
  if str(path) in libraries: continue
  output=subprocess.check_output(['readelf','-d',str(path)],text=True)
  needed=re.findall(r'\(NEEDED\).*?\[(.*?)\]',output)
  libraries[str(path)]=needed
  for name in needed:
   if re.search(r'flatbuffers|Zfb',name,re.I):findings.append(str(path)+': '+name)
 for directory in ('src','test','util','itest','example','bench'):
  owner=module/directory
  if not (owner/'Makefile').exists() or not (owner/'Makefile.am').exists(): continue
  recipe='zffb-audit:;@echo $(sort $(foreach v,$(filter %_OBJECTS,$(.VARIABLES)),$($(v))))'
  output=subprocess.check_output(['make','--no-print-directory','-s','-n','-C',str(owner),'--eval='+recipe,'zffb-audit'],text=True)
  if not output.startswith('echo '):raise RuntimeError('unexpected object expansion: '+str(owner))
  for name in output[5:].split():
   obj=(owner/name).resolve()
   suffix='.Plo' if obj.suffix=='.lo' else '.Po'
   path=obj.parent/'.deps'/(obj.stem+suffix)
   depfiles+=1
   if not path.exists():
    findings.append(str(path)+': missing configured-object depfile');continue
   dependencies=path.read_text(errors='replace')
   if module.name=='zf' and '/zi/src/' in dependencies:findings.append(str(path.relative_to(root))+': forbidden Zi dependency')
   if re.search(r'(?:/flatbuffers/|/zfb/|Zfb[^/\s]*\.hh)',dependencies):
    findings.append(str(path.relative_to(root))+': canonical or deleted dependency')
by_name={Path(name).name:deps for name,deps in libraries.items()}
visited=set()
def walk(name):
 if name in visited:return
 visited.add(name)
 if re.search(r'^libZi(?:-|\.|$)',name):findings.append('Zf transitive runtime dependency: '+name)
 for child in by_name.get(name,[]):walk(child)
walk((root/'zf/src/.libs/libZf.so').resolve().name)
headers=sorted(p.name for p in (root/'zf/src').glob('ZfFB*.hh'))
if headers!=['ZfFB.hh','ZfFBReflect.hh']:findings.append('unexpected codec headers: '+repr(headers))
for p in (root/'zf/src').glob('ZfFB*'):
 if p.suffix in ('.hh','.cc') and re.search(r'#include[^\n]*(?:/Zi|/flatbuffers/)',p.read_text()):findings.append(str(p)+': forbidden lower-layer dependency')
report=dict(modules=[p.name for p in sorted(modules)],libraries=libraries,depfiles=depfiles,headers=headers,findings=findings)
print(json.dumps(report,indent=2));raise SystemExit(bool(findings))
