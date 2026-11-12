# JSON fixture inventory

The source tree stores maintained fixture and tool configuration data in TOML.
`scripts/toml-to-json` renders the JSON required by external tools into generated
output or an owned test directory. It uses Python's `tomllib`, or Tomli on older
Python versions.

| Former JSON input | Classification | Checked-in replacement and consumer |
| --- | --- | --- |
| `zws/interop/autobahn/fuzzingclient.json` | Maintained Autobahn configuration, not captured test output | `fuzzingclient.toml`; `run-server.sh` renders the JSON in its owned report directory |
| `zws/interop/autobahn/fuzzingserver.json` | Maintained Autobahn configuration, not captured test output | `fuzzingserver.toml`; `run-client.sh` renders the JSON in its owned report directory |
| `zgrpc/interop/dotnet/packages.lock.json` | NuGet dependency versions and content hashes | `packages.lock.toml`; the peer Makefile renders `obj/packages.lock.json` before locked restore |
| `zhttp/interop/testdata/qir-results/fixture/zquic-server_ngtcp2-client/results.json` | Synthetic input used by QIR report assertions, not collected runner output | `results.toml`; `qir-script-test.sh` renders JSON in its owned temporary fixture copy |

The audit removed root naming reports, review snapshots, execution-evidence JSON,
log reports, recorded benchmark JSON/JSONL and generated .NET JSON metadata.
These are run residue, not maintained fixtures. Residue tracked in Git was also
removed from history; generated .NET files had never been tracked.

New `.md`, `.json`, `.jsonl` and `.ndjson` files are ignored outside `docs/`.
Existing tracked documentation remains tracked. Tests may create JSON when the
protocol or tool requires it; it belongs in generated output or owned test
residue, never among the maintained inputs.
