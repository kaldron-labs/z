import json,subprocess
from pathlib import Path
baseline=json.loads(Path('/tmp/zffb-final-parity-dependent-size-before.json').read_text())
print('| Library | Previous text bytes | Current text bytes | Change |')
print('| --- | ---: | ---: | ---: |')
for name,old in baseline.items():
 p=Path(name);fields=subprocess.check_output(['size',str(p)],text=True).splitlines()[-1].split()
 text=int(fields[0]);print(f'| {p.name} | {old["text"]} | {text} | {text-old["text"]:+} |')
 if p.stat().st_mtime==old['mtime']:print('WARNING: unrefreshed artifact '+name)
