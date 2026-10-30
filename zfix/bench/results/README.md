# Measurement evidence

The current archive is [2026-10-28.tar.xz](2026-10-28.tar.xz). Its root report,
`build-manifest.json`, codec rows and `process/` tree describe the fresh post-Link
run with the shared codec input timestamp `20261028-12:34:56.123456789`.
All 288 process cases and 18 codec rows pass the strict retained-data audit.
The native and QuickFIX scalar/group/binary frames have matching lengths of
170/910/4281 bytes. The generated measurement report and raw files are retained
without rewriting them; the repository report adds context and code-size tables.

The dated archives retain process manifests, individual case results, peer JSON
and stderr, allocation/copy observations, codec result rows and their executable
provenance. Public fixture certificates are included; generated private keys and
binaries are excluded. Original absolute paths and commands remain as provenance.

Read [the measured baseline](../RESULTS.md) before comparing results. The recorded
build uses optimized, instrumented Zfix with existing debug/ASan dependencies;
it does not establish ordinary release or production throughput. Verification
is scoped to Linux x64; ARM and Windows are outside this task.

Unpack an archive and run `sha256sum -c SHA256SUMS` inside its dated directory.
Its `process/manifest.json` records the four peers and their actual loaded
libraries. `build-manifest.json` additionally records both codec executables,
exact codec commands and sanitizer settings. Individual distributions and raw
resources remain separate by session, repeat and diagnostic mode. Diagnostics
are excluded from timing summaries.

The archive separates the three datasets:

- Root `process/` and codec files: the current corrected measurement run.
- `prior/pre-Link/`: the original historical snapshot and its original audit.
- `prior/pre-codec-input-fix/`: the rejected post-Link snapshot, including its
  failed verification. Separate wall-clock samples and different fractional
  precision produced unequal codec frame lengths in that run.

The common fixed timestamp repairs fixture input only. Every process and codec
case was rerun; earlier samples are neither substituted nor combined. The Link
source comparison alone was not treated as performance identity. See
`PROVENANCE.md` for these boundaries.

`process/verification.json` records the current raw-data audit, including all
155 source hashes, six measured executables and 36 loaded libraries.
`verification/current/index.json` pins separate correctness evidence under
`compilers/`, `runtime/`, `installed/`, `examples/`, `memory/`, `layout/` and
`measurement/`. For example, current installed results and header/library checks
are `verification/current/installed/summary.json` and
`verification/current/installed/link-install-verification.json`; the two-compiler
gate and targeted codec-input repair are recorded under
`verification/current/compilers/`. Earlier delivery checks remain under
`verification/pre-layout/`. These checks do not add timing samples.

The packaged `measurement-verifier.py` independently checks retained case,
resource, heap, diagnostic, codec and cross-manifest provenance records. After
unpacking, it can recheck the snapshot without requiring the original binaries:

```sh
python3 measurement-verifier.py . --expected-source-count 155 \
  --output ../verification-recheck.json
```

Historical mode reports unavailable or changed current paths separately. Use
`--require-current-hashes` only when the original executable, dependency and
source paths are available and should still match the measured snapshot.

To regenerate the combined report after unpacking, invoke `zfix/bench/results.py`
from the repository with the extracted `process` directory and all six extracted
codec JSONL files as `--codec` arguments. Original commands and configure site
are included; adjust checkout, dependency and output paths for another machine.
Preserve compiler flags, dependency ABI/instrumentation, dictionary pins,
workload and heap settings when making a comparison. Regression envelopes are
host-specific observations, not portable thresholds or service promises.

Published archive SHA256 (`2026-10-28.tar.xz`):
`bb066944cf075af408fed5210c7199182b306e42bf7152adfb23092e9b4a8587`.
