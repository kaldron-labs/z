# Zum post-fix audit

This audit records the current worktree after executing `zumfix3.md`.

## Lifecycle and ownership

Resolved: `MaintenanceRun_` in `zum/src/zumd_daemon.cc` owns the three database cleanup continuations through `ZmRef`. Each database completion posts a value-only continuation to the daemon request scheduler before it changes run state. `Daemon::cleanupStop_()` cancels timer ingress, marks the run stopping on that scheduler, and `Daemon::stop()` waits for `cleanupDone_()` before releasing dependent state. `start()`, `stop()`, and `final()` reject duplicate lifecycle transitions. Scheduler-affinity assertions cover run start/stop/completion and timer manipulation.

Remaining evidence gap: there is no isolated test that forces `stop()` during each of the grant, refresh, and session database operations. The full daemon integration suite exercises ordinary teardown; it does not deterministically hold each callback boundary.

## Cleanup selection and sagas

Resolved: `ExpiredCleanup_` and `AppCleanup_` use bounded pages and a continuation key, skip owned rows during selection, and preserve compact before-images. `GrantCleanup`, `SessionCleanup`, `RefreshCleanup`, and `AppCleanup` treat a missing selected row as an idempotent successful delete and reject changed/owned rows before commit. The `findDel`/`findUpd` audit in `zumd_app_db.hh`, `zumd_catalog.hh`, `zumd_sagas.hh`, and `zumd_revoke.hh` handles null rows before data access.

Resolved: compact maintenance types use new saga identifiers while legacy decoders remain registered as terminal failures, so interrupted old payloads cannot be misdecoded during restart.

Remaining evidence gap: direct tests for an owned prefix followed by later unowned rows and a multi-page app cleanup batch are still absent. `zumrestarttest` covers restart recovery but is not a replacement for those focused selectors.

## Storage and catalog bounds

Resolved: maintenance payloads use typed `ZtArray` vectors with named heaps: `GrantDeleteVec`, `SessionDeleteVec`, `RefreshDeleteVec`, `EvidenceDeleteVec`, and `ConsentDeleteVec`. Their ADL transformers use `ZfbTransform::UDTArray`. `ZumTest` serializes and loads each type. `CatalogPublish_` applies named structural limits and builds `CatalogNameHash` lookups before reference validation.

Remaining evidence gap: no committed benchmark records peak payload bytes, allocation count, catalog worst-case CPU, or scheduler turn duration. The implementation is bounded but those measurements have not been captured.

## Secret handling and diagnostics

Resolved: `OIDCState` invokes `clearSecret_()` on admission, discovery, random-generation, duplicate-state, request completion, stop, and final paths. `clearRequest_()` wipes verifier, ID token, access token, and client secret before release. Maintenance results include selected, deleted, skipped, conflicted, failed, and rescheduled fields; owned-row selection increments skipped and a grant conflict increments conflicted. Logs are structured and retry uses bounded exponential backoff.

Resolved: the Valgrind run exposed a test UAF in recovery-capability log verification. `ZumTest` now retains a deliberate assertion copy and scrubs it after use. The corrected `libtool exec valgrind` run reports zero errors and no definite, indirect, or possible leaks.

## Current verification

- `make -C zum -j3`
- `make -C zum test`
- `cd zum/test && ../../libtool exec ./ZumTest`
- `cd zum/test && ../../libtool exec valgrind --leak-check=full --errors-for-leak-kinds=definite --error-exitcode=99 ./ZumTest`

All completed successfully in the current configured debug build. Address sanitizer was not run because `zumfix3.md` explicitly retains the current build configuration and excludes other build configurations.
