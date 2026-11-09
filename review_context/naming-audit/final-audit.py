import re,json,subprocess
from pathlib import Path
owners='zdbus zdf zfix zgrpc zhttp zjrpc zmcp zpb zquic zrest ztc ztls zum zws'.split()
lex=re.compile(r'(?P<comment>//[^\n]*|/\*.*?\*/)|(?P<string>(?:u8|u|U|L)?R"(?P<d>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=d)"|(?:u8|u|U|L)?"(?:\\.|[^"\\])*")|(?P<number>(?:\b[0-9]|\.[0-9])(?:[eEpP][+-]|[A-Za-z_0-9.]|\x27[A-Za-z_0-9])*)|(?P<char>(?:u8|u|U|L)?\x27(?:\\.|[^\x27\\])*\x27)|(?P<id>[A-Za-z_]\w*)',re.S)
def tokens(s,kind):return [m[0] for m in lex.finditer(s) if m.lastgroup==kind]
# named backref d is internal; group selection uses lastgroup for enclosing string.
def ids(s):return {m[0] for m in lex.finditer(s) if m.lastgroup=='id'}
plan=Path('naming_repair.md').read_text()
ledgers={m:Path('naming_'+m+'_execution.md').read_text() if Path('naming_'+m+'_execution.md').exists() else Path('naming_execution.md').read_text() for m in owners}
coverage={}
for m in owners:
 section=plan.split('### '+m+' —')[1].split('\n### ')[0]
 rows=re.findall(r'^\| `([^`]+)` \|',section,re.M)
 coverage[m]={'rows':len(rows),'missing':[r for r in rows if '`'+r+'`' not in ledgers[m]]}
files=[p for m in owners for p in Path(m).rglob('*') if p.suffix in ('.cc','.hh','.h') and p.is_file() and '.libs' not in p.parts]
allids=set();over={};newlong={};literal_changes=[]
for p in files:
 s=p.read_text(); names=ids(s);allids.update(names)
 for n in names:
  if len(n)>28:over.setdefault(n,[]).append(str(p))
 old=subprocess.run(['git','show','HEAD:'+str(p)],capture_output=True,text=True)
 if old.returncode:continue
 if old.stdout==s:continue
 for n in names-ids(old.stdout):
  if len(n)>28:newlong.setdefault(n,[]).append(str(p))
 a=tokens(old.stdout,'string')+tokens(old.stdout,'char')
 b=tokens(s,'string')+tokens(s,'char')
 if a!=b:literal_changes.append(str(p))
confirmed=plan.split('## Over-limit identifiers')[1].split('## Confirmed suffix')[0]
rows=re.findall(r'^\| `([^`]+)` \| `([^`]+)`',confirmed,re.M)
result={'coverage':coverage,'conditional_total':sum(x['rows'] for x in coverage.values()),'confirmed_count':len(rows),'confirmed_residuals':[(a,b) for a,b in rows if a in allids], 'confirmed_dest_missing':[b for a,b in rows if b not in allids], 'remaining_overlength':over,'new_overlength':newlong,'literal_changed_files':literal_changes,'cpp_files':len(files)}
Path(__file__).with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ['remaining_overlength']},indent=2))
print('remaining_overlength',len(over))
for n,paths in sorted(over.items()):print(n, paths[0])
import difflib
print('\nLITERAL DIFFS')
for name in literal_changes:
 p=Path(name);before=subprocess.check_output(['git','show','HEAD:'+name],text=True)
 old=tokens(before,'string')+tokens(before,'char');new=tokens(p.read_text(),'string')+tokens(p.read_text(),'char')
 delta=[line for line in difflib.unified_diff(old,new,n=0) if line.startswith(('+','-')) and not line.startswith(('+++','---'))]
 print(name,repr(delta))
headers=re.findall(r'^\| `(\w+/[^`]+\.hh)` \| `(\w+/[^`]+\.hh)` \|',plan.split('## Support-header')[1].split('The associated guard')[0],re.M)
print('\nHEADER AUDIT')
for old,new in headers:
 p=Path(new);guard=p.stem+'_HH';text=p.read_text()
 oldbase=Path(old).name
 refs=[]
 for module in owners:
  for f in Path(module).rglob('*'):
   if not f.is_file() or f.is_symlink():continue
   if f.suffix not in ['.cc','.hh','.h'] and f.name not in ['Makefile','Makefile.am','Makefile.in']:continue
   try:s=f.read_text()
   except UnicodeError:continue
   if oldbase in s:refs.append(str(f))
 print(old,'->',new,'old_exists',Path(old).exists(),'guards',('#ifndef '+guard in text and '#define '+guard in text),'old_source_build_refs',refs)
 oldtext=subprocess.check_output(['git','show','HEAD:'+old],text=True)
 print('  renamed header literals unchanged',tokens(oldtext,'string')+tokens(oldtext,'char')==tokens(text,'string')+tokens(text,'char'))
print('\nEXCLUDED CHANGES')
changed=subprocess.check_output(['git','diff','HEAD','--name-only'],text=True).splitlines()
print([p for p in changed if Path(p).suffix in ['.cc','.hh','.h'] and p.split('/')[0] not in owners])
