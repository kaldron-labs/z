# Fix 1: QIR Migration Advertisement

## Problem

The QIR image currently runs rebinding tests while zquic advertises
`disable_active_migration: true`.

That happens because QIR's direct QUIC paths use default `Zquic` migration
settings. `Zquic::EngineParams` defaults to `MigrationMode::Passive`, and
`ZquicLink` maps every non-active mode to the transport parameter:

```c++
m_transportParams.disableActiveMigration =
  app->migrationMode() != MigrationMode::Active;
```

`quiche`, `quic-go`, and `msquic` respect that advertised policy more strictly
than `ngtcp2`. In the failed `rebind-port` and `rebind-addr` cases they keep
using the old client path or fail to sustain migration progress, so the
simulator drops packets to stale 4-tuples as `unknown binding`.

The first fix is configuration, not recovery logic: when QIR runs rebinding
cases, zquic must explicitly advertise active migration support and reserve
enough peer CIDs.

## Scope

Change only QIR endpoint configuration and its automated coverage.

Do not change:

- QUIC migration state-machine behavior.
- PTO, loss recovery, or transparent NAT rebinding heuristics.
- General `Zquic::EngineParams` defaults.
- Public defaults for `zhttp` or `zhttpd` outside the QIR runner.

## Implementation

1. Add QIR-local constants in `zhttp/bench/ZhttpQIR.cc`:

```c++
enum {
  QIRMigrationCIDReserve = 4
};
```

Use `4` because QIR rebinding tests use repeated path changes, and the stricter
reference stacks have `active_connection_id_limit` values around `4`. This
keeps the reserve explicit without changing library defaults.

2. Add a QIR-local helper for direct `Zquic` parameter builders:

```c++
template <typename Params>
static Params qirMigrationParams_(Params params)
{
  return ZuMv(params)
    .migrationMode(Zquic::MigrationMode::Active)
    .migrationCIDReserve(QIRMigrationCIDReserve);
}
```

Keep this helper in the QIR file, not in `zquic`, because this is an interop
runner policy choice.

3. Apply the helper to every direct QIR `Zquic` init path:

- `runHQServer_`
- `runHQClient_`
- `runH3Client_`

The resulting init calls should still keep the existing ALPN, key log, qlog,
cert, CA, and H3 limit settings. Only the migration policy/reserve should be
added.

4. Update `runH3Server_` so the embedded `zhttpd` argv includes:

```text
--quic-migration active
--quic-migration-cid-reserve 4
```

Do this in both argv arrays, with and without `--key-log`. Update the
corresponding `ArgcWithKeyLog` and `ArgcNoKeyLog` constants in the same change.

5. Add focused unit/smoke coverage in `zhttp/bench/ZhttpQIRTest.cc` or
`ZhttpQIRSmokeTest.cc`:

- Verify the QIR helper/config path selects `MigrationMode::Active`.
- Verify the CID reserve is `QIRMigrationCIDReserve`.
- Verify the generated `zhttpd` argv path includes
  `--quic-migration active` and `--quic-migration-cid-reserve 4`.

Avoid shell-only assertions for this. Prefer a small C++ helper that builds the
argv vector/array so tests can inspect it without launching `zhttpd`.

6. Extend `zhttp/bench/qir-script-test.sh` to keep rebinding in the automated
script coverage:

```sh
scripts/qir-run-local --dry-run \
  --tests rebind-port,rebind-addr \
  --peers quiche,quic-go,msquic
```

The dry run must prove the intended matrix is requested. The live interop run
remains a developer/CI acceptance step because it depends on Docker images and
the external interop runner.

## Verification

Run local build/test verification:

```sh
make -C zhttp/bench test
make -C zhttp test
make -j8
```

Rebuild the image:

```sh
scripts/qir-build-image \
  --root zhttp/bench/qir-root \
  --tag zhttp-qir:local \
  --allow-dirty
```

Run the focused rebinding matrix:

```sh
scripts/qir-run-local \
  --runner /tmp/quic-interop-runner-zhttp \
  --image zhttp-qir:local \
  --peers quiche,quic-go,msquic \
  --tests rebind-port,rebind-addr \
  --build-type release \
  --out zhttp/bench/qir-results/fix1-rebind-refs \
  --python /tmp/qir-venv-zhttp/bin/python
```

Inspect peer logs/qlogs and confirm that zquic no longer advertises
`disable_active_migration: true` in QIR runs. The expected transport parameter
state is either no `disable_active_migration` parameter or an equivalent false
value, depending on each peer's log formatting.

## Acceptance Criteria

- QIR zquic endpoints advertise active migration in all HQ and H3 paths.
- QIR zquic endpoints reserve at least four migration CIDs.
- `rebind-port` and `rebind-addr` pass against `quiche`, `quic-go`, and
  `msquic` in both zquic-client and zquic-server roles, unless a remaining
  failure has a new packet/qlog diagnosis unrelated to advertised migration
  policy.
- Existing `handshake`, `transfer`, and `http3` interop cases still pass.
- No `zquic` library default is changed to satisfy a QIR-only requirement.
