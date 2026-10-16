# Plan: optional telemetry publishing in `zumd`

## Required behavior

Make `zumd` a telemetry publisher through `Ztc::App`, controlled by the
boolean `ztcPublish`. Publishing belongs to the process lifetime, independent
of Zdb activation, IAM readiness, bootstrap completion and HTTP admission.

- Default to disabled.
- Enable when `ZUMD_ZTC_PUBLISH` is exactly `1`.
- Enable with the case-sensitive, value-free CLI flag `--ztcPublish`.
- Allow `ztcPublish: true` in the node configuration.
- When enabled, initialize and start one `Ztc::App` early during startup;
  stop and finalize it late during shutdown, including error exits.
- Passive, active, deactivated and reactivated database states do not start,
  stop or recreate the publisher.
- Apply the same flag semantics to `--once` and `--rekey`: these modes may
  publish during their shorter process lifetime. Help and invalid invocations
  still exit before runtime initialization.

This document is an implementation plan; it does not change runtime behavior.

## Current implementation and constraints

[`zum/src/zumd.cc`](zum/src/zumd.cc) parses CLI options and environment inputs,
starts logging, loads the node configuration, constructs the IAM multiplex and
database, then starts the multiplex and database. Its main loop handles Zdb
activation generations and bootstrap/signing preparation. A shared `stop`
lambda handles normal exit, exceptions and the explicit `--rekey` completion
path. Some initialization currently occurs before that cleanup scope.

[`Ztc::App`](ztc/src/ZtcApp.hh) already provides `init`, synchronous `start`
and `stop`, and `final`. It owns a separate scheduler, a request reader, shared
ring transport, publisher registration and alert storage. It observes existing
framework telemetry registries, including multiplexes and databases. Reuse
those facilities; do not implement another polling loop, telemetry protocol or
database-activation observer.

The publisher uses `ZTC_RING` and `ZTC_DIR`, with existing defaults. A separate
`ztcagent` collects and forwards telemetry. `zumd` needs no hub OAuth client,
agent bearer or dependency on IAM being ready to publish locally. Successful
publisher startup is not proof that a collector has received telemetry.

## 1. Resolve configuration once

Add `Options::ztcPublish`, defaulting to false, and its `ZfCLI` boolean metadata
using `CLI::Long<"ztcPublish">`. Extend help text with the flag and environment
variable. Read the environment using an exact string comparison, not `atoi`
or a generic truthiness conversion.

Add a small typed node configuration view for the root `ztcPublish` boolean.
Resolve the effective value once after parsing configuration:

```text
ztcPublish = node.ztcPublish || cli.ztcPublish ||
             (ZUMD_ZTC_PUBLISH == "1")
```

Both explicit enable mechanisms force publishing on. An unset variable,
empty value, `0`, `true`, `01`, or another value does not enable it or cancel
another enable source. There is no new disable flag or runtime toggle.
Malformed node boolean values remain configuration errors.

Load optional publisher tuning from a root `ztc` section using the existing
`Ztc::AppCf` metadata. Only parse publisher tuning and initialize publisher
resources when the effective flag is true. Use `Ztc::AppCf` defaults when
the section is absent; do not duplicate its scheduler or capacity defaults.
Set the default publisher ID explicitly to `zumd`, while allowing `ztc.id`
to select a unique instance ID. Retain the framework's version/role defaults
unless the application has an authoritative version value to supply.

Example intended configuration:

```text
ztcPublish: true,
ztc: {
  id: "zumd-local",
  alertPrefix: "/path/to/local-state/zumd-alerts"
},
mx: { ... },
zdb: { ... }
```

Document that publisher IDs must be unique among local publishers sharing the
registry/ring namespace. Alert storage must use an appropriate writable path;
preserve `Ztc::App`'s existing degraded-storage behavior rather than silently
changing its failure policy.

## 2. Integrate the process lifecycle

Keep the optional publisher stack-owned, using an established in-place optional
storage facility if construction must be conditional. Avoid a new heap-owned
service wrapper. All synchronous lifecycle calls run on the main thread,
never an owned publisher scheduler thread.

The intended order is:

```text
parse and validate invocation
start logging; load and resolve node configuration
if enabled: construct publisher, init(AppCf), start()
construct/init IAM multiplex, database and daemon resources
start IAM multiplex and database; start HTTP when applicable
run existing activation/bootstrap/maintenance logic
drain IAM requests; stop HTTP/OIDC and database work
quiesce telemetry sampling while IAM telemetry owners are still usable
finalize publisher watchers and publisher resources
finish destroying IAM telemetry owners; stop logging; exit
```

Start the publisher before `db->start()` and outside the activation loop so
that it is available while the node waits for activation or initial enrollment.
Do not add publishing lifecycle calls to `dbUp`, `dbDown`, `daemon.prepare`,
or any readiness transition. Register observation through `Ztc::App`'s existing
registry watches; database and multiplex objects created afterward should be
discovered through those watches.

Extend cleanup coverage to include failures during IAM object construction,
request initialization and schema registration, not just the existing inner
startup/run block. Use a scope guard or a concise process owner with explicit
initialization state, following local framework patterns. Ensure resources
referenced by a cleanup guard outlive that guard.

