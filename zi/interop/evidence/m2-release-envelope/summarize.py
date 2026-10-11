import csv, glob, statistics
from collections import defaultdict
from pathlib import Path
prefix='/tmp/zffb-envelope-repair-perf-'
print('| Case | Operation | Canonical ns | Native ns | Native/canonical |')
print('| --- | --- | ---: | ---: | ---: |')
for filename in sorted(glob.glob(prefix+'*.csv')):
 groups=defaultdict(lambda: defaultdict(list))
 with open(filename) as source:
  for row in csv.DictReader(line for line in source if not line.startswith('#')):
   if not row.get('seconds'): continue
   key='/'.join(row.get(k,'') for k in ('operation','mode')).strip('/')
   groups[key][row['codec']].append(float(row['seconds'])*1e9/int(row['count']))
 for key,codecs in groups.items():
  if 'canonical' not in codecs or 'native' not in codecs: continue
  canonical=statistics.median(codecs['canonical']);native=statistics.median(codecs['native'])
  print(f'| {Path(filename).stem.removeprefix(Path(prefix).name)} | {key} | {canonical:.3f} | {native:.3f} | {native/canonical:.3f} |')