During implementation, verify the precise stop order against the registry
watchers and pending snapshot callbacks. Drain service work while publishing
is still running, but stop/drain publisher sampling before an observed IAM
scheduler can no longer service a snapshot callback. Call publisher `final()`
while referenced multiplex/database objects still exist so watches and raw
back-pointers are removed safely. Then finish IAM multiplex/database teardown.
This late process teardown boundary must not move into Zdb deactivation.

Track successful initialization and startup as needed for deterministic cleanup.
On enabled publisher `init()` or `start()` failure, log a clear error and fail
startup after cleanup; do not silently run with requested publishing disabled.
`Ztc::App::start()` already cleans up a failed synchronous start, and `final()`
also invokes `stop()`; account for their idempotence rather than assuming each
method is internally called only once. Treat a failed explicit stop as an
error without skipping remaining cleanup.

Preserve existing database drain/error reporting, especially `--rekey`'s
durable-completion check. SIGINT/SIGTERM, normal exit, `--once`, `--rekey` and
exceptions all reach the same publisher cleanup path.

The initial change need not attach `alertSink()` to `ZiLog`: publishing
framework telemetry does not automatically imply forwarding log messages.
Keep existing logging intact. If alert forwarding is included later, compose
it with the existing sink and specify its drain/detach lifetime explicitly.

## 3. Fix build dependencies before linking the daemon

Add the direct `ZtcApp.hh` include and publisher library linkage to the
`zumd` executable, without making `libZum` itself depend on `libZtcApp`.

The current build cannot support this through a single `LDADD` addition:

- Top-level traversal builds `zum` before `ztc`.
- `libZtcApp` links `libZtc`.
- `libZtc` currently lists `libZum` in its dependencies, and the `ztc` agent,
  hub and dependent tests also need Zum.

Audit library-level dependencies separately from executable dependencies.
`libZtc` currently contains `ZtcLib.cc` and `ZtcFB.cc`; confirm whether its
`libZum` linkage is unnecessary and remove it if so. Keep actual Zum
dependencies on the agent/hub targets. Do not merely reverse the complete
module traversal: the agent/hub still need Zum built first.

Establish explicit build stages with this dependency order:

```text
foundation/Zdb libraries
Zum and Ztc libraries (ordered by their actual dependencies)
libZtcApp
zumd and the remaining dependent executables
dependent tests/examples/integration fixtures
```

Implement the staging in the Automake hierarchy, retaining source files under
their existing modules and using explicit library-target prerequisites or a
documented module build split. Select the smallest change that works for clean
builds, parallel builds, installation and direct module builds. Do not rely on
an old `libZtcApp.la` left in the tree or an installed copy. Document any new
directory exception if a build split introduces one.

Review `Makefile.am`, `zum/Makefile.am`, `zum/src/Makefile.am` and
`ztc/src/Makefile.am` together, including generated FlatBuffers header ordering.

## 4. Verify behavior and lifecycle

Use existing `ZuTest`/`ZuTestUtil` conventions. Put process-level verification
under `zum/itest`, reusing the persistent daemon and collector fixtures where
practical. Give each fixture isolated publisher IDs, ring/registry names,
database storage and alert paths.

Required coverage:

1. **Enable inputs:** default off, node true, environment exactly `1`, CLI
   flag, CLI with environment `0`, and non-enabling environment values.
   When disabled, assert absence of publisher registration and publisher
   threads/files rather than merely observing no received frames.
2. **Before readiness:** observe publisher registration and real application
   telemetry while initial-admin enrollment is still pending.
3. **Passive lifecycle:** keep a node passive, observe telemetry, activate it,
   deactivate it without exiting, and reactivate it. Publishing remains
   available with the same publisher lifetime throughout. Initial enrollment
   alone does not substitute for the passive-node test.
4. **Real data path:** collect application and available framework telemetry
   through the existing request/ring path. Confirm database telemetry remains
   safe across activation transitions. Do not assert only startup log text.
5. **Shutdown and failures:** terminate with an outstanding subscription;
   exercise an IAM startup failure after publisher startup and a publisher
   startup failure. Check process termination, registration removal, worker
   drain and absence of callbacks into destroyed owners. Check shutdown/EOS
   when a live collector can receive it.
6. **Maintenance:** exercise enabled `--once` and `--rekey`, preserving their
   existing results and database durability checks. Verify cleanup even when
   their publishing window is too short for periodic samples.
7. **Collector independence:** start the daemon without an agent, then attach
   one and collect telemetry. Preserve the framework's normal ring attachment
   behavior; do not make missing collection an IAM activation dependency.

Build with `make -j8` and verify a clean build with no pre-existing publisher
library artifacts. Run affected Zum and Ztc unit/integration targets after
building their default targets. Use sanitizer-enabled lifecycle coverage and
`libtool exec` for any debugger or Memcheck runs. Document the new test entry
points in `zum/itest/README.md`.

## 5. Document the finished feature

Update `zum/README.md`, daemon help and `zdash/README.md` with the exact flag,
environment variable, node configuration, publisher identity, alert path and
shared `ZTC_RING`/`ZTC_DIR` requirements. The localhost dashboard guide should
show `zumd` supplying real telemetry through `ztcagent`, while retaining the
distinction between a publisher ID and the agent's authenticated device ID.

Completion requires an opt-in publisher visible before IAM readiness and
through passive/active transitions, clean teardown on every initialized exit
path, no publisher initialization when disabled, and a working clean build
without a circular library dependency.
