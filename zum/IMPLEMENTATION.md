# Zum multi-application IAM implementation ledger

This ledger tracks Zum implementation and verification. Current domain terms
are defined in [Zum terminology](../zum_terminology.md). A milestone is complete
only when its gate has the required observable evidence.

## Current policy

Backward compatibility and data migration are non-goals. `zumd` accepts an
empty store for bootstrap or a store at the exact current schema version; it
rejects any other non-empty store without changing it. The former migration
implementation, command-line interface, sagas, schemas, and tests have been
removed. Historical entries below that describe migration implementation or
verification are superseded and retained only as a record of earlier work.

Zum integration persistence tests now select SQLite solely through
`ZDB_MODULE` and `ZDB_CONNECT`; there is no Zum-visible SQLite interface.

The current schema is 24. Audience is an immutable attribute of `App`; there
is no audience table, ID, management operation, or scope wrapper. Access tokens
use `App.audience`. Application enrollment requires `audience`, client access
selects role IDs, and consent is keyed by user/client/application. Management
has 61 contiguous operations; obsolete numeric slots are removed.

User application access is an `Assignment`, stored in `zum.assignment` and
managed through `/admin/apps/{app_id}/assignments` and the
`assignmentQuery`, `assignmentAdd`, `assignmentRoles`, `assignmentState`
operations. The CLI group remains `assign`. Schema 24 renames the former
membership table, saga identities and authority-version fields; existing
databases require reprovisioning. Historical entries retain their original
terminology and validation results.

Management REST JSON and query fields use snake_case through `JSON::ID` and
`URI::ID` metadata; C++ members, storage fields, and native configuration retain
Z naming. OAuth/OIDC and WebAuthn protocol fields retain their standard names.
The CLI keeps kebab-case options and emits the corresponding snake_case JSON.
Client grant flags use `ZtFlags`: `AuthCode`, `ClientCredentials`, and `Refresh`,
with comma-delimited CLI and JSON input/output. The numeric storage masks remain
1, 2, and 4 respectively; the wire metadata change does not change schema 24.

Snake-case/grant-flag validation on 2026-09-30 in the Linux x86-64 Clang debug
tree: top-level `make -j8` passed. After adding required-field presence checks
at the management JSON boundary, the focused `zum/src` rebuild and
`make -C zum/test test` passed (5 binaries, 35 TAP cases). CLI tests cover comma
flag parsing/canonical printing and reject numeric, pipe-delimited, obsolete,
and malformed grant names. JSON tests cover snake-case IDs and named flags.
Fresh isolated SQLite stores passed `zumhttp.py` (61/61 successful management
operations), `zumssf.py`, `zumfederation.py`, and `zumrestarttest -q` (14 cases).
The HTTP fixture rejects old camel-case query keys and missing snake-case
required body fields with HTTP400. Python fixture syntax, documented shell
command syntax, and `git diff --check` passed.

Administrator bootstrap reissue now works after the initial enrollment reaches
Ready. It mints an expiring AddCredential capability for the persisted initial
administrator, preserving the WebAuthn user handle, credentials, assignments,
and readiness. It recreates a cleaned-up grant or replaces the prior capability
or ceremony under the deterministic bootstrap key; stale links/challenges remain
invalid. Credential addition uses the existing CredentialAdd saga. The pending
bootstrap path still supports reissue, and missing output is rejected before
mutation. No schema or REST operation changes are needed.

Validation on 2026-09-30 (Linux x86-64 Clang debug): top-level `make -j8` and
all 5 Zum unit binaries / 35 TAP cases passed. The expanded `zumdvault.py` fixture
passed pending and enrolled reissue, missing-output rejection without grant
mutation, user/assignment preservation, stale-link and interrupted-ceremony
rejection, capability expiry, repeated completed registrations, and independent
login with old and added passkeys. The full `zumhttp.py` fixture passed with
61/61 successful management operations. Both used isolated disposable SQLite
stores. README shell syntax, fixture Python syntax, and diff checks passed.

Consent pages now identify the requesting client by label and ID, name the
application receiving access, and list the exact scopes from the pending
server-side authorization grant. Standard identity, administrative and offline
scopes have plain-language descriptions. Dynamic labels and scope names are
HTML-escaped. Passkey, provider SSO and upstream OIDC consent paths share this
page. The SSO callback now retains its ceremony ID before transferring the
request to authorization processing.

Validation on 2026-09-30 in the Clang debug tree: top-level `make -j8`, all
5 Zum unit binaries / 35 TAP cases, `zumfederation.py`, and `zumhttp.py` passed.
The HTTP fixture checks the exact scope set and client ID on each consent page,
plus HTML escaping of a client label containing markup. Independent fixtures
must run sequentially when they share native callback port 8081.

Manual `zdash/README.md` acceptance on 2026-09-30 used the real desktop Firefox
passkey/consent flow, KeyRing Vault and a fresh SQLite IAM database. Core CLI
login, hub enrollment/catalog publication, dashboard assignment/client access,
native dashboard login and device-client provisioning succeeded. The agent was
accepted and GTK accessibility exposed a `zumd-local` dashboard row. The README
was corrected to include the required `telRing.name`. A graceful shutdown in
dashboard/agent/hub/daemon order and restart in daemon/hub/agent/dashboard order
succeeded. Restart omitted the daemon DB-key and agent-secret environment inputs;
the dashboard resumed its saved refresh credential without a browser callback,
and the same publisher row reappeared. All four local processes were left running.

Schema-24 assignment/CLI validation on 2026-09-30 in the existing Linux
x86-64 Clang debug tree: top-level `make -j8` passed, as did
`make -C zum/test test` (5 binaries, 35 TAP cases) and `zumrestarttest -q`
(14 SQLite restart/recovery cases). Fresh disposable SQLite stores also passed
`zumhttp.py` and `zumssf.py`, with `PYTHONPATH=zi/itest`, `ZDB_MODULE` pointing
to the built SQLite adapter, and a separate `ZDB_CONNECT` for each fixture.
The HTTP fixture observed success for all 61 management operations and completed
schema rejection and rekey checks. The SSF fixture checked lease expiry and
durable refresh-family delivery across restart. CLI tests include hierarchical
commands, positional user-name filters, full-width string-encoded UInt64 IDs,
and rejection of overflow and malformed arguments.

Schema-20 validation on 2026-09-15 (Linux x86-64, Clang 22.1.8, `-O3 -g`):
`make -C zum -j2 test` passed all 5 unit binaries (34 TAP cases), all 7
integration fixtures (30 TAP cases), and OpenSSL JWT interoperability.
The Ztc rebuild, 7 unit binaries and 2 C++ integration binaries also passed.
Its dependent `ztchubtest.py` completes enrollment, catalog publication, token
issuance and request forwarding, but still disconnects before the first front-end
telemetry message after the rebuild; the full Ztc test target therefore fails.

Resource scopes are derived at request time from
active application roles: a scope name resolves to exactly one same-named role,
and that role supplies the action set. There is no persisted scope record,
scope ID, scope administration endpoint, catalog scope list, or scope saga.
`ClientAccess`, consent, grants and code families persist role IDs where they
need authorization snapshots. Provider configuration still persists upstream
OIDC scope strings, and provider role mappings still map upstream role values
to local application role IDs.

Clang-debug acceptance on 2026-09-12: the incremental `zdb_sqlite` and `zum`
module builds passed with `-j3`. The SQLite adapter integration target passed
all 7 files and 52 TAP cases. The Zum unit target passed all 4 files and 29 TAP
cases. The complete Zum integration target passed `zumrestarttest` (14 cases),
`zumhttptest` (1), `zumupstreamtest` (3), `zumfederationtest` (1), and
`zumclustertest` (1), using fresh disposable SQLite stores. During acceptance,
`ZdbSL` was corrected to distinguish a valid empty offset-backed value from a
decode failure; the adapter test now covers an all-empty type-family row.

## Baseline

- Date: 2026-09-09
- Build configuration: existing clang debug tree; not reconfigured.
- Initial inspection performed without building.
- Existing schema: 10 Zum tables (`issuer`, `user`, `cred`, `action`, `role`,
  `scope`, `client`, `grant`, `signKey`, `audit`).
- Existing programs: `zum` is an offline Zdb bootstrap utility; `zumd` and
  `zumc` are examples.
- Existing `libZum` contains both server and database implementation.
- Pre-existing worktree edits are preserved, including `ZumMgmt.hh` and its
  build/export additions.

## Current continuation: 2026-09-10

Latest schema-17 acceptance on 2026-09-11: ordinary incremental clang-debug
builds of `zum/src`, `zum/test`, `zum/itest` and `zum/example` passed with `-j3`
and no compiler warnings. All five unit binaries passed 31 TAP cases. Fresh
PostgreSQL stores `zum17_mig_src`/`zum17_mig_dst` passed all 12 migration checks;
`zum17_restart` passed all 13 then-current restart/recovery scenarios. Fresh stores
`zum17_http`, `zum17_federation`, and `zum17_cluster_a`/`zum17_cluster_b` passed
the full HTTP, TLS federation, and two-node activation fixtures respectively.
The HTTP run includes nested `zum.issuerURL`/`zum.clientID` service configuration,
missing/invalid environment-secret rejection and redaction, unavailable-server
failure/recovery, signing-provider-reference rejection, key rotation, and all
68 administrative success/401/403 paths. No build or test is live.

A focused rerun of `zumfederationtest` on fresh PostgreSQL store
`zum17_federation_claim` also passed the complete TLS federation flow after
adding `ClaimValues` policy coverage: accepted array and scalar claims authorize,
unknown values and invalid claim types deny, and the restored mapped-role policy
continues through the existing UserInfo and freshness cases.

A second focused rerun on fresh PostgreSQL store
`zum17_provider_isolation` passed after adding a second enrolled provider record
backed by the same independent TLS issuer and subject. Its application cannot use
the first provider's maps, an unmatched provider-local map denies, its own matching
maps authorize, and projected user/evidence state remains qualified by both
application and provider. This also exercises identical `ping` catalog names in
another isolated application. No production rebuild was needed for these
test-driver additions.

The expanded PostgreSQL restart suite passed all 14 scenarios on fresh store
`zum17_role_boundaries2` in 208.871 seconds. The added role-deletion matrix
reopens the database from the payload and every intent/effect cut across all 24
run-time-expanded saga steps (49 durable images), including repeated membership,
client-access, admin-access, scope and role-map branches. It verifies successful
completion or compensation, stable unrelated state, logical versions, released
owners and empty journals. The first attempted matrix exposed only a fixture
idempotency-key collision between cases; the per-case key was corrected before
this fresh-store pass. No production source changed and no in-memory restart
evidence was used.

Earlier schema-16 post-cleanup acceptance on 2026-09-11 covered the complete
standalone HTTP workflow, federation and two-node activation. The final
production-form `zumd` passed `zumhttptest` on fresh PostgreSQL store
`zum16_http_accept`: all 68 administrative operations had observed success,
401 and 403; bootstrap enrollment, browser/PKCE admin login, REST-only app/user/
membership administration, real `zumpingd` catalog publication, real `zumping`
ping/pong and refresh, role denial/restoration, service restart, secret rotation,
backup restore and repeated daemon drains all passed. The same current lifecycle
sources passed five consecutive instrumented fresh-store runs before temporary
diagnostics were removed. The optional `ZUM_HTTP_DB_KEY` fixture variable now
permits a deterministic test key for reproduction; absent it, the fixture still
generates private random key material.

Two earlier schema-16 HTTP runs timed out in synchronous
`Zhttp::Server<Daemon>::stop()` before DB shutdown, once during backup validation
and once during application-login restart. Their symbol-only traces both located
the wait in the HTTP engine. Focused rebuilt `ZhttpH1HubTest` and
`ZhttpServerIdleTest` passed 26 cases, including active and retained-link stop.
Five subsequent boundary-instrumented HTTP runs and the final uninstrumented run
all passed; the pending-connection diagnostic never fired and was removed. This
is strong regression evidence but not a proven root cause for the two timeouts;
keep intermittent HTTP shutdown on the regression watch list.

`zumfederationtest` passed on fresh PostgreSQL store `zum16_federation`, covering
the private-CA HTTPS upstream, discovery/code exchange, ID-token and UserInfo
validation, N:1 mapping, external projection/evidence persistence, unmapped-role
denial, local administrator access during upstream outage, and unchanged native
ping integration. `zumclustertest` passed on fresh stores `zum16_cluster_a` and
`zum16_cluster_b`, including standby 503 coverage for all 68 administrative
operations and survivor activation. Zdb remains responsible for replication and
failover guarantees. The final `make -C zum/test -j3 && make -C zum/test test`
passed all five binaries/31 TAP cases. Builds used only the existing clang-debug
configuration and `-j3`; no clean, reconfiguration or downstream module build
was used. No build or test is live.

The final contract audit found that the signing-key REST handler accepted a
`providerRef` even though this daemon has no configured external signer. Such a
key could become active but could not issue a token. The handler now rejects
provider references and requires matching private material; the HTTP signing
rotation fixture verifies rejection leaves the collection unchanged. The schema
retains `providerRef` for a future explicitly configured signer implementation.
The incremental clang-debug `make -C zum/src -j3` passed without warnings, and
`zumhttptest` passed this correction on fresh PostgreSQL store
`zum16_signing_contract` as part of its complete signing rotation/restart flow.

The example acceptance fixture now also runs the real `zumping` executable while
`zumd` is stopped and while `zumpingd` is stopped. Each case must terminate with
failure and emit no pong; the `zumd` case then restarts the provider and requires
another successful login/refresh/pong through the still-running service. This
test-only expansion passed in the complete `zumhttptest` on fresh PostgreSQL
store `zum16_unavailable_contract2`. Its first run used the same 30-second outer
deadline as the configured client deadline and raced a correctly logged client
timeout; the isolated unavailable-case config now uses a three-second client
deadline and a ten-second harness bound. No production timeout was changed.

Schema ownership cleanup completed and verified on 2026-09-11. The physical
schema is now version 17. `Issuer` no longer carries global action allocation or
authority version state; `User` no longer carries role IDs or an unqualified
OIDC subject; `Scope` no longer duplicates its audience URI; and `Client` no
longer duplicates target audiences, scopes or roles. Per-application authority
is persisted only in `App`, `Membership`, `ClientAccess`, `Audience`, `Scope`
and `Grant` as applicable. Runtime scope resolution carries the audience URI in
a non-persistent `ScopeAuth` value. Authority loading no longer synthesizes the
removed fields or grants an app-zero/global fallback.

The schema-17 increment removes the unused persisted `Evidence::refreshOutcome`
field left over from the abandoned database audit design. Upstream evidence
contains authority provenance/freshness and optionally encrypted refresh
material; operational outcomes are emitted through ZiLog rather than retained
as IAM data. The schema-17 source, generated FlatBuffers metadata, unit roundtrip,
migration, PostgreSQL recovery, HTTP, federation and cluster gates all pass as
recorded at the head of this continuation.

The obsolete issuer-global administrative sagas and mutation API were removed;
the app-scoped REST sagas are the sole production mutation path. Enrollment now
creates identity/passkey state only, while `Membership` is the sole local
user-to-application role assignment. Audit remains structured `ZiLog` output,
not a Zum persistence subsystem. The PostgreSQL restart fixture's basic action
case now runs the production `AppActionAdd` saga instead of the removed global
allocator.

Focused clang-debug evidence: `make -C zum/src -j3` PASSED; `ZumMigrateTest`
PASSED 2 cases/30 assertions; `ZumTest` PASSED 22 cases through 458 assertions;
and `make -C zum/itest -j3 zummigrationtest zumrestarttest` PASSED. Fresh
PostgreSQL schema-16 stores `zum16_mig_src`/`zum16_mig_dst` passed all 12
migration, interrupted-recovery and repeated-bootstrap checks. Fresh store
`zum16_restart` passed all 13 durable restart/recovery scenarios, including the
complete app-action and catalog intent/effect boundary matrices. Each shutdown
closed request admission and drained Zdb/PostgreSQL before reopening. No
in-memory store was used for restart evidence. PostgreSQL `template1` has a
pre-existing collation-version mismatch, so the disposable databases were
created from `template0`; cluster metadata was not changed. No test is live.

Offline schema migration is now implemented and verified on 2026-09-11.
`zumd --migrate=MAPPING.json --source-connect=... --connect=...` opens distinct
standalone source and target stores, validates the complete legacy store before
writing, uses native migration sagas for the marker and each translated row,
and drains both stores before success. The typed source adapter covers the
pre-application 10-table schema. Explicit mappings translate global action
bitmap positions, roles, scopes, audiences, clients and user memberships into
application-local records. Legacy grants/signing keys/audit rows are invalidated;
provider-unqualified external projections and their credentials require an
explicit `discardExternalUsers` entry and are not silently converted.

The focused clang-debug build `make -C zum/src -j3` PASSED. `ZumMigrateTest`
PASSED 2 TAP cases/30 assertions. The dedicated PostgreSQL
`zummigrationtest` PASSED 1 TAP case/12 assertions against fresh stores
`zum_migrate_source_13_20260911` and `zum_migrate_target_13_20260911` selected by
`ZUM_MIGRATION_SOURCE_CONNECT` and `ZUM_MIGRATION_TARGET_CONNECT`. It populated
the physical legacy tables, staged a drained interrupted target containing one
completed app and one payload-only `migrationPut.v1` saga, then ran the real
daemon. Zdb replayed that saga and migration completed. Exact translated rows,
split memberships/client access, discarded external user/credential and scope
catalog baselines passed. A completed second migration was byte-semantic
idempotent. Ordinary `zumd --once` then bootstrapped the migrated Empty store;
a second start preserved the AdminPending core IDs. The mapping's explicit
positive `time` makes synthesized rows deterministic across retries. No build
or test is live. This is staged durable recovery, not arbitrary process-kill
coverage at every row boundary.

The destination gate uses Zdb's maintained whole-table cardinalities for all
22 Zum application tables before the first marker and before
`MigrationFinish`; this detects orphan authority without misinterpreting a
table's grouped primary-key count. While diagnosing that distinction, the
PostgreSQL adapter's one-shot grouped `count()` callback was corrected not to
fire again on libpq's terminal null result.

HTTP batch44 PASSED (run47548,33.491 seconds). Full68 admin success/401/403
coverage remained green; repeated server shutdowns did not reproduce the stale
TCP-link crash. Offline rotation, partial-error resume, independent signing and
provider secret verification, and completed retry passed. The fixture also
created a custom-format pg_dump before rotation, restored it into a fresh
separate database with uint/libz installed in required order, proved restored
ciphertext identical, rejected the new key, and reached readiness/login with
the matching old key. It dropped the exact random restore DB after success.
Batch44 consumed; no live build/test. Rotation lifecycle acceptance is now
substantially covered, though this is graceful shutdown plus staged native saga
recovery/partial-error resume, not arbitrary process-kill injection.

After resolving the crash and restore-order failure, removed the exact private
temporary diagnostic directories `/tmp/zum-shutdown-core-Si4dF4qW`,
`/tmp/zum-http-nekgwr9n`, and `/tmp/zum-http-psl98h6m` using bounded depth-first
deletion because `/tmp` does not support trash. They contained the exported
core, secret-bearing fixture files, and failed archive; they are not recoverable.
Repository sources and PostgreSQL source fixture DBs were not deleted.

Top-level rebuild59100 PASSED through configured Zum src/test/itest/example and
interop. It exposed only unused KeyValues aliases in HPack/QPack; removed both.
Final incremental build44023 PASSED without compiler warnings (libtool's existing
program -release notices are build-system diagnostics). Post-build regression:
ZmHashTest2 PASSED11/45ms and all four Zum unit binaries PASSED29/2.012s.
HTTP batch43 reached offline rotation and backup restore without reproducing the
prior shutdown crash, then FAILED only because pg_restore restored libz before
uint and libz had already created type uint4. Diagnostics retained at
/tmp/zum-http-psl98h6m; batch43 consumed. Updated fixture to preinstall uint,
then libz, and restore a generated archive TOC excluding only EXTENSION and
extension-COMMENT entries. Successful restore drops its exact random target;
failure retains it. Removed the exact failed throwaway target
zum_key_backup_d07ab356d0d5540f; source fixture data remains recoverable in its
retained diagnostic archive. Python syntax/diff checks pass. HTTP batch44 later
passed as recorded at the head of this continuation; its database is consumed.
No build or test is live.

ZmHash fix validation: build27737 PASSED, then prove -v -j1 ZmHashTest2
PASSED11 cases/42ms. Its concurrentCount subtest now observes exactly16384
inserted nodes by count and traversal, removes all16384, and ends with zero
count/empty traversal. Started ordinary top-level incremental make -j3 as
build59100 so dependency files rebuild affected enabled modules through Zum;
do not start another build. This is the configured module graph ending at Zum,
not downstream WIP. No clean/reconfigure/alternate makefile was used. After it
passes, rerun the shutdown/backup HTTP fixture on a fresh PostgreSQL database.

Shutdown crash root-cause evidence: coredump PID1456503 exported privately to
/tmp/zum-shutdown-core-Si4dF4qW/core (contains process secrets; never publish).
libtool/gdb scalar inspection shows TCP m_pending=0, m_links.m_count=0, but
bucket11 still holds a node pointing to the invalid link; hub m_nLinks=1.
ZmHash add/delete/iterator-delete use separate load/store updates to their
shared count while different lock stripes mutate concurrently. Lost updates
can make count zero with live nodes; delNode_ then refuses removal, leaving
a stale link after destruction. Added concurrentCount to ZmHashTest2 using
two semaphore-synchronized ZmThreads and disjoint keys without resize. Baseline
build13185 PASSED; prove -v -j1 ZmHashTest2 FAILED the count-after-insert and
count-after-delete assertions while traversal proved all inserted nodes present.
Patched the three count updates to use atomic ++/-- for locked hashes, retaining
the prior load/store fast path for ZmNoLock. No TCP/Zdb workaround. A new
incremental zm/src + zm/test build27737 is live. Need run regression, then rebuild affected source
dependencies through Zum before HTTP/backup verification. No PostgreSQL test
is live; batch42 consumed and backup/restore still unverified.

HTTP batch42/run86737 FAILED before reaching rotation/backup: ordinary first
server shutdown SIGSEGV after5.628 seconds. Private diagnostics are retained
at /tmp/zum-http-nekgwr9n/server.log; do not print whole logs or secrets.
Symbol-only trace points to Ztcp::Hub<Zhttp::Server<Zum::Daemon>::Hub<TCP>>::
downLinks_ during Server::stop_0. Disassembly maps the fault to virtual down()
dispatch at ELF0xdb386f4: loading 0x20 from a null vtable pointer, consistent
with an invalid link lifetime, not a null connection inside disconnect().
Investigate link registration/destruction and shutdown ownership before retrying;
no fix or rebuild has been made for this crash. No build/test remains live.
Batch42 is consumed; backup restore code remains unverified. Batch41 rotation
and native rekey1 recovery passes above remain valid but do not exclude this
intermittent shutdown bug. Source changes since build are Python fixture/docs.

Native PostgreSQL restart run3171 PASSED:13 cases,125.741 seconds, including
all three ciphertext-field mutations and issuer-binding switch at the three
staged durable boundaries. Existing invitation/catalog/member/enrollment
recovery gates also remain green. No build is live. HTTP batch42 run86737 is
LIVE on fresh zum_http_20260911_batch42, testing the newly added actual custom
pg_dump archive restore into a separate random zum_key_backup_* database.
The fixture compares restored ciphertext to its original, rejects startup with
the post-rotation key, and requires readiness/login with the original key.
Archive and backup database name are retained in private fixture diagnostics
on failure; it never restores over the source. Backup/restore remains UNVERIFIED
until run86737 finishes. Batch42 is consumed; batch41/rekey1 already consumed.

Build91565 PASSED through src/test/example/itest. PostgreSQL HTTP batch41
PASSED (run43956,32.732 seconds): all68 admin success/401/403 gates remain green,
plus offline rotation partial-error/pending startup refusal, wrong-target retry
with unchanged binding/ciphertext, repair/resume, new-binding switch, completed
retry without ciphertext changes, known-old-key refusal, and new-key restart
and login. Independent AESGCM verification proves signing/provider secret
fingerprints unchanged and retained old signing ciphertext readable with its
old key. Batch41 is consumed. Native PostgreSQL restart run3171 is LIVE using
zum_restart_20260911_rekey1 (also now consumed); poll rather than restart it.
No build is live. These passing partial-error tests are not process-kill or
full PostgreSQL backup/restore evidence. Commands from zum/itest used PATH
/home/count0/postgres/bin, ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so,
ZUM_HTTP_CONNECT='host=/tmp dbname=zum_http_20260911_batch41' prove -v -j1 zumhttptest;
ZUM_TEST_CONNECT='host=/tmp dbname=zum_restart_20260911_rekey1' prove -v -j1 zumrestarttest.

Unit batch39644 PASSED: make -C zum/test test, four binaries /29 TAP cases,
2.017 seconds (ZumTest, ZumUpstreamTest, ZumClientTest, ZumSecretTest). This
includes the new issuer/saga serialization cases and crypto/key-check tests,
including empty plaintext, wrong-key/AAD/tag rejection and rekey retry. Native
src/test builds passed and examples were up to date. Build91565 is still live
in zum/itest compiling the expanded restart test and upstream test. PostgreSQL
rotation and recovery cases have NOT run; batch41/rekey1 stores remain unused.

Build91565 has now PASSED zum/src (including ZumRekey, ZumSagas and zumd link)
and is compiling zum/test; examples/itest remain in the same queued command.
No tests run yet. HTTP fixture now creates a nonempty provider client secret
through the production endpoint, checks REST redaction, and independently
decrypts provider ciphertext before/after rotation to require identical secret
fingerprints. This closes the prior test's empty-provider-field gap once run.
Python syntax and diff checks pass. Prepared PostgreSQL stores remain unused.

Build91565 remains live; last confirmed output completed zumd.cc and
ZumAuthorize.cc, with no new diagnostics observed. Strengthened the unrun
rotation fixture: current issuer key binding must remain unchanged through
partial failure and wrong-target retry; compare undamaged signer ciphertext
by ID rather than positional rows. Independent Python AESGCM decryption now
compares private-material fingerprints before/after rotation and verifies the
retained old ciphertext with its old key. Plaintext is not printed or persisted.
Syntax/diff checks pass. This is matching-key field-snapshot evidence once run,
not a full PostgreSQL backup/restore test. No rotation runtime test has run yet;
fresh batch41/rekey1 databases remain unused.

Build31339 exited2 after its remaining jobs drained. Build91565 is now live
with the native saga Base-alias fix, using the same ordinary incremental -j3
src/test/example/itest command. Also corrected zumd --rekey to check the result
of db->stop() before printing successful rotation; a failed store drain must
not be reported as durable completion. Rotation execution remains unverified.
Prepared PostgreSQL batch41/rekey1 stores are still unused. Added operator
instructions to MANAGEMENT.md, explicitly linked to this validation status.

Build34799 ended with two ZumSagas errors. Its diagnostic detail was truncated,
so incremental build31339 was started to capture it: both new saga definitions
omitted the Base alias required by native MSaga::load. Added the established
Base/context/saga declarations; no Zdb changes. Build31339 is still draining
its other compile jobs after the failed saga compilation; poll it before any
next make. No rotation tests have run. Prepared fresh, UNUSED PostgreSQL stores
zum_http_20260911_batch41 and zum_restart_20260911_rekey1 from template0 with
uint/libz extensions for the next test batch. Do not recreate/reuse them once
a fixture has consumed them. Existing postgres collation warning left alone.

Build34799 remains live (most recent output compiling ZumDB/ZumAuthorize);
do not start a second build. During compilation, expanded the unrun HTTP rekey
fixture to corrupt a late signing envelope, require partial failure/pending
startup refusal, reject a different resume target without ciphertext changes,
repair the fixture envelope and resume with the original target. Added native
rekeyRecovery to zumrestarttest: provider/evidence/signing-key replacement and
issuer binding switch each staged before step UN, after UN and after mutation,
followed by drained PostgreSQL reopen and unchanged-business-field assertions.
These are durable-image recovery tests, not process-kill tests. Python syntax
and diff whitespace checks pass; all new runtime gates remain UNRUN. The build
has not yet reached itest, so these source edits join the existing build batch.

Historical rotation driver and CLI (since superseded by Vault) took two
environment keys, started no HTTP/upstream client, waited for native
activation, and drained shutdown before its completion message. The current
interface loads the old key from Vault and takes the new key as `ZUM_DB_KEY`.
ZumRekey.cc uses one indexed key/row and one native saga in flight, marks the
target key pending, rewraps provider/evidence/signing-key fields, verifies a
second pass, then switches the key binding. Completed retries verify without
changing ciphertext. Store scan callbacks dispatch row access to the table
owner. The shared schema constant is15. Added a PostgreSQL HTTP-fixture gate
for rotation, byte-identical completed retry, old-key refusal and new-key login.
Python syntax and diff whitespace checks pass. BUILD RUN34799 is live:
make -C zum/src -j3 && make -C zum/test -j3 &&
make -C zum/example -j3 && make -C zum/itest -j3.
No runtime claims for this batch yet. Interrupted rotation, wrong target-key
resume, malformed ciphertext, evidence nonempty-field and backup-matching
coverage remain pending. Current Evidence writers retain no refresh ciphertext;
the rotation AAD contract for that field is evidence / appID:userID:providerID /
protectedRefreshToken. Do not claim offline lifecycle acceptance from the
command implementation or the as-yet-unrun happy-path fixture alone.

Added private ZumRekey.hh with native keyBinding.v1 and secretRekey.v1 saga
definitions and their FlatBuffers payloads/catalog registration. KeyBinding
updates current/pending key checks in one issuer step. SecretRekey branches
among provider, evidence and signing-key ciphertext updates using native skip;
payloads contain record identifiers and before/after ciphertext only. Forward
updates reject stale ciphertext and outstanding provider/evidence ownership;
compensation restores ciphertext without changing business fields. Native Zdb
owns step replay. Added MSaga payload roundtrip cases for both definitions and
all three field selectors. UNBUILT/UNTESTED; diff whitespace check passes.
Offline driver/CLI, bounded indexed traversal and final verification are still
missing; no rotation can yet be invoked. No build/test is live.

Offline rotation persistence groundwork: Issuer now has hidden, mutable
pendingKeyCheck metadata (including FlatBuffers); bootstrap refuses readiness
when it is present. Schema gate is now15. SignKey.privateMaterial is mutable
so ciphertext can be replaced through native Zdb updates. Extracted the existing
key-check derivation into private serverKeyCheck for bootstrap and maintenance
to share; it rejects non-256-bit input. Added pending-marker serialization and
key-check tests. These changes, like the crypto extraction below, are UNBUILT
and UNTESTED; git diff --check passes. No build/test is live. The maintenance
command, record traversal, native saga updates, final verification/key switch,
and PostgreSQL interrupted-rotation tests remain unimplemented. A marker alone
does not constitute resumable rotation. Last verified runtime remains schema14.

Moved secret encryption/decryption/rekey from ZumBootstrap.cc into private
ZumSecret.hh/.cc in libZumServer (not exported/installed with public libZum).
Bootstrap signer preparation now calls the shared serverSecretEncrypt entry.
Added ZumSecretTest and ordinary Makefile.am/default-test registration plus
test README. Cases cover roundtrip, randomized nonce output, wrong keys,
issuer/table/record/field substitution, tampering, rekey retry and empty plaintext.
Source review found AAD output length being reused as payload output length
when encrypting/decrypting empty plaintext; offset now remains zero in that
case. All changes are UNBUILT/UNTESTED. No reconfigure/clean/supplemental makefile
was used, and no build/test is live. Continue batching offline maintenance
implementation before rebuilding; the primitive/tests do not complete rotation.

Started offline DB-key rotation implementation with serverSecretRekey in
ZumBootstrap.hh/.cc. It validates both key lengths, authenticates using the new
key first to recognize already-rewrapped fields, otherwise authenticates using
the old key and encrypts with the new key/fresh nonce. Temporary plaintext is
cleared on both successful paths; failed decryption/encryption leaves original
ciphertext unchanged. This primitive is UNBUILT/UNTESTED and not yet called by
a maintenance operation. No server CLI, persisted rotation progress, field
traversal, key-binding switch or recovery test has been implemented. Keep the
full rotation requirement open. Next work should expose the existing secret
implementation to focused server-side tests and wire the offline operation,
without adding key sources outside protected environment injection. No build
or runtime test is live; batching the implementation before rebuilding.

Remaining-work audit found an explicit unimplemented feature beyond schema
migration: offline DB-secret key rotation (zum3.md lines208-214,1811).
zumd Options has no maintenance/rewrap mode; startup reads only ZUM_DB_KEY.
ZumBootstrap.cc's AES-GCM envelope writes/requires fixed KeyID1; keyCheck binds
startup to one key. Existing clientSecretRotate/signKeyAdd/signKeyRetire are
different operations and do not meet the DB-key rotation contract. Recoverable
fields include Provider.clientSecret, Evidence.protectedRefreshToken, and
SignKey.privateMaterial; the latter is not currently marked Mutable in metadata.
A rotation implementation must account for that persistence constraint, keep
keys environment-only and plaintext out of saga payloads, use native Zdb
continuations/sagas for resumable progress, and switch the issuer key binding
only after all encrypted fields are verified under the new key. No rotation
code or test exists yet; do not claim encryption lifecycle acceptance complete.
No build/test was started for this audit.

HTTP batch40 PASSED (run33062, 24.743 seconds). GET/POST UserInfo reject an
otherwise valid access token while its client is suspended through the real
administrative state endpoint. Restoring the client permits the same unexpired
access token and unchanged subject again. Existing refresh, cross-app token
rejection and68 success/401/403 administrative gates remain green. This does
not establish global-user suspension or credential-recovery behavior. No
rebuild; store consumed and no build/test is live. Command (zum/itest):
PATH=/home/count0/postgres/bin:$PATH
ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so
ZUM_HTTP_CONNECT='host=/tmp dbname=zum_http_20260911_batch40'
prove -v -j1 zumhttptest.

HTTP batch39 PASSED (run83215, 24.914 seconds). New GET/POST UserInfo checks
accept a signed openid access token and return exactly its ID-token subject,
without ungranted profile/email fields. Missing credentials, malformed bearer,
the ID token itself, and a valid admin access token lacking openid all return
401 invalid_token. The full standalone workflow and68 success/401/403 gates
remain green. Profile/email opt-in, client/user suspension and additional
UserInfo token-substitution cases are not proved by this subset. No rebuild;
store consumed; no live build/test. Command from zum/itest:
PATH=/home/count0/postgres/bin:$PATH
ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so
ZUM_HTTP_CONNECT='host=/tmp dbname=zum_http_20260911_batch39'
prove -v -j1 zumhttptest.

HTTP batch38 PASSED (run16727, 23.987 seconds). The real ping service now
rejects both a core-admin access token and another application's token with
the same ping action name, before and after service restart. The fixture
independently verifies each token's signature, issuer and unexpired lifetime
and checks its audience differs from ping; both service responses are401.
Normal user ping/pong and refresh still pass, as do all68 success/401/403
administrative gates. This verifies cross-resource token rejection, not every
possible issuer/client/signature substitution. No source rebuild was needed;
store consumed and no build/test is live. Command (zum/itest):
PATH=/home/count0/postgres/bin:$PATH
ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so
ZUM_HTTP_CONNECT='host=/tmp dbname=zum_http_20260911_batch38'
prove -v -j1 zumhttptest.

HTTP batch37 PASSED (run38438, 24.793 seconds). New cross-app REST checks try
the core superuser role ID in business-app membership roles, scope roles,
client access and administrative delegation. All return400 with unchanged
target records, app authority versions and source roles. The fixture confirms
the role ID is absent in the target app; this checks ownership-qualified
references, not same-ID/same-name catalog isolation. All68 success/401/403
gates still pass. No production edit/rebuild was needed; store consumed and
no build/test is live. Command from zum/itest: PATH=/home/count0/postgres/bin:$PATH
ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so
ZUM_HTTP_CONNECT='host=/tmp dbname=zum_http_20260911_batch37'
prove -v -j1 zumhttptest.

Cluster schema14 a5/b5 PASSED (run26654, 2.341 seconds). Standby admission
coverage now enumerates all68 method/path declarations from ZumMgmt.cc rather
than only GET/admin/apps; every request returned503. Fixture checks the source
catalog count and uses syntactically minimal, unauthenticated requests, so this
proves admission closure, not authenticated request validation on an inactive
node. Graceful leader stop, survivor activation, unchanged bootstrap IDs and
clean teardown also passed. No new production code or rebuild was required.
Both stores consumed; no build/test is live. Command from zum/itest with
PATH=/home/count0/postgres/bin:$PATH and
ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so:
ZUM_CLUSTER_CONNECT_A='host=/tmp dbname=zum_cluster_20260911_a5'
ZUM_CLUSTER_CONNECT_B='host=/tmp dbname=zum_cluster_20260911_b5'
prove -v -j1 zumclustertest.

Standalone schema14 HTTP batch36 PASSED (run81010, 24.701 seconds). The fixture
now requires success,401,403 for all68 catalog operations. A service client's
admin delegation suspension/restoration adds the previously missing forbidden
operationQuery case without removing its seeded permission. Batch35 failed
only this new coverage assertion (67/68 forbidden cases); source behavior was
not changed. Observed400 coverage is28/68,412 is24/68,503 is0/68 in this
standalone fixture. Those raw counts are not per-operation applicability or
cross-app/security completeness claims; inactive-node coverage remains in the
cluster fixture. Both stores are consumed, no build/test remains live.
Command from zum/itest: PATH=/home/count0/postgres/bin:$PATH
ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so
ZUM_HTTP_CONNECT='host=/tmp dbname=zum_http_20260911_batch36'
prove -v -j1 zumhttptest. No rebuild was needed.

Build57180 PASSED the ordinary incremental clang-debug -j3 sequence through
Zum src/test/example/itest. Unit run85424 PASSED: ZumTest, ZumClientTest,
ZumUpstreamTest, 28 cases, 1.975 seconds. This includes new invitation snapshot
roundtrip, metadata, version invalidation and stale-snapshot compensation checks.
Federation batch12 PASSED (run24651, 15.680 seconds): actual projected-name
collision advances external authority/version once, denies old refresh and
silent SSO, preserves separate identities and avoids upstream fallback. Matching
only the untrusted login hint does not invalidate the unrelated projection.
The PostgreSQL restart suite PASSED (run32466, 12 cases, 113.154 seconds) on
zum_restart_20260911_conflict1. The new invitationRecovery case verifies all17
intent/effect cuts plus stale-snapshot compensation, alongside the existing
membership/catalog/role-deletion recovery coverage. These are drained reopen
tests with staged durable images, not process-kill injection. Both stores are
consumed; no build or test remains live. Full concurrent projection/invitation
race coverage, migration and external-provider acceptance remain outstanding.
Commands (zum/itest): ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so
with ZUM_FEDERATION_CONNECT='host=/tmp dbname=zum_federation_20260911_batch12'
prove -v -j1 zumfederationtest; separately with
ZUM_TEST_CONNECT='host=/tmp dbname=zum_restart_20260911_conflict1'
prove -j1 zumrestarttest. PATH included /home/count0/postgres/bin.

Conflict fix source implemented, not yet build/runtime verified. Projection
names are synthetic oidc:<provider>:<userID>, not the untrusted browser hint.
Batch10 therefore did not prove the claimed projected-name conflict. Corrected
fixture first verifies that a local account matching only the hint does not
invalidate the unrelated external identity, then invites the actual projected
name. Batch11 against the unchanged binary FAILED as expected (run30463,
14.807 seconds): actual projected-name conflict still permits refresh200.
Private diagnostics: /tmp/zum-federation-njmmd4mp; store consumed.
AuthorityLoad_ now rejects interactive external authority when an exact local
name exists. UserInvite.v2 persists an external-user snapshot and performs a
compensatable authVersion/version increment with native saga ownership/release;
existing session/grant version checks provide identity-wide invalidation.
No scan or second revocation service is introduced. New payload metadata and
schema gate14 require old invitation intents to be settled before migration.
Fixture also checks version advancement and silent session rejection. Recovery
boundaries, snapshot races and new runtime behavior remain UNVERIFIED.
An ordinary incremental clang-debug -j3 build through Zum src/test/example/itest
has just been started; poll its live session before any further build.

Federation batch10 FAILED (run60172, 14.373 seconds) in a new local-first
identity-conflict scenario. After a successful external login, inviting a
distinct pending local user with the same normalized login correctly yields
a local passkey page with no upstream calls and no transferred membership.
But the previous external refresh token still returns200 instead of invalid_grant.
This contradicts zum3.md's explicit conflict-revocation contract. Do not weaken
the test: it remains failing. AuthRouteLoad_::start currently returns Local as
soon as a local row exists, with no affected-external-session/family revocation.
Existing Revoke and user authVersion invalidation paths require review for the
fix; no new revocation mechanism or speculative production change was added.
Private diagnostics: /tmp/zum-federation-ki_puo23. Store consumed; no process or
build remains live. Next implementation work is this confirmed contract gap.

Migration source audit: commit6dda5fe has the global-role layout (Issuer has
no schemaVersion; User.roleIDs, global Action/Role IDs, Scope.audience strings,
Client.audiences/scopeIDs/roleIDs). HEAD e19b652 already registers app-qualified
tables and its bootstrap requires schema9; the current worktree requires13.
These require distinct source adapters, not one version-marker upgrade.
ZdbPQ validates physical columns and indexes; no generic import/export path
was found in Zdb src. Old saga definitions must be settled with their owning
source executable before conversion. An adapter must preserve/translate action
bitmaps explicitly and supply app ownership and standard-scope manifest history
where absent. The actual existing store/source revision remains unspecified;
the earlier source-store question is still open. No migration converter has
been implemented or existing store modified by this investigation.

HTTP batch34 PASSED (run87449, 24.177 seconds), all68 administrative success
cases plus authentication-age and refresh-reuse additions to session lifecycle.
Silent authorization accepts max_age=3600; after a real protocol-second
boundary max_age=0 returns login_required. Restoring the original cookie permits
authorization without that age restriction, separating freshness from session
validity. For both native business and admin clients, refresh rotation followed
by consumed-token replay rejects the replay and then the newest family token.
This establishes real HTTP family revocation, beyond stale-token rejection.
No production change/rebuild was needed. Store consumed; no live test/build.
Command (zum/itest): PATH=/home/count0/postgres/bin:$PATH
ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so
ZUM_HTTP_CONNECT='host=/tmp dbname=zum_http_20260911_batch34'
prove -v -j1 zumhttptest.

HTTP batch33 PASSED (run74490, 23.960 seconds), including all68 administrative
success cases and new explicit session lifecycle checks. A confidential-client
login supplies silent prompt=none authorization for a native business client
and the separate Zum admin application. prompt=login displays authentication;
invalid logout CSRF preserves session use; valid provider logout rejects both
the cleared cookie and replay of the old authenticated handle. Independent
business/admin refresh grants remain usable after provider logout.
Batch32 failed because the fixture tried to use the unauthenticated transaction
cookie from the abandoned prompt=login flow as the authenticated session.
The corrected test restores the original session handle before the separate
logout checks; no production behavior changed. This is not completed
reauthentication, account switching, or a full session-expiry/provenance matrix.
Both stores are consumed; no build or test is live. No rebuild was needed.
Command: PATH=/home/count0/postgres/bin:$PATH
ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so
ZUM_HTTP_CONNECT='host=/tmp dbname=zum_http_20260911_batch33'
prove -v -j1 zumhttptest (from zum/itest).

Build47045 completed exit0 using the ordinary incremental clang-debug -j3
build through Zum src/test/example/itest. Expanded cluster a3/b3 failed in the
fixture's read-only snapshot SQL: generated ID columns use `_i_d`, not `_id`.
Corrected the query; no production change or rebuild was required. Fresh
a4/b4 PASSED (run66452, 2.318 seconds): standby health/admission, graceful
leader stop, survivor preparation following native Zdb activation, unchanged
bootstrap identities, and clean teardown. This is not a durability/SLA test.
Federation batch9 PASSED (run74051, 14.752 seconds), including new signed
upstream ID-token nonce/audience/issuer mismatch rejection before UserInfo fetch
or assignment-evidence changes, and the existing local/delegated ping, restart,
expiry and refresh-narrowing scenarios against the new startup implementation.
All of these stores are consumed. No build or runtime test remains live.
Offline migration, the full negative authorization matrix, real-browser and
external-provider/conformance acceptance remain outstanding.

Build39787 completed exit0. First passive-startup cluster run a2/b2 PASSED
(run52306, 3.412 seconds): both listeners, one prepared active node, standby
administrative503, health and clean stop. Standalone HTTP batch31 also PASSED
(run28165, 23.935 seconds), including the updated wrong-key/schema refusal tests.
All three stores are consumed. Follow-up fixes now discard preparation failure
when its activation generation is no longer current, apply bootstrap reissue
only until the first successful bootstrap, and finalize partially initialized
Daemon state on failure. Build47045 is live for those main-only changes.
The cluster fixture now stops the active process gracefully, awaits Zdb's next
activation, and checks unchanged issuer/bootstrap IDs using read-only PostgreSQL
snapshots. Fresh a3/b3 stores are prepared and unused. This extension is UNRUN;
no runtime test is live. No failover durability/SLA claim is made.

Passive HTTP/active preparation refactor is implemented and build39787 is live
in Zum src (ordinary -j3, then test/example/itest). Daemon init no longer loads
signing material; main-thread prepare follows bootstrap. The process uses one
wake semaphore for Zdb notifications and signals, so a standby no longer waits
on a signal-inaccessible activation semaphore. Existing Requests guards separate
internal bootstrap from HTTP admission; both drain before preparation and close
on deactivation. Zdb-thread generation checks reject stale preparation completion
before opening HTTP admission; this is not a new persistence/election epoch.
Normal HTTP fixtures await both listening and active events. Wrong-key/schema
checks require failed active admission, permitting passive health before refusal.
Cluster fixture now awaits one prepared node plus both listeners and verifies
standby administrative HTTP503. Fresh PostgreSQL a2/b2 cluster stores and HTTP
batch31 are prepared with uint/libz and unused; no runtime test is live.
Further review/coverage still needed for deactivation during signing preparation
(a failed prepare currently terminates even if activation was lost meanwhile),
and limiting explicit bootstrap reissue to the first successful preparation.
Do not claim all reactivation edges verified from the initial startup fixture.

New two-node PostgreSQL activation fixture reproduces missing standby HTTP:
build65802 passed ordinary Zum src/itest all (Makefile.am regenerated normally).
zumclustertest run73490 FAILED after30.147 seconds: native logs on both nodes
confirm host0 elected leader, but both listeners never become available. Both
processes stopped cleanly in this run. Private diagnostics:
/tmp/zum-cluster-h6v6d7f1. Databases zum_cluster_20260911_a1/b1 are consumed.
No build or runtime test is live. This is a Zum startup/admission gap, not
evidence of a Zdb election defect or a failed replication guarantee.
Source: zumd waits on db->active before initializing HTTP; dbUp activates the
shared request guard before bootstrap/signing preparation. A correct refactor
must start passive HTTP independently while keeping DB-dependent requests shut
until active bootstrap/key preparation completes, and handle reactivation and
signals during startup without adding a separate election or saga engine.
Daemon::init currently blocks in loadKey_, so simply moving it before activation
would incorrectly read an inactive/uninitialized store. No speculative startup
patch has been applied. Asked user asynchronously for the actual migration
source revision; offline migration remains required, not dropped from scope.

HTTP batch30 PASSED (run47322, 23.556 seconds), including explicit unsupported-
schema refusal after a drained PostgreSQL stop: nonzero exit, no listener,
migration-required log, unchanged unsupported marker and user/app counts, then
marker restoration. Batch29 had failed because the rejection subprocess omitted
the required bootstrap-output argument and exited during CLI validation; the
fixture restored its marker in finally. Both fixture issues are corrected.
No build or test remains live. This verifies refusal, not offline migration;
source-schema conversion, ownership input and migration recovery remain required.

Migration investigation confirms ZdbPQ rejects incompatible layouts rather than
converting them; opening all current tables is not a migration strategy. Added
an explicit migration-required diagnostic for unsupported issuer schema versions
to bootstrap, preserving the existing refusal. Incremental build53212 completed
exit0 (Zum src/test/example/itest). Added a stopped-PostgreSQL rejection test that
temporarily changes only the version marker, expects no listener/no additional
users or apps, and restores the marker. This is not old-schema conversion.
HTTP batch28 failed in the new fixture before changing the marker: PGDATABASE
does not expand a connection string. Fixed the fixture to use psql --dbname and
report query failure without connection details. HTTP batch29 is now running
(run44522); no build is live. Actual offline conversion remains unimplemented.

Federation batch8 PASSED (run29018, 14.371 seconds). New live checks remove both
upstream mappings, then independently disable the mapped role: refreshed signed
tokens contain no actions without upstream requests or evidence changes.
Restoring configuration cannot expand the already narrowed refresh family;
fresh login regains ping. Earlier batch7 (run55587) failed because the new test
incorrectly required HTTP400 for role removal. The scope/action-ceiling contract
permits narrowed issuance; the corrected test verifies signed actions and the
ceiling across subsequent refresh, matching the established local-user test.
No production change or rebuild was needed. Both databases are consumed; no
build or runtime test is live. Offline migration remains absent and must still
be implemented, alongside the remaining isolation/logout/replication acceptance.

Expanded federation batch6 PASSED (run43965, 13.751 seconds), including real
assignment-evidence expiry while browser-session deadlines remain later,
deadline-capped tokens, refresh denial without upstream calls/evidence extension,
and restored access after fresh upstream login. Prior UserInfo selection and
subject mismatch, unknown-role denial, real CLI/ping, outage and restart checks
also pass. Batch6 is consumed. No build or runtime test remains live.

Build66575 completed exit0. Native HTTP lifecycle suite PASSED (run89414,
6 binaries/77 tests, 4.493 seconds), including actual stop with retained closed
H1 TCP and TLS links, H2 hub, server idle/active shutdown, hub lifecycle, and
client cancellation/pooling. Together with current-binary HTTP batches26/27,
this verifies the native pending-connection accounting correction. No build live.
Added live evidence-expiry coverage using a five-second app assignmentMaxAge:
browser session deadlines remain later, resource token expiry is capped, refresh
after the actual evidence deadline must fail without extending evidence or making
upstream requests, then a fresh login restores authority. Federation batch6 is
running against a new PostgreSQL database. This new case is not yet verified.

Second current-binary HTTP run, batch27, PASSED (run75278, 27.164 seconds),
again including fixture restarts. Batch27 is consumed; no runtime test remains
live. Build66575 remains active in the HTTP default-all test stage, currently
compiling ServerIdle and client tests. After that completes, run H1Hub (including
both retained-link cases), H2Hub and ServerIdle, plus client lifecycle regressions.
Do not start another build or reuse the consumed HTTP/federation databases.

Current-binary HTTP batch26 PASSED (run44083, 27.047 seconds), including all
68 administrative success paths and fixture restarts. Expanded federation batch5
PASSED (run97893, 10.241 seconds), including selected UserInfo roles differing
from ID-token roles and subject-mismatch rejection with unchanged users/evidence,
plus prior local/delegated real-CLI/ping, outage, mapping and restart checks.
Both databases are consumed. Native retained-link regression execution still
awaits completion of build66575's HTTP default-all test build. Zum src/test/
example/itest stages completed; no runtime test remains live. One successful
HTTP batch alone does not close the prior intermittent-shutdown investigation.

Rebuilt Zum unit suite passed (run59521, 3 binaries/28 tests, 1.975 seconds).
Build66575 remains live in zum/example after completing src/test; no runtime
test remains live. HTTP batch26 and federation batch5 are still unused.
The federation fixture now also rejects a mismatched UserInfo subject and
checks unchanged user rows/evidence. Shared negative-login setup avoids
duplicating the callback ceremony. AST/diff checks passed; live federation
verification, including these additions, remains pending.

Build66575 remains live and has reached linking zumd after compiling the corrected
native lifecycle headers. No runtime test has started; prepared HTTP batch26 and
federation batch5 databases remain unused. Strengthened the UserInfo fixture so
its role claim differs from the ID token and asserted that evidence contains
only UserInfo's roles. Python AST and diff checks pass. Updated the stale
milestone table to reflect existing automated CLI/example/federation evidence
without marking incomplete acceptance gates complete.

Native TCP/TLS shutdown fix implemented, verification pending. Server m_pending
now counts installed connections via linkConnected_ on the owning Rx shard and
decrements on disconnect completion even while running. Stop no longer derives
pending completions from retained telemetry-link lifetime. Stop also crosses
the I/O Rx shard after listener cancellation before draining connected_1 work
on the application Rx shard. HTTP's existing delayed linkDrained_ notification
still gates its completion. The regression now retains the closed link across
actual stop for both H1 TCP and TLS, instead of inspecting downLinks_'s count.

Initial build20297 was explicitly interrupted (exit130) during HTTP tests after
review found that counting directly from accepted() would also count sockets
whose later I/O registration failed. The corrected hook runs on connection
installation, with a no-op Hub default for clients. Replacement build66575 is
live: ordinary incremental -j3 dependency src through Zum src/test/example/itest,
then HTTP test default-all. It has reached Zum src; no test is live. Fresh
PostgreSQL databases zum_http_20260911_batch26 and
zum_federation_20260911_batch5 have uint/libz installed and remain unused.
Federation fixture additionally switches claimSource to UserInfo, checks a live
UserInfo request, refreshed evidence and absence of local memberships before
the existing unmapped-role denial. Python AST and git diff checks passed;
this expanded federation case is UNRUN.

Retained-link diagnostic now reproduces the counting defect deterministically:
normal -j3 HTTP src/test build78412 passed; prove ZhttpH1HubTest then completed
with exactly the new subtest7 failing (13/14 passing). A fully disconnected,
retained server link contributes one pending shutdown count although its
disconnect completion has already been delivered. The diagnostic releases its
retained reference before actual stop, so this run exits normally rather than
hanging. No production fix yet. Next work is to align native stop accounting
with outstanding completion, preserve Rx/Tx draining, and then rerun the actual
PostgreSQL Zum shutdown scenario. No build or runtime test remains live.

Shutdown investigation: the three native ServerIdle/H1Hub/H2Hub diagnostic
tests passed (run40402, 30 cases), without reproducing batch25. Source tracing
found that TCP stop counts the entire link registry, including links retained
after their disconnect completion. HTTP response cancellation posts a Tx task
holding a link reference after reporting linkDrained_, providing a possible
retention window. This is not yet proof of the batch25 root cause.
Added a retained-disconnected-link diagnostic to the existing H1 hub fixture.
Its first execution aborted because the new test called client disconnect off
its owning Rx shard; corrected the fixture to dispatch through client.rxRun.
Only test fixtures changed; no speculative production fix has been applied.

HTTP batch25 reproduced the shutdown failure (run6894, 37.45 seconds), earlier
in app_login's membership restart. ZmTrap backtrace in private
/tmp/zum-http-1p0k89t0/server.log shows main waiting in
ZmEngine<Zhttp::Server<Zum::Daemon>>::stop via Daemon::stop, before DB stop.
This directs investigation to native HTTP server lifecycle, not a claimed DB
index/cache root cause. No build is live. Started existing native ServerIdle,
H1Hub and H2Hub tests as focused diagnostic coverage.

Diagnostic local HTTP batch24 PASSED (run43487 exit0, 22.76 seconds), including
68/68 administrative success coverage and all fixture restarts. Batch23's timeout
did not reproduce; no root cause or fix is claimed. Another fresh PostgreSQL
HTTP batch25 is now running under prove -v with the native backtrace-on-timeout
diagnostic still enabled. No build or other runtime test is live.

Local HTTP batch23 FAILED (run40331, 38.37 seconds): final app_login restart
timed out in Fixture.stop after30 seconds, then the fixture killed its server.
Private diagnostics /tmp/zum-http-t0z1jzx1 retained. No clean local regression
claim applies to this batch. Added a fixture-only timeout diagnostic: send
SIGABRT so existing ZmTrap emits a symbol-only backtrace, allow up to2 seconds
for exit/output, then kill if still live. No production/debugger framework change.
Fresh PostgreSQL HTTP batch24 is now running under prove -v (run43487). No build
is live; the same current binaries are being investigated, not rebuilt.

PostgreSQL restart batch3 PASSED (run10799 exit0, 11 cases, 99.25 seconds), using
native stop/reopen and staged durable saga boundaries. This is not process-kill
or clustered failover verification. Build22399 is complete; federation batch4
has passed. Local HTTP batch23 remains the only active verification run here.

Build22399 completed exit0. Expanded federation batch4 PASSED (run75106,
8.58 seconds): real zum CLI HTTPS login/enrollment, real ping service/client local
login first, then delegated external login through the same running service and
unchanged client configuration; repeated after service restart with unchanged
catalog; local role-removal/restoration checks still pass under delegated policy.
Also passes prior independent-client/freshness/outage tests, drained zumd restart
with persisted identity/provider-secret/mappings, fresh unmapped-role denial and
rejection of the prior refresh family. No upstream configuration is supplied to
zumpingd/zumping. This is independent fixture coverage, not Okta conformance.
Local HTTP batch23 is now running; PostgreSQL restart batch3 (run10799) is still
live. No build remains running.

Restart batch3 is now running (run10799) with ZUM_TEST_MODULE/ZUM_TEST_CONNECT
under prove -j1; make -q confirmed its target current. Build22399 has passed Zum
src/test and remains compiling zumping in the example stage. This CLI-only batch
does not change the recovery binary or its libraries. No federation/HTTP test
has started; their fresh databases remain unused.

Build22399 is active for the CLI HTTP protocol fixes. Federation batch4 now also
stops/reopens the PostgreSQL-backed daemon through its native drain, then repeats
external login to verify persisted provider credentials, identity binding and
mapping configuration before unknown-role denial. This extension is UNRUN.
Fresh PostgreSQL restart batch3 is prepared with uint/libz, unused, for the
recovery regression after this pass. No runtime test live; no duplicate build.

Started ordinary incremental -k -j3 Zum src/test/example/itest pass for the two
CLI protocol-policy fixes. Fresh PostgreSQL federation batch4 is prepared with
uint/libz and unused; HTTP batch23 also remains unused. No runtime test live.

Build22616 completed exit0. Expanded federation batch3 FAILED (run48488,
2.69 seconds) before admin authorization: private admin log says HTTP client
start failed. Source diagnosis: zum disables QUIC but retains Config's default
PreferH3, whose valid() requires both TLS and QUIC for HTTPS. Set DisableH3 in
zum and zumping, matching their intentionally disabled QUIC and the existing
service/upstream transports. These two changes are UNBUILT. No build/test live.
Batch3 consumed; private diagnostics /tmp/zum-federation-usj567rw retained.
The fixture now switches one running ping service from local login to delegated
login without changing service/client configuration and repeats after restart.
Those native checks and the unknown-role denial remain unverified.

Build22616 is the active Zum-only pass for caPath client config. Federation
fixture expansion now also checks fresh unknown-role denial and rejection of
the earlier refresh family after ineligible evidence is recorded. These new
native-client/negative checks remain UNRUN; batch3 remains unused. Python AST
and diff checks pass. HTTP batch23 is likewise unused for the shared fixture
regression once binaries are current.

Build9160 completed exit0. Started the ordinary Zum src/test/example/itest -k -j3
pass for the remaining admin CLI caPath change; example caPath changes were picked
up by build9160's later example stage. No runtime test is live. Federation batch3
and HTTP batch23 remain unused pending this pass.

Rebuilt Zum unit suite PASSED (run38121 exit0, 28 tests, 1.97 seconds), including
repeated external projection/evidence refresh and evidence/session deadline-index
lookups. Fresh PostgreSQL federation batch3 and HTTP batch23 are prepared with
uint/libz, both unused. They will exercise the pending CLI/service trust config
and the shared Python ping-registration refactor after binaries are refreshed.

Federation batch2 PASSED under prove (run5499 exit0, 4.83 seconds): canonical
HTTPS daemon behind the verifying fixture proxy, upstream discovery/PKCE/Basic,
ES256 claims, N:1 role mapping, external projection without local assignments,
downstream code/refresh, evidence-capped expiry, refresh during outage without
changing evidence, local admin login during outage without upstream traffic,
and repeat external login reusing identity/updating evidence. Batch2 consumed.
This does not yet prove native ping federation, stale-evidence denial or Okta.
Next pending batch adds optional caPath to zum/zumpingd config and their native
TLS transports, plus real admin CLI and ping clients to the federation fixture.
Python fixture registration code is shared with the existing local ping test;
downstream apps get no upstream provider/role-map configuration. Build9160 remains
active; it already passed zum/src before the new zum.cc config change, so that
change requires a later ordinary source build. No duplicate build started.

Native upstream TLS probe PASSED: ran current ZumUpstreamTest under prove with
ZUM_UPSTREAM_TEST_URL pointing to independent fixture discovery and
ZUM_UPSTREAM_TEST_CA pointing to its private CA. Both test cases passed (50ms),
and the provider observed exactly one discovery GET. Native selected TLS/H1.
This verifies the C++ transport's configured CA path and successful HTTPS, not
the daemon node-config route or the full federation flow. Temporary probe files
were removed after the provider stopped. Build9160 remains live in zum/src.

Focused HTTP rerun8350 PASSED all79 tests in six binaries (2.3 seconds), including
the previously failing ClientPool heap-baseline assertions. Build9160 remains live.
Source review found the first federation fixture's HTTP canonical issuer cannot
satisfy the existing HTTPS upstream-callback validation. Added a fixture-only TLS
reverse proxy and TLSFixture connection override; browser traffic verifies the
same ephemeral private CA, and zumd is configured with its HTTPS issuer. No
production TLS check was weakened. Python AST/diff checks pass; end-to-end batch2
still awaits the refreshed source executable. No additional build was started.

Build65443 completed exit0. ZhttpClientPoolTest is current (make -q exit0);
the six focused HTTP lifecycle/pool/fixture tests are running again (run8350).
Started build9160: ordinary incremental -k -j3 Zum src, test, example, itest
default targets for the batched shard dispatch and deadline-index fixes. No
dependency header changed since the completed build; no HTTP rebuild requested.
Federation batch2 remains unused pending the refreshed zumd executable.

The pending federation correction batch now also dispatches OIDCUserLoad_'s user
and evidence lookups through each table's native run(0). Evidence refresh changes
indexed deadline1; session use changes indexed idleDeadline2. Declared those keys
in findUpd and added unit checks for repeated projection/evidence refresh plus
lookup through the new evidence/session deadline indexes. All are UNBUILT;
build65443 remains live in zhttp/test, with its earlier Zum stages already passed.
Fresh PostgreSQL zum_federation_20260911_batch2 has uint/libz and is unused.
No new build or runtime test started in this continuation yet.

Build65443 source stages PASSED; dependent targets remain live. Focused federation
batch1 (run97644) FAILED: POST /login closed its connection; private server log
shows RoleTable::find_ assertion invoked(shard), followed by SIGABRT. Role-map
selectRows completes on the store thread, not the role table's owner shard.
AuthRouteLoad_ now dispatches policy/provider/role finds with native table run(0)
continuations; source edit is UNBUILT because this build already passed zum/src.
No extra build started. Fixture teardown now preserves the original protocol
failure if the server has already crashed. Batch1 is consumed; credentials and
diagnostics remain private in /tmp/zum-federation-2ygy_sfg. No runtime test is live.
The independent provider self-check also PASSED wrong Basic/PKCE rejection,
ES256 token signature validation and code replay rejection. This is fixture
validation only; full Zum federation remains unverified.

Build65443 is active: ordinary -k -j3 source dependency order zhttp/src,
zrest/src, zum/src, then Zum unit/example/itest and zhttp/test. First two stages
passed; Zum source compilation is live. Fresh PostgreSQL
zum_federation_20260911_batch1 has uint/libz and remains unused. Python AST and
diff checks pass. Independent Python fixture sanity check initially rejected its
certificate for missing Authority Key Identifier; added standard CA key usage,
SKI/AKI and leaf AKI. Recheck PASSED: default trust rejects private CA, explicit
CA accepts discovery, authorization returns a state-bound callback. This checks
the fixture itself, not Zum's new CA path or federation implementation.

Build36001 completed exit0. Focused HTTP tests (run7030) passed HubLifecycle,
ClientCancel, QPackDynamic, H3Push and AltSvc; ClientPool failed3 heap-baseline
checks (one allocation retained after final). Hubs::final_ now releases its entry
array rather than merely clearing length; this correction is not yet verified.
The same pending source batch wires optional non-secret upstream.caPath node
configuration into native H2Config TLS trust, preserving system trust by default.
Added independent local TLS/OIDC provider fixture and focused PostgreSQL federation
driver: discovery, PKCE/Basic, ES256 claims, N:1 mapping, projection, token/refresh,
local-admin outage checks. Uses its own ZUM_FEDERATION_CONNECT fresh DB. These
new federation checks are UNBUILT/UNRUN and do not establish Okta interoperability.
zum3.md and management/itest docs describe the trust configuration and test scope.

New zumupstreamtest PASSED all3 cases (run93483 exit0, 60.05 seconds): one-slot
origin eviction, saturation/cancellation, startup drain. Four refused connections
consume the configured15-second request timeout each; quiet prove output was
mistaken for a suspected hang. Attach was denied by ptrace policy. The original
test completed successfully before an attempted SIGTERM (ESRCH; nothing killed).
An unnecessary debugger-launched reproduction was then interrupted and closed;
it provides no additional acceptance evidence. No DB is involved in this test.
Build36001 remains active in zhttp/test; no runtime test remains live.

HTTP batch22 PASSED (run48238 exit0, 23 seconds) against the refreshed server
and examples. Build36001 resumed only zum/itest then zhttp/test using ordinary
-k -j3 after the fixture-only correction; itest passed, zhttp/test remains live.
Real-origin lifecycle test run is now started. Stop failure pressure-test:
the concrete TCP/TLS engines used by Upstream report stopped(true) after their
native drains, and Pool Tx completion also reports true. Generic ZmEngine's
false-stop transition does not justify a new application recovery mechanism.
No source change or extra rebuild was introduced for that hypothetical path.

Build41229 terminated exit2 in zum/itest: the new test chained name().isolated(),
but name() returns base ZmThreadParams. Corrected the three fixture lambdas to
set isolation and name separately. Source, unit and example stages passed;
zhttp/test stage did not run. Rebuilt Zum unit tests PASSED (run92135, 28 tests,
1.97 seconds). HTTP batch22 is now running (run48238) against refreshed examples.
An extra HubLifecycle execution passed11 tests but make -q was1, so that run is
not evidence of a refreshed test target. zum3.md now states the native asynchronous
start/stop and nonblocking finalization contract explicitly.

Build41229 is active: ordinary incremental -k -j3 default targets in order
zhttp/src, zrest/src, zum/src, zum/test, zum/example, zum/itest, zhttp/test.
The three source stages passed; zum/test compilation remains live. No runtime
test started in this pass; batch22 remains unused. The native ZmEngine stop
failure path returns to Running, so upstream eviction must reject failure before
finalizing its client; shutdown failure handling also needs review. Do not claim
the lifecycle batch accepted from the source-stage build alone.

Build69151 is terminal exit2 at zhttp/test. Earlier zhttp/src, zrest/src, zum/src
stages passed; later Zum test/example/itest stages did not execute. The recorded
RxQueue, export and wrapper-warning corrections are applied but unverified.
No build/test process is live. HTTP batch22 remains unused; new zumupstreamtest
has not been compiled or run. Preserve the positive nested-controller result,
but do not claim lifecycle batch acceptance until all required checks pass.

The rebuilt ZhttpHubLifecycleTest is current (make -q exit0) and PASSED under
prove: 11 tests including nested deferred start/stop (40ms). Corrected both
synchronous start wrappers to explicitly call this->start, addressing Clang's
unused-capture diagnostic. Remaining test build69151 is still active; these
source/test corrections require the next ordinary dependency-ordered pass.

The same build also exposed missing dynamic exports for public out-of-line
AltSvcCursor constructors/next and AltSvcCache::update. Their definitions exist
in libZhttp but nm -D confirms no exported symbols. Added ZhttpAPI only to those
public declarations, leaving private methods/templates hidden. This header fix
requires a subsequent native source rebuild; current make -k still runs against
the preceding library and may report equivalent link failures. No restart/new
build yet. RxQueue fixture fixes and this export fix are pending verification.

Build69151 remains live under make -k in zhttp/test. It found two fixture type
mismatches in ZhttpQPackDynamicTest/ZhttpH3PushTest: private derived receive nodes
cannot accept the current parsers' native ZiRxQueue nodes. Replaced only those
test-private queue declarations with the native ZiRxQueue type, retaining their
allocator tuning. These fixes are not yet compiled (make already marked those
targets failed). Let the active build finish collecting diagnostics; its later
Zum stages will not run after this failed stage. No duplicate build started.

Build69151 zum/src stage passed and zhttp/test default targets are now compiling.
The startup-wrapper warning remains outstanding. Do not run the full HTTP fixture
with stale example binaries: this batch changes their client-template headers;
wait for zum/example later in this same build chain. HTTP batch22 remains unused.

Prepared fresh PostgreSQL zum_http_20260911_batch22 with uint/libz for the
post-lifecycle HTTP regression; it is unused. Build69151 remains live in zum/src.
No additional build or runtime test started.

Build69151 zhttp/src and zrest/src stages passed; zum/src is compiling. Clang
reported an unused-this capture warning in the new Client::start synchronous
wrapper (ZhttpClient.hh:7363); use explicit this->start in both Pool/Client wrappers
in the next coherent correction, without restarting this live build. No errors
reported yet. Source review for later federation fixture: Ztls loads a configured
CA path or system certificate directory, not SSL_CERT_FILE. Zum upstream transport
currently does not expose caPath. A private-CA node configuration is needed for
local TLS federation tests/private IdPs; do not modify system trust or disable TLS
verification. These are pending items, not verified provider support.

Started build69151: ordinary -k -j3 default targets, sequentially zhttp/src,
zrest/src, zum/src, zhttp/test, zum/test, zum/example, zum/itest. Initial native
transport compilation is live. No runtime tests started; do not duplicate or
restart this build based on an observation timeout.

Added zum/itest/zumupstreamtest.cc and its normal Makefile.am target. It uses
reserved loopback sockets (refused connections or held TLS handshake) to exercise
one-slot origin eviction, saturation503, cancellation, owner-shard callbacks and
immediate shutdown after submission, without external credentials/certificates or
DB setup. It does not test successful TLS or federation. Audited affected Zum CLI,
PingHTTP/zumping, zhttp utility/fallback and zrest example callers: their normal
and failure paths explicitly stop before finalization. Existing native hub tests
also await stop or finalize never-started initialization. No compatibility wrapper
was added. Current lifecycle/itest batch remains UNBUILT; diff checks pass. No
process live. Next build must refresh zhttp/src then zrest/src then zum/src before
affected test/example targets, using ordinary incremental -j3 only.

Continued the unbuilt upstream lifecycle batch: Client pools now register with
the existing Zhttp::Hubs controller through add() after successful initialization;
Hubs registration is factored from init(), preserving failure isolation. Removed
ClientStopState and m_started; Client/Pool expose start(done) using native Hubs
semantics, with synchronous start retained only as the main-thread wrapper.
Borrowed resolvers must already be running. Zum initializes the resolver on main,
initializes origin clients without starting them synchronously, and dispatches
requests on its owner shard after native asynchronous startup completes. Pending
requests remain counted through startup; shutdown suppresses their dispatch.
Added nested-controller deferred start/stop regression. Diff checks pass; no
build/test is live. Caller audit and real-origin lifecycle tests remain pending
before the next dependency-ordered build. No new app-level lifecycle engine.

Build4310 is terminal exit0. PostgreSQL restart batch2 PASSED via
ZUM_TEST_MODULE/ZUM_TEST_CONNECT prove -j1 zumrestarttest (run69605 exit0,
11 tests, 96.2 seconds). All current HTTP/unit/restart regression runs passed.
No build/test is live.

Started the next source batch for upstream HTTP lifecycle: removed implicit
blocking stop from Zhttp Hubs/Pool/Client finalization; final now requires native
stop completion (or never-started initialization), matching TCP/TLS finalization.
Added quiescence assertions and documented external resolver ownership for
worker-finalized clients. Zum eviction already posts after native stop completion;
updated its stale blocking comment. The external-resolver lifecycle test now
posts client finalization onto Rx after stop, detecting the prior self-blocking
second shutdown. UNBUILT; finish caller audit and async-start work before rebuilding
the required zhttp/zrest/Zum dependency targets. No new lifecycle engine or app
locking was introduced. Actual upstream origin/federation validation remains open.

Build4310 unit and example stages passed; integration compilation remains active.
prove -j1 ZumTest ZumClientTest ZumUpstreamTest PASSED (run28511 exit0,
3 binaries/28 tests). Includes all public-byte JSON regressions. No external
ZUM_UPSTREAM_TEST_URL was supplied, so that upstream test is not federation or
real-origin lifecycle evidence. PostgreSQL restart batch2 remains unused.

HTTP batch21/session24490 PASSED (exit0): observed direct HTTP success coverage
is now68/68 administrative operations. Includes optional consent/grant selectors,
exact grant ID query-to-revoke and zero-effect repeat, bounded cleanup, credential
queries/update/state/recovery, auth-policy replacement, role-map deletion, signing
retirement, and real CLI/ping/refresh/restart/role-removal-denial/restoration flow.
This is success coverage plus the fixture's specific negative checks, not the
full per-operation invalid/forbidden/redaction matrix or provider completion.
Build4310 source stage passed; dependent unit compilation remains live. Prepared
fresh zum_restart_20260911_batch2 with uint/libz, unused, for the refreshed restart
fixture after the build's integration stage. No HTTP test live; batch21 consumed.

Started build4310: ordinary incremental -j3 source, unit, example and integration
default targets in dependency order, validating the complete public-byte encoding
and optional-selector/exact-revoke fixes. Fresh PostgreSQL
zum_http_20260911_batch21 has uint/libz and is unused. No runtime test live.

Extended the pending metadata/revocation batch: unit JSON contract checks now
cover all five public byte fields; HTTP uses a queried grant ID for exact
revocation, checks only that grant changes, then repeats under a fresh request
key and expects zero new effects. Removed the hard-coded m_removed=1 from the
exact selector: it now uses Revoke::count(), like the bounded path, so an already
revoked grant correctly reports zero. No replay guard or saga framework change.
AST/diff checks pass; all current source edits remain unbuilt. No process live.

Build13451 is terminal exit0 (source/unit/example/integration). Rebuilt ZumTest
also passes under prove (22 tests, run73576 exit0). No build/test process live.
With that build finished, applied the remaining public-byte JSON::Base64URL
annotations to App.catalogDigest, User.handle, Grant.id and Grant.credentialID.
These and the daemon optional-selector default fixes are unbuilt; batch further
coherent source changes before another incremental build.

Build13451 unit and example stages passed; integration compilation is live.
Rebuilt ZumTest run39111 exited0, including the credential base64url regression.
The daemon selector-default corrections remain unbuilt and must be included in
the next source batch with the other public-byte metadata corrections.

Build13451 source stage PASSED; dependent unit compilation remains active.
HTTP batch19/session19470 stopped at signKeyRetire because the new fixture
omitted its required Idempotency-Key. Fixed the fixture and ran fresh batch20,
session47532: signing retirement, real-client role-removal denial/restoration,
auth-policy replacement, role-map deletion, credential exact/group queries,
credential update/state, user recovery/retry and session revocations all passed.
It then failed consent revocation (expected1, got0). PostgreSQL inspection shows
the user's consent remains Active/unowned; session records were all revoked.
Root cause: adminBody uses framework ctor(), so optional numeric fields without
metadata defaults receive null sentinels, not struct member initializers. Added
explicit zero defaults to ConsentSelector app/audience IDs, GrantSelector user/app
IDs, CleanupInput.before and AuthPolicyInput.providerID. Daemon edits are UNBUILT;
do not start a competing build. Added collection name to count-failure diagnostics.
Failed private artifacts: batch19 /tmp/zum-http-vj6v2p6t;
batch20 /tmp/zum-http-nesf4fjl. No HTTP test live. Zum data tables are public tables
with dotted names (e.g. public."zum.consent"); zdb owns its separate native schema.

Added real zumping authority-removal coverage while build13451 runs: the virtual
browser completes authorization, then a callback hook uses the real zum CLI to
remove the user's ping role before delivering the code. Require client exit1 and
no pong; restore the role via current ETag and require a new login/two-pong flow.
No production change or new build. AST/diff checks pass; runtime pending batch19.

Source review found the credential encoding issue also affects public
App.catalogDigest, User.handle, Grant.id and Grant.credentialID: their JSON
metadata still defaults to standard base64, contrary to zum3.md's byte-string
base64url contract. Exact grant revocation already decodes base64url, making
query-to-revoke interoperability incorrect. These need native JSON::Base64URL
annotations and query-to-exact-revoke coverage in the next coherent source batch;
do not modify the common header midway through build13451 or claim its current
credential-only correction resolves the whole byte-string contract.

Build13451 remains live (source compilation), with no reported diagnostics.
Added bounded grant-cleanup HTTP checks without a C++ edit/rebuild: reject missing
request key, zero limit and caller cutoff; verify removed count <=1, removed rows
expired and surviving rows unchanged. AST and diff checks pass; runtime pending.
Reconfirmed upstream lifecycle gap in source: Zhttp::Client::final calls each
Pool::final, which unconditionally calls blocking stop while initialized, even
after Zum's asynchronous eviction stop. Worker-shard eviction must not use that
path. Hubs already exposes continuation-based start/stop; Pool/Client currently
expose synchronous start. No framework lifecycle change made in this batch.

While build13451 runs, added signing-key retirement HTTP checks: required/current
ETag, future verification retention, Suspended state and version increment,
continued verification of an old token, stale retry rejection. Python AST and
git diff --check pass; runtime verification remains pending on batch19.

Build59731 passed. PostgreSQL HTTP batch18/session7785 failed at exact
credential-ID lookup after the user-group query succeeded: Cred JSON defaulted
to standard base64, whereas credential route IDs use base64url. Corrected the
Cred ID JSON field with native JSON::Base64URL metadata; added a unit regression
covering URL alphabet and unpadded output. Added auth-policy replacement and
role-map deletion HTTP checks with missing/stale ETags and persisted results.
Batch18 is consumed; diagnostics /tmp/zum-http-xqk03403 retained privately.
Build13451 is running ordinary incremental -j3 source, unit, example, integration
targets in dependency order. No runtime test is live; the new tests and metadata
correction are pending verification. Fresh PostgreSQL batch19 has uint/libz and
is unused. The full plan remains incomplete.

While build59731 runs, added bounded sessions/consents/grants revocation HTTP
cases for the disposable ping user: missing idempotency key and zero limit
rejection, exactly one effect with limit1, unrelated users unchanged, remaining
effects and then zero work. Tests are syntax-checked, not yet run. Documented
credential exact-ID/group-user query contract and its cursor binding. Prepared
fresh PostgreSQL zum_http_20260911_batch18 with uint/libz; unused.

Started build59731: make -C zum/src -k -j3 for credential query support. Initial
daemon compilation is live without diagnostics; do not duplicate it.

Added user/credential lifecycle and recovery HTTP checks. Batch17/session27848
failed at GET /admin/credentials?userID=...400 after userInvite and userUpdate
success. Found credentialQuery allowed only pagination despite native primary
credential-ID and grouped user-ID indexes. Added exact base64url ID lookup and
native index1 user-group query, with user-bound cursors and repeated userID filter
on continuation. Kept cursor+exact-ID and mixed ID/userID invalid, preserving
other routes' existing filter restrictions. Added malformed/mixed/exact/group
query checks. Changes are unbuilt; batch17 consumed; diagnostics
/tmp/zum-http-e3eetav_. No test/build live yet.

HTTP batch16/session53445 PASSED (exit0), including new definition lifecycle
coverage and the full CLI/ping/refresh/restart flow. Direct HTTP successful
operation coverage increased from47 to56 of68: audienceAdd/Update/State,
roleUpdate/State, scopeState, clientState, providerUpdate/State now exercised
successfully with missing/stale ETags, version increments and unchanged-state
checks. CLI userInvite success remains outside this direct-HTTP counter. Still
uncovered directly: authPolicySet, consentRevoke, credentialState/Update,
grantCleanup/Revoke, roleMapDelete, sessionRevoke, signKeyRetire,
userInvite/Recover/Update. No C++ rebuild was performed. No process live.

PostgreSQL restart run91505 PASSED (exit0) all11 top-level cases on fresh
zum_restart_20260911_batch1 with uint/libz. Command: ZUM_TEST_MODULE pointing to
zdb_pq/src/.libs/libZdbPQ.so and ZUM_TEST_CONNECT='host=/tmp dbname=zum_restart_20260911_batch1'
./zumrestarttest from zum/itest. Includes basic restart, grant metadata persistence,
action saga recovery, membership recovery, role-removal completion/rollback,
catalog completion/rollback, and all32 catalog-boundary scenarios. These stage
durable intent/effect images and drain native Zdb shutdown before reopen; they
are not process-kill or cluster failover evidence. No build or test live.

Build15185 is terminal exit0: source and integration default targets pass without
diagnostics. No build or runtime test is currently live. The rebuilt PostgreSQL
restart-test executable has not yet been rerun.

HTTP batch15/session2618 PASSED the full expanded fixture (exit0). The unwanted
form '?' correction is runtime-verified: direct /service/authorize succeeds,
and real zumping authenticates through real zumpingd to zumd, receives pong,
rotates its refresh token, and receives pong again. CLI-enrolled user has only
ping app membership/role and no core membership. Repeat client login after
graceful zumpingd restart also passes, with unchanged catalog records. All prior
HTTP/admin CLI/signing-key/restart cases still pass. Direct HTTP success count
remains47/68 and omits CLI successes; this is not full68-operation coverage.
Browser exchanges use virtual WebAuthn, not a real platform authenticator.
Build15185 passed source and remains in integration compilation; no HTTP test
is live. Batch15 is consumed. Full objective remains incomplete (upstream
lifecycle/federation, complete endpoint matrix, remaining acceptance gates).

Prepared fresh PostgreSQL zum_http_20260911_batch15 from template0 with uint/libz.
No Zum process has used it. Build15185 remains live compiling the daemon without
diagnostics; the HTTP fixture must wait for its source stage to finish.

Started build15185: make -C zum/src -k -j3 followed by make -C zum/itest -k -j3.
Only daemon source and integration build inputs changed; libraries/unit/example
objects are unchanged. Initial compile is live without diagnostics; do not duplicate.

HTTP batch14/session98545 ended exit1 with direct POST /service/authorize400
invalid_request, using a fresh service token and the same ping client. This
locates the failure in zumd, not zumpingd parsing or cached workload credentials.
Source cause: serviceField prepended '?' to an internal OAuth form; parseAuthorize
does not strip URL delimiters and therefore never recognized its first client_id.
Removed that prefix and replaced hand-written serviceEscape with ZfURI::PathQuote.
The added direct facade HTTP check remains as a regression before actual zumping.
Correction is unbuilt; batch14 consumed; diagnostics /tmp/zum-http-50sv9z4m.

Build16150 is terminal exit2: source/unit/example stages passed, integration
failed because ZumMgmt.hh now needs the zhttp include directory. Added that
directory to itest/Makefile.am; let ordinary make regenerate it. No build live.
Unit run53150 passed all22 subtests, including all501 enrollmentRuntime checks.
HTTP batch11/session10186 PASSED the then-current full fixture, including CLI
login/query/invite/enrollment/membership/roles, consent, signing rotation, wrong
key rejection, and zumpingd catalog/startup/restart; direct HTTP success report
was47/68 (CLI successes are not recorded in that counter).
Extended the fixture with real zumping: enroll user/passkey with only ping role,
register public client and access, drive browser redirects and callback, require
two pongs around refresh rotation, repeat after service restart. Batch12/session16064
failed at the initial service redirect; batch13/session95460 narrows this to
HTTP400 authorization_failed (not query parsing invalid_request). Diagnostics:
/tmp/zum-http-ucy4n9j_ and /tmp/zum-http-ufisf741. The issue is within libZum's
authorize call or zumd's facade path; cause not yet established. Both runs
completed earlier HTTP/CLI/signing checks and user/client/role setup before this.
No runtime test live; batch11–13 consumed. No full end-to-end ping pass yet.

Added a real zumpingd startup/restart scenario after the existing HTTP cases.
It enrolls through zum, injects only the service secret (explicitly strips DB
test/connection/key variables), waits for the listener event, checks /ping401,
and verifies exactly one ping action/role/scope with unchanged records after a
graceful service restart. README records the example executable dependency and
the still-missing actual zumping/user-assignment flow. Python AST and diff checks
pass; the scenario has not run. Build16150 remains the only build; batch11 unused.

While build16150 runs, expanded the HTTP fixture to use the actual zum CLI for
appEnroll, userInvite, membershipAdd and membershipRoles, as well as login/query.
CLI secret operations require exact file-only stdout receipts and0600 delivery
files; the existing direct HTTP authority/version/retry checks remain. These
are test-code additions, not runtime passes. AST parsing and git diff --check
pass. PostgreSQL HTTP batch11 remains unused pending refreshed source binaries.

Started build16150 for the collected collision, CLI file-write and integration
include fixes: source, unit, example, integration default targets in dependency
order, each -k -j3. Initial source compilation is live without diagnostics.
Do not restart or duplicate this build. HTTP batch11 remains unused.

Build70254 is terminal exit2: source, unit and example stages passed; both
zumping and zumpingd now link. Integration compilation failed only on missing
ZumMgmt.hh (MgmtOp uses) in zumrestarttest.cc; added the direct include.
Unit run6328 completed normally (exit1), with 21/22 subtests passing. Revoke and
GrantCleanup rollback regressions pass, as do the earlier credential/family
collision fixes. Remaining failures cluster around UserInvite/RecoveryStart
capability collisions and ConsentCode creation over an existing consent.
Added business duplicate detection to those native insert mutation callbacks;
Zdb still suppresses callbacks on replay. These header fixes and the CLI file
write status correction are unbuilt. ZumUpstreamTest and ZumClientTest both
passed in a separate run, without external upstream configuration; that does
not verify actual origin creation/eviction or federation. Replaced zumpingd's
hand-written percent decoder with ZuPercent before its example compile.
Prepared empty PostgreSQL zum_http_20260911_batch11 with uint/libz; unused.

Build70254 passed source compilation and is still live in dependent builds.
HTTP batch9/session22762 ended exit1 because the new CLI fixture assumed every
authorize request renders a passkey page; an existing session correctly returned
302. The shared fixture now accepts that redirect, and clears browser cookies
before the CLI scenario to exercise a fresh passkey login. Batch10/session89426
then reached CLI callback/code redemption and wrote its credential file, but
the CLI returned failure: writeProtected compared ZiFile::write's status with
the byte length instead of Zi::OK. Corrected zum.cc; this change is not yet
built. Both databases are consumed; no runtime test remains live. Failure
diagnostics are /tmp/zum-http-mr5sqm_n and /tmp/zum-http-dvszzzc3 respectively.
The source build must be refreshed after build70254 finishes; do not duplicate it.

Build70254 remains live in source compilation without diagnostics. Prepared
fresh PostgreSQL zum_http_20260911_batch9 from template0 with uint/libz; no Zum
process has used it yet. Added actual admin CLI login/query coverage to the HTTP
fixture: virtual WebAuthn browser interaction, real CLI PKCE/code redemption and
loopback callback, protected credential file, and a second process querying the
core application. Extracted the existing authorization-page exchange for reuse
without changing token/refresh checks. Python AST parsing and git diff --check
pass; this new CLI scenario has not run. Upstream lifecycle inspection also found
blocking Pool::start via Hubs::start, in addition to redundant stop in final;
fixing eviction alone will not make runtime origin creation nonblocking.

Started validation build70254 after the above source/test batch:
`make -C zum/src -k -j3 && make -C zum/test -k -j3 && make -C zum/example -k -j3 && make -C zum/itest -k -j3`.
Initial source compilation is live without diagnostics; do not duplicate it.

Continuation 2026-09-11: build15686 is terminal exit2. Source and unit binaries
built; zumping linked; the example stage failed at two untyped empty response
arguments in PingHTTP.cc, now replaced with ServiceHTTPResponse{}. The integration
build stage did not run. Unit run48207 passed CredentialAdd collision checks120/121
but stalled again at Revoke; the confirmed test PID1259576 was terminated (exit143).
Native saga update of an absent row takes the framework Missing path, not the
application mutation callback. Revoke now reads the row in its shard continuation
and rejects absence before invoking native saga update. This is a business
precondition, not an application replay mechanism. GrantCleanup's rollback test
now uses a changed, existing grant (and verifies its preservation); absent deletion
is explicitly tested as success, matching native Zdb deletion semantics.
These fixes and the consent shard dispatch correction require build/runtime
verification. Upstream eviction finalization still blocks its owner shard and
remains unresolved. No full test suite or end-to-end CLI ping pass is claimed.

Build15686 passed its source stage and remains live in dependent builds.
HTTP batch8/session69505 is terminal failure after substantially more coverage:
startup/enrollment/admin login, removed audit404, app enrollment/idempotency,
catalog publication and restart checks completed before application consent
approval aborted. Private diagnostics /tmp/zum-http-b64h6tok show Zdb AppTable
invoked(shard) assertion at ZumConsent.hh:82. Wrapped ConsentCode validation's
App/User/Client reads in each table's run(0) continuation. This header correction
is unbuilt and will require a later source refresh; do not restart build15686.
Batch8 is consumed. git diff --check passes. Full HTTP suite is not green.

Prepared empty PostgreSQL zum_http_20260911_batch8 with uint/libz; no Zum process
has used it yet. Build15686 remains live without diagnostics. Clarified the
initial ES256 signing-key wire contract in zum3.md: JSON-string publicJwk and
base64url raw32-byte P-256 private scalar, pair validation, encrypted storage and
no private/ciphertext output. This documents the actual backend contract rather
than the misleading DER assumption from earlier testing.

Build15686 remains live without diagnostics. RecoveryEnroll already has a
duplicate-credential rollback fixture near rollbackRecovery in ZumTest.cc;
strengthened it with an exact SagaImage comparison of the existing credential
before/after rejection, not merely a failure result. This test edit precedes
the pending unit compilation stage. git diff --check passes; it is not a test
pass yet. Source/header edits are otherwise frozen for the running batch.

Extended the native mutation-callback duplicate decision to RecoveryEnroll's
credential insert and CodeFamily's refresh-family insert. Existing CodeFamily
collision regression expects code before-image restoration and preservation of
the unrelated family; recovery-credential collision coverage remains to be
verified. No application replay journal/guard was introduced. Started a sequential
source/unit/example/integration build, each `make -C zum/DIR -k -j3`, connected
with && so dependency failures stop dependent stages. Live session is stored in
zumValidationBuildSession, full output in zumValidationBuildLog. This includes
the pending404, CredentialAdd and Revoke fixes; no new runtime result yet.

Build44586 completed exit0 without diagnostics. Direct ZumTest run19204
confirmed jwt and enrollmentSaga now pass, and enrollmentRuntime checks112–118
confirm both Enrollment collision rollback cases now pass. It then failed
CredentialAdd duplicate checks120/121 and stalled in Revoke rollback. Stopped
the confirmed test PID1253805 (terminal143). Added the same mutation-callback
duplicate decision to CredentialAdd. Changed Revoke's missing-row branch to
complete(!Fwd): forward missing-row rejection remains, while rollback of a row
that was never present succeeds rather than retrying forever. Changes are
unbuilt/unrun; no process/build is live. The unknown-route404 fix remains in
the same pending source batch. git diff --check passes.

Build44586 completed its source stage successfully and is still compiling units.
HTTP batch7 progressed through listener startup, bootstrap WebAuthn enrollment,
Ready and administrator OAuth login, then failed at the removed /admin/audit
route: expected404, got400. This provides runtime evidence that owned public-key
output fixed startup/key matching. Changed the unknown administrative-route
branch to AdminNotFound/not_found, retaining405 for known paths with unsupported
methods and400 for malformed parsed requests. This routing correction is not
compiled yet; do not restart the live unit build. HTTP session83039 is terminal
failure; private diagnostics /tmp/zum-http-lrm2wwe3. Batch7 is now consumed.

Prepared fresh zum_http_20260911_batch7 from template0 with uint/libz for the
next HTTP run; no Zum process has used it yet. Build44586 remains live and has
completed the daemon main, administrative and authorization objects without
new diagnostics. Do not reuse consumed batch1–6 for fresh-key fixtures.

HTTP fixture failure handling now preserves the original exception and retains
its mkdtemp-created private diagnostic directory on failure, printing only the
path. Successful runs still remove their temporary artifacts. The DB encryption
key is never written there; a failed database remains consumed for fresh-key
runs. README updated; Python AST parsing and git diff --check pass. Build44586
remains live without diagnostics; do not restart it.

Started source-then-unit validation batch 44586:
`make -C zum/src -k -j3 && make -C zum/test -k -j3`.
This includes the owned public-key output buffer, Enrollment duplicate business
checks, and their formatting cleanup. Log/session are retained in
zumValidationBuildLog/zumValidationBuildSession. The build is live and has no
diagnostics so far. No runtime test is active; git diff --check passes.

Detailed unit run 15277 was confirmed stalled after enrollmentRuntime check115
and terminated (exit143); no test process remains live. Source inspection found
the collision tests assumed Zdb insert rejects primary-key duplicates, contrary
to its documented idempotent insert contract. Added Enrollment user/credential
duplicate lookups in the step body, passing only the duplicate decision into
the native mutation callback. Rejection is evaluated there, so Zdb's suppression
of an already-applied callback still owns replay idempotency. Existing rows are
not changed on rejection. This is business uniqueness validation, not an app
effect/replay guard. Changes are unbuilt/unrun; investigate any remaining stall
after these rollback preconditions are restored. Public-key output fix also
remains pending compilation. git diff --check passes. No build is live.

Unit run 95251 is terminal failure: ZumTest stalled in enrollmentRuntime and
was terminated after confirming its PID. Harness then completed:
ZumUpstreamTest and ZumClientTest passed, ZumTest failed jwt/enrollmentSaga and
had an incomplete plan due to termination. Direct detailed ZumTest run 15277
is still live; output retained as zumDetailedTestLog. It reports JWT record
verification failures, signKeyPublic/key matching failures, then enrollment
collision rollback failures at checks112/113 and stalls after check115.
Do not start a duplicate runtime test until that process is handled.
Debug batch6 confirmed secret decryption and private-key import succeed;
loadKey_ rejects at signKeyMatch. Fixed signKeyPublic's local-array result
construction by allocating its owned output first and decoding directly into
it (no stack-array shadow/copy). This correction is unbuilt/unrun. Batch6 is
consumed; diagnostic directory /tmp/zum-gdb-8i150vcn. No build is live.

Validation build 78977 completed exit 0 without diagnostics (source and units).
`make -C zum/test test` is live in session 95251; output in zumUnitRunLog.
Fresh HTTP batch3 still failed before listening. Debug batches4/5 reached the
main-thread signing-key check without the previous shard assertion, proving
that dispatch fix is effective. The selected key has Active state/version1;
loadKey_ nevertheless returns false. Decryption/import/public-key matching still
need isolation; do not claim initialization is fixed. Debug processes are
terminal; databases batch3/4/5 are consumed. Private diagnostic directories:
/tmp/zum-gdb-0xzsv287 and /tmp/zum-gdb-gnrs_2pg.
Backend inspection corrected a misleading SK_EC constructor comment assumption:
pkey_ec_import_private consumes a big-endian scalar, not DER. Updated the HTTP
signing-key fixture to supply its 32-byte P-256 scalar. Earlier ledger statements
claiming DER alignment were incorrect; this fixture remains unrun.

Fresh database zum_http_20260911_batch2 (uint/libz installed) reproduced the
startup failure under `libtool exec gdb`. Exact cause: SignKeyLoad_ receives
selectRows results on the PostgreSQL store thread and called find<0> there,
triggering Zdb's invoked(shard) assertion at Zdb.hh:2302. Wrapped the follow-up
find in signKeys->run(0, continuation), preserving native shard ownership.
The debugging process is terminal; batch2 is consumed (ephemeral key not saved).
Private diagnostic directory: /tmp/zum-gdb-jisn069b. No key material was printed.
Started sequential `make -C zum/src -j3 && make -C zum/test -k -j3`; live handle
is in zumValidationBuildSession and retained output in zumValidationBuildLog.
This validates both the startup dispatch fix and the pending test null() fixes;
neither is yet verified by runtime tests.

Unit build 67168 is terminal exit 2: eight diagnostics from seven ambiguous
String assignments using `= {}` in ZumTest.cc. Replaced these with native
null(); changes are uncompiled. ZumUpstreamTest and ZumClientTest build results
are available in zumTestBuildLog; do not call the whole unit suite passed.
Ran the existing unchanged zumhttptest driver against zum_http_20260911_batch1:
failed before listener startup (TAP failure). A diagnostic startup reproduction
also failed and retained logs in /tmp/zum-startup-jh0uo9ci (private directory).
Log shows Zdb activation followed by `HTTP server start failed` and orderly DB
stop. Exact initialization failure is not diagnosed yet. Important: the query
for schema `zum` returning zero tables was NOT proof of an untouched database;
Zdb stores its tables under its own namespace. Both fixture attempts generated
different ephemeral encryption keys. Treat batch1 as consumed; do not reuse it
with another fresh key to diagnose the original failure. Use a fresh disposable
DB and retain that attempt's key only in process memory/environment for debugging.
No processes/build sessions from this continuation remain live.

Source build 23296 completed successfully (exit 0), with no warnings/errors
in the retained diagnostic log. This verifies compilation/linkage of the current
zum/src batch, not runtime gates. Started `make -C zum/test -k -j3`; its live
session is stored as zumTestBuildSession, and diagnostics as zumTestBuildLog.
The source log is retained as zumSourceBuildLog. No runtime tests have run yet.

Source batch 23296 is live (`make -C zum/src -k -j3`), rebuilding after the
payload-header corrections. Removed actually unused delete-step captures;
expressed constant-direction owner/validation checks as ordinary constant-folded
conditions, eliminating captures unused only in the constexpr rollback branch.
Consent owner publication uses the same ternary convention as other saga steps.
Strengthened existing ProviderEdit/ClientEdit/UserInvite recovery roundtrips
with timestamp, grant expiry and default overlap assertions. Those tests already
cover role kind/label/state, provider/client field masks, request payloads,
secret overlap and transient-field exclusion; they remain unbuilt/unrun.
git diff --check passes. No fresh runtime acceptance results yet.

Build 12703 completed with only the accessRefs error family; all other source
targets compiled. Follow-up 37280 is also terminal (exit 2): explicit row typing
exposed aggregate reconstruction mismatches in RoleEdit, ProviderEdit, ClientEdit
and UserInvite. Fixed Ctor ordinals without changing schema field ordering;
moved RoleEdit's transient actions bitmap and ClientEdit's error field after
serialized members. These header corrections are uncompiled. Capture warnings
remain at ZumSagas.hh lines 211, 231, 413, 564, 731, 955, 990, 2588, 2687 and
ZumConsent.hh line 193 (pre-edit locations); inspect/suppress legitimate
constexpr-only captures and remove actually unused captures before rebuilding.
No build is live. Current diagnostic text is in orchestration store zumBuildLog.
Created disposable PostgreSQL database zum_http_20260911_batch1 from template0
and installed uint/libz extensions; it is empty and has not run any Zum fixture.
The existing postgres control DB reports a collation-version warning; no shared
database maintenance or alteration was attempted.

Keep-going source build is live in session 12703 (`make -C zum/src -k -j3`).
Output is retained in orchestration store zumBuildLog, with diagnostic lines
displayed separately. The next error batch identifies accessRefs' generic row
callback in ZumSagas.cc: cache dispatch instantiated it with a cache node and
selected Node::data rather than the model. Replaced auto with explicit
ZdbRowRef<T>, deriving T from the table's native model alias. This correction
has not been recompiled; let the current build finish collecting other errors.
Added wrong-ZUM_DB_KEY startup rejection to the HTTP rotation/restart fixture:
the stopped PostgreSQL-backed server must exit nonzero without listening, then
restart normally with the original key. This test remains unrun; Python AST
parsing and git diff --check pass.

Session 73257 is now terminal (exit 2). ZumDaemon, ZumDB, ZumAdmin,
ZumAuthorize and ZumSchema objects compiled. ZumDiscovery failed on three
untyped SignKeyFn arguments; changed them to SignKey{}. The terminal diagnostic
chunk was truncated, so additional errors (including possible ZumSagas errors)
must be collected, not assumed absent. Next source build should collect the
remaining diagnostic batch with ordinary make's keep-going option and retain
full output while displaying only diagnostic lines. No live build remains.
Added HTTP signing-key registration/rotation coverage: DER private material,
mismatched public/private rejection, response redaction, new-key token issuance,
JWKS verification and old-token acceptance before/after a drained PostgreSQL
restart. Python AST parsing and git diff --check pass; runtime cases are unrun.

Dependency validation on 2026-09-11: ordinary `make -C zdb/src -j3` and
`make -C zdb_pq/src -j3` both completed successfully with nothing outstanding.
Resumed the Zum source batch with `make -C zum/src -j3` (execution session
73257); it is still running, not a successful build yet. Corrected the integration
README's stale 70-operation/audit-table description to match the 68 live
operations and ZiLog checks in the current fixture. `git diff --check` passes.
Further source review confirms that upstream origin eviction calls Client::final
on its owner shard, while native Pool::final still calls blocking stop. This
violates the main-thread-only blocking rule even though the owner is non-I/O;
the asynchronous lifecycle integration remains to be corrected and tested.
Added upstream unit cases for empty/hostless/fragment-bearing/cleartext URLs,
checking rejection callbacks run on the configured owner shard, plus a queued
invalid request whose callback must finish before main-thread final returns.
These cases are unbuilt/unrun. The source build remains live in session 73257;
ZumAdmin compilation has completed and ZumAuthorize/ZumDB are compiling. No new
compiler diagnostics have been observed. Do not restart this live build.

Deferred compilation began with normal `make -C zum/src -j3` in the existing
clang-debug tree. Make regenerated its configured Makefile and FlatBuffers
headers normally. The first diagnostics exposed untyped `{}` arguments to
AdminAuthFn; corrected those and equivalent upstream/test callbacks to explicit
Principal/String values. Removed unused captures in the request cancellation
lambdas. This invocation finished with exit 2 at zumd-ZumDaemon.o (two errors and
repeated capture warnings); corrections have not been recompiled. No runtime
tests have run.
Zdb headers are newer than its library objects; refresh required upstream src
dependencies before treating any linked output as verified. Do not immediately
restart the Zum build after individual fixes; collect the diagnostic batch.

Readiness now uses Server::ready under native request admission/deadlines. It
requires completed bootstrap, selects the currently eligible signing key using
the same selection rule/token lifetime as issuance, and calls the configured
signer with a fixed probe digest. Empty-signature failures keep readiness false;
probe signatures are discarded/cleared and never returned over HTTP. Key/digest
storage survives asynchronous signer completion. The daemon rechecks activation
before returning ready. Added inactive-provider readiness coverage; live rotation,
failed signing and readiness deadline scenarios remain unrun/unproved. The
implementation no longer treats bootstrap completion alone as signing readiness.

Service facade calls now enter the same Requests::run boundary as administrative
calls before bearer/key/client/app lookups. Request::complete gates the final
response; capacity/deadline/deactivation failure returns 503 without a second
late reply. The downstream provider operation still uses its existing request
and saga paths. No DB mutation or outcome persistence moved into response
callbacks. Service admission saturation and cancellation acceptance remain
unrun; source edits pass git diff --check.

Server shutdown no longer clears provider callbacks, configuration, signing keys,
and database-secret keys before Zdb stops. Daemon::stop closes native request
admission, cancels pending upstream transactions via Server::stop, and stops
HTTP. Main then drains/finalizes upstream HTTP, waits for native Zdb::stop, and
only afterward finalizes the daemon/provider; the scheduler stays alive until
that work finishes. The HTTP-start failure path uses the same ordering. This
fix relies on existing Zdb/HTTP stop behavior and does not change Requests'
cancellation bookkeeping into a new drain engine. In-flight shutdown and durable
restart acceptance remain unrun; source edits pass git diff --check.

Administrative HTTP operations now enter Requests::run and return through
Request::complete, sharing the provider's configured request deadline. Capacity
rejection, deadline expiry, and deactivation return a 503 envelope; the native
Request suppresses a later competing response. Moved the common readiness,
authentication, operation permission, target delegation, idempotency lookup,
business dispatch and logging pipeline out of the HTTP template into ZumDaemon.cc.
No new persistence layer or completion hooks were added. Source review also
established that Requests cancellation removes admission bookkeeping immediately;
it does not itself prove underlying DB callbacks have drained. Full shutdown
ordering and deactivation tests remain necessary. All edits remain unbuilt/unrun.

Activation review found that readiness, administrative reads, and service-facade
admission could proceed while Requests was inactive. Added activation checks at
admission and asynchronous completion boundaries; inactive results return 503.
UserInfo now enters the existing Requests::run lifecycle and completes through
Request::complete, inheriting bounded admission, deadlines, deactivation
cancellation, and drain semantics rather than doing untracked identity reads.
Added a provider fixture asserting inactive UserInfo rejects before token parsing
and, after reactivation, malformed tokens receive the ordinary bearer error.
These tests are unrun. Full administrative in-flight request ownership/draining
and replicated deactivation acceptance still need verification; activation
checks alone do not establish those guarantees.

zumd now accepts --config FILE with native zdb/mx node settings instead of forcing
every deployment to use hard-coded standalone hosts. Standalone defaults remain
when omitted; module/connect are optional startup arguments when the file supplies
its own store values. Configuration source storage survives the parsed tree's
lifetime. Requests resolve the named shard thread rather than hard-coded SID 5.
The HTTP fixture starts with defaults and subsequently restarts from a distributed
zumd.cf fixture with reordered numeric threads and the same PostgreSQL store.
ZUM_DB_KEY remains environment-only. Actual multi-node activation/replication and
the new configuration restart scenario are unverified; no build/test was run.

Both CLI token-response parsers now require one JSON object, reject duplicate or
mistyped recognized fields, and publish a replacement token set only after full
validation. Partial tokens are cleared on failure; prior caller credentials are
unchanged on rejection and cleared before successful replacement. Bearer type
comparison uses ZuICmp. Browser callbacks no longer dereference a missing parsed
request object on invalid input. These are source-reviewed fixes; CLI malformed-
response/callback runtime regression coverage remains outstanding. Unbuilt/unrun.

Removed the duplicated hand-written percent encoders/decoders from zum and
zumping. Their form/query/path construction now uses ZfURI::PathQuote directly;
callback fields are decoded in owned mutable buffers with ZuPercent::Codec.
zumping still has no libZum dependency. OAuth success/error redirects now use
the same component quoting: literal '+' in state/code no longer becomes a space
when the client decodes the callback. Added special-character redirect assertions.
These changes preserve ordinary parameter spellings and remain unbuilt/unrun.

OAuth Basic client credentials now percent-encode each component before Base64
in both libZum and the upstream OIDC client; zumd decodes both components in
place after splitting the decoded Basic value. This follows RFC 6749 §2.3.1.
Uses ZfURI::PathQuote and ZuPercent::Codec, not an application percent codec;
PathQuote escapes literal colon/plus and represents space as %20. The upstream
form builder uses the same quoting so literal plus signs in login hints, codes,
and POST credentials are preserved. Added special-character Basic parsing and
malformed-escape tests; the signed service-client and upstream exchange fixtures
now require the expected header for a special-character secret. Upstream Basic
temporary Base64 text is cleared after constructing the header. Unbuilt/unrun.

Upstream origin retention is now bounded by `--upstream-origins` (default 32).
Each owner-shard entry tracks pending requests and retirement. Matching origins
are reused; a new origin at capacity asynchronously stops an idle entry and
reuses its slot after native teardown. If all entries are busy/retiring the
request fails with 503; there is no overflow waiting queue or background GC.
Retiring entries retain their capacity slot. Finalization counts their existing
stop callbacks instead of stopping them twice, and pending HTTP completions post
back to the owner shard. Cleanup uses existing Zhttp finalization on a non-I/O
thread. Added zero-capacity rejection to the upstream initialization fixture.
Eviction, capacity contention, and shutdown-during-eviction still require runtime
coverage; none of this batch has been compiled or run. `git diff --check` passes.

Upstream transport lifecycle now explicitly owns its client collection on a
configured non-I/O scheduler shard (production uses Requests::sid). Calls post
to that shard; finalization closes admission there and drains clients using
Zhttp's existing asynchronous stop callbacks. Actual client finalization remains
on the main thread because Zhttp Pool::final still invokes blocking teardown.
The adapter rejects Rx/Tx/invalid shard IDs. Updated the upstream test's scheduler
configuration and its formerly synchronous rejection assertion to continuations.
No new framework hooks or locks were added. Origin eviction/capacity is still
outstanding; this change establishes its lifecycle prerequisite. Unbuilt/unrun.

Service token renewal now checks that the service is still Started before
verifying the renewed token or waking queued operations successfully. Previously
a successful response arriving during stop could advance an operation into the
stopped transport and report Unavailable rather than Stopped. The client-only
signed-token fixture now holds renewal, queues stop, delivers the successful
response, and asserts Stopped with no additional facade request. This uses
scheduler ordering and continuations, not sleeps/polling, and remains unrun.
Upstream HTTP endpoint logging now snapshots IP/port by value instead of retaining
an endpoint pointer in a deferred ZiLog callback. Inspection also confirms that
UpstreamHTTPState retains each encountered origin until finalization; bounded
origin lifecycle management remains outstanding and is not fixed by this edit.

Administrative delegation paths now normalize user actor IDs with the existing
framework integer parser/formatter before lookup and saga submission. Previously
`00123` could be stored as a distinct actor key that authentication's `123` lookup
could never use. Both replacement and state-change paths normalize; client IDs
remain opaque strings. The collection query returns the stored canonical key.
Extended the PostgreSQL HTTP fixture to create a padded-ID delegation, assert
its canonical query projection, suspend using another padded spelling, and
reactivate using the canonical spelling. Runtime execution remains deferred.

Administrative admission now rejects saga-owned client/user/delegation records
and does not use owned memberships or roles to grant core superuser authority.
An unavailable membership still permits checking an independent explicit
app delegation. The HTTP dispatcher already checks the token's core operation
action before target delegation; no additional permission mechanism was added.
Consent reuse and service-facade client/app admission already reject owned rows.
These administrative checks have only been source-reviewed, not runtime-tested.

Session logout previously changed a session even while a saga owned it, allowing
that saga's compensation to overwrite the logout. It now returns Storage without
mutation for an owned row, including an intermediate revoked row. Added native
continuation-based test assertions that reserve an existing session, reject use
and logout, inspect unchanged state/version/deadlines/timestamp, then release the
fixture reservation and exercise ordinary logout. This is a reservation-conflict
fixture, not a saga-recovery or PostgreSQL restart test. All edits remain
unbuilt/unrun; `git diff --check` passes.

Native-operation conversion: explicit consent approval. Source audit of
`AuthorizeConsentFinish_::record_` and `::issue_` confirms separate persistent
writes: consent insert/update commits before authorizationFinish changes the
ceremony into a code. A failure between them leaves a consent without completed
issuance. The handler now submits ConsentCode, registered at catalog index 44,
with native reserve/insert-or-update/publish/release steps. Its admission checks
active app/user/client state and authority versions inside the saga. The old
direct consent writes and post-consent code mutation are removed. Plaintext
code is not in the payload; payload serialization and catalog assertions were
added. No builds/tests have run; runtime rollback and PostgreSQL recovery remain
to be proved. The implementation/review contract is:

Native runtime assertions now cover absent-consent creation, existing-consent
update, stale-version rejection after ceremony reservation, and insertion
collision after reservation. Rejection cases compare serialized full grant and
consent before-images, including ownership, scopes, version and timestamps.
The cases are unrun; rollback after an applied consent mutation and PostgreSQL
crash-boundary recovery still need coverage.
Consent scope merging, lifecycle/version transitions and timestamps now live in
ConsentCode::result, not the HTTP adapter. The payload carries approved scope
IDs rather than a precomputed after-consent row. New approval of revoked consent
does not revive its old scopes; unit assertions distinguish active scope union
from revoked-consent replacement. The same native creation/update/rollback
fixtures now exercise this saga-owned logic. All remain unbuilt/unrun.

- Pass DB through authorizeConsentFinish (its production caller is Server).
- Prepare the opaque code/digest and immutable grant/consent before-images;
  plaintext code stays only in the request adapter and is cleared on all exits.
- Native steps reserve the ceremony, branch between consent insert and update,
  publish the code, and release consent ownership. Validate active app/user
  authority and before-image versions inside the saga; use native skip for the
  absent/present consent branch. Zdb supplies replay idempotency.
- Compensation restores full before-images, including scopes, versions and
  timestamps, and removes a newly inserted consent. No result/audit DB writes
  belong in response callbacks.
- Return the code redirect only after saga completion. Add rollback and native
  recovery assertions for existing and absent consent rows, then PostgreSQL
  crash-boundary coverage when the deferred verification batch begins.

Single-row session touches and refresh-reuse revocation were also located;
those are not the same multi-row atomicity gap. Do not expand this conversion
into an unrelated session or persistence redesign.

Corrected upstream issuer-path discovery: OIDC appends the well-known suffix to
the issuer, so `/oauth2/default` remains before that suffix. The previous code
incorrectly inserted the well-known component before the issuer path. Updated
the mocked upstream exchange to require the OIDC URL. Verified the construction
rule against OIDC Discovery §4; the implementation/test edits are unbuilt/unrun.

Local-first route review found and removed a daemon-only rejection of upstream
providers configured for UserInfo claims. Both IDToken and UserInfo sources now
reach the existing discovery/exchange implementation. Existing local identities,
including disabled/suspended ones, still never fall through to upstream SSO.
The normalized entered login is forwarded as an encoded upstream login_hint;
identity resolution still uses verified issuer/subject, not that hint. Added an
encoded-hint assertion to the upstream UserInfo fixture. Live routed UserInfo
SSO remains unverified; no rebuild/test run.

Fixed JSON tree ownership in `ZumService.cc` and `zum.cc`: their `jsonObject`
helpers returned dangling object pointers after destroying the owning parse
tree. Helpers now return an owning `ZuPtr` retained by token/discovery/JWKS/
authorization-response and CLI credential/request consumers. Added a client-only
fixture that drives token and discovery decoding through to JWKS rejection and
drains service shutdown. It does not establish successful authentication or
JWKS ambiguity rejection; those service acceptance cases remain outstanding.
These edits and the regression are unbuilt/unrun.
Added client-only signed-token fixtures: successful service startup and REST
forwarding, renewal based on the lesser of JWT expiry and `expires_in`, and
rejection of duplicate JWKS key IDs/key-set members/key fields with an otherwise
valid signed token. `verifyWorkload_` now caps the renewal deadline at the signed
expiry; `Service::init` rejects invalid scheduler shard IDs. All remain unrun.
Fixed service authorization-response handling: malformed HTTP 200 JSON no longer
maps back to success merely because the HTTP status is 200. Added client-only
assertions for that rejection and for rejecting an administrative service token
as a ping application bearer (audience isolation). Source inspection confirms
`zumpingd` publishes only its ping catalog, not user memberships. No rebuild/run.
Fixed service startup/shutdown overlap: each startup continuation checks that
the service is still Starting before advancing or resetting state. Previously
a late failed HTTP response could reset Stopping to Initial and strand stop's
completion. Added deterministic fixtures that hold each of the three startup
HTTP phases, enqueue stop first, then deliver the held response; no sleeps or
polling. They require Stopped startup completion and drained stop. Unbuilt/unrun.

JWKS parser source review: upstream and service parsers now reject duplicate
recognized key fields and duplicate supported key IDs, and validate decoded
P-256 points before accepting keys. Upstream also rejects duplicate `keys`
members. The upstream exchange fixture exercises key removal, duplicate IDs,
and duplicate key-set members using otherwise eligible signed claims. This
remains unbuilt/unrun; service-side runtime regression coverage is outstanding.

Upstream source review confirms `zumd.cc` supplies the real `UpstreamHTTP`
transport. Removed OIDCState's unbounded, non-expiring upstream key hash; each
code exchange now fetches a response/key-count-bounded JWKS and verifies only
against that response. A fixture completes two upstream logins, removes the
previously used key from the upstream response, and requires the third exchange
to fail with a fresh JWKS fetch. This is an unrun regression, not live upstream
SSO acceptance. The plan records the extra per-exchange retrieval explicitly.
Upstream login now requests and requires `auth_time`, forwards `max_age` and
`prompt=login`, and checks authentication age with configured skew. Removed the
incorrect `iat`-as-login-time substitution. Updated fixtures deliberately use
different issue/authentication times and assert the max-age boundary. These
source changes and assertions remain unbuilt/unrun.
Removed three hand-written `max_age` decimal loops in OAuth validation,
authorization result construction and grant preparation. Parsing now uses
`ZuBox<uint64_t>` with complete-consumption/null-sentinel validation; result
construction runs only after successful profile validation. Added ordinary,
trailing-junk, null-sentinel and empty-input assertions. No rebuild/test run.

Signing rotation source follow-up (unbuilt/unrun): `SignFn` receives the selected
`SignKey` record, allowing the daemon to decrypt non-bootstrap private material
with the `zum.sign_key` AAD. Token requests no longer carry a fixed signing key
ID: they select the newest eligible active ES256 issuer key within the configured
JWKS catalog bound, then reload and recheck eligibility before issuance.
Runtime fixture assertions now cover future-key exclusion, activation, and
retirement fallback. UserInfo now resolves the token header's key ID through
the database, checks issuer/algorithm and active-or-retiring validity, and uses
the same public-JWK parser as key registration before verifying the signature.
ServerConfig no longer accepts a fixed key ID/public key. Parser assertions
cover source preservation, point validity, ID mismatch, private-field rejection,
and clearing a previous output after failure. Administrative and app-service
bearer admission now share asynchronous database-key lookup and verification;
the existing audience, action and live-assignment checks remain. Their fixed
public-key cache is removed. Startup and token issuance now share `signKeyLoad`:
startup checks the selected key's decrypted private material against its public
JWK and caches that signer by its actual key ID, not `bootstrap`. Read-only
inspection confirms Ready bootstrap reconciliation does not reseed/reactivate a
retired bootstrap key. Selection fixtures cover future keys, wrong issuer,
catalog overflow, invalid limits, activation, and retirement fallback. External
signer references still have no daemon signer integration and fail closed.
PostgreSQL rotation/restart acceptance remains outstanding.
UserInfo and daemon bearer admission now share `signKeyVerify`, avoiding
independent lifecycle predicates. Signed-token fixture assertions cover active
keys, retiring-key overlap and its exact deadline, disabled/pending/revoked
states, future activation, issuer/audience/key-ID mismatch, and algorithm
rejection. UserInfo's explicit issuer-only verification remains separate from
the required administrative audience check at its call site.
These edits are unbuilt and unrun; only `git diff --check` has passed.

Resumed `zum3.md` after completing the separate build refactor documented in
`../zumbuild.md`. Server-private DB definitions are now split across context,
table-group, operation and saga headers; new work must preserve those boundaries.
The current baseline passed 75 Zdb unit tests, 25 Zum unit tests, four PostgreSQL
integration cases and the OpenSSL JWT fixture. These do not complete gate 5.

The first resumed gate-5 regression exercises an invited local user through the
actual REST API. The old server rejects direct Pending-to-Active but incorrectly
accepts Pending-to-Suspended-to-Active without credential enrollment. PostgreSQL
fixture `zum_user_state_20260910_1` reproduced that failure (expected 409, got
200). `adminState` now selects user-specific checks by table type, not a runtime
flag, and requires an enrolled handle before activation from any intermediate
state. It also rejects local state writes on External projections.

The expanded HTTP fixture covers Suspended and Disabled intermediates, missing
and stale ETags, unchanged rows on rejection, request-level audit actor/outcome/
correlation, secret exclusion and preservation across a drained PostgreSQL
restart. The ordinary `make -C zum/src -j3 && make -C zum/test -j3 &&
make -C zum/itest -j3` build passed, as did all 25 unit tests. PostgreSQL HTTP
`zum_user_state_20260910_2` passed the lifecycle assertions but failed its audit
query: SQL confirms two success and five failure `userState` records, whereas
REST omitted them. Audit's primary index is issuer-grouped; its initial query
used an empty group. The query continuation now owns its typed group key and
the audit route supplies the configured issuer. The first compile of this fix
exposed a default-group tuple mismatch; after correction, the incremental build
and all 25 unit tests passed. PostgreSQL HTTP fixtures `_3` through `_5` then
exposed response corruption: pagination returned identical audit IDs, but the
response formatter prepended each request's correlation ID to the first stored
audit correlation ID. The formatter now matches only the trailing empty error
envelope placeholder. The ordinary incremental src/test/itest build passed.
All 25 unit tests and the complete PostgreSQL HTTP fixture passed against fresh
database `zum_user_state_20260910_6`, including audit pagination, correlation
integrity, lifecycle rejection and record preservation after drained restarts.
The passing fixture also enumerates all 70 management routes through HTTP, checking
registry/seeded-action identity, missing/invalid bearer denial, denied workload
permissions and unsupported-method Allow responses. This is not a substitute
for the still-required authorized success/input/target tests for every operation.
External-projection HTTP coverage still belongs to the federation gate.
At that point other grouped collections, particularly role mappings and client
access, still needed explicit full-collection coverage; exact-key queries did
not prove that their default-group scans enumerated all intended records.

The next regression reproduced an empty client-access collection against the
previous binary using PostgreSQL `zum_group_query_20260910_1`. Client-access and
role-mapping records now have secondary application-grouped indexes; their REST
collections use those indexes, with the selected key carried through cursor
encoding/decoding and continuation. Existing primary indexes retain the
client-group and application/provider-group access paths. Bootstrap schema
version is now 10; legacy migration remains outstanding. The HTTP fixture adds
multiple clients, multiple providers with repeated mapped values, cross-app
isolation, cursor tampering/cross-app rejection and post-restart snapshots.
The ordinary incremental build through Zum's own examples/interop passed, but
PostgreSQL `zum_group_query_20260910_2` still returned an empty client-access
collection. Inspection established that both stores require grouping fields as
a leading prefix, while generic key metadata retains declaration order. Zdb's
typed-table store metadata now partitions group fields before member fields,
preserving the original C++ tuple layout and field accessors. This uses the
existing split-key and field-factory facilities, not a replacement table API.
A Zdb unit regression covers inverse grouping, ordinary keys and unchanged key
tuple order.
All 76 Zdb unit tests and all 25 Zum unit tests passed, as did the complete
dependency-ordered build and OpenSSL JWT fixture. PostgreSQL
`zum_group_query_20260910_3` returned the previously omitted client-access rows,
then crashed on cursor continuation. The captured core (PID 1061462), inspected
through `libtool exec coredumpctl debug`, identified a null `owner` load: the
decoder reconstructed a full ClientAccess from a key-only payload. It now
constructs only the selected key tuple. It also rejects malformed base64url
before the decoder, which otherwise accepts a valid prefix followed by invalid
characters. The incremental daemon rebuild passed. PostgreSQL fixtures `_4`
and `_5` then showed client-access pagination passing but role-map pagination
returning only two of four rows. PostgreSQL continuation SQL used independent
inequalities joined by AND, not lexicographic comparison. `ZdbPQ` now uses row
comparison for uniformly directed keys and equal-prefix alternatives for mixed
directions. Its incremental build passed. The full HTTP fixture passed against
`zum_group_query_20260910_6`, including both collections, cursor tampering and
cross-operation/app rejection, multiple provider groups, and preservation after
a drained restart. All three Zum PostgreSQL restart/saga cases also passed on
`zum_group_restart_20260910_1` before the SQL correction. A direct PostgreSQL
regression for inclusive/exclusive ascending composite and descending grouped
cursors passed on `zdb_cursor_20260910_1`; mixed-direction runtime coverage
remains to be added. The broader PostgreSQL saga suite stalled after reconnect:
the store was idle and both saga journals were empty, but the fixture awaited
the old live submission's callback after reconstructing the DB. That specific
test process was terminated (the suite is recorded as failed, not as a restart
pass). The fixture now relies on completed startup/replay for reconnect rather
than awaiting a callback that cannot survive reconstruction. Its incremental
rebuild passed, and all nine PostgreSQL integration cases passed against fresh
`zdb_cursor_20260910_2`, including the new cursor checks, reconnect, crash replay
and uncommitted-effect recovery.

Final combined Zum integration acceptance passed all four cases against fresh
PostgreSQL `zum_group_restart_20260910_2` and `zum_group_query_20260910_7`, using
the corrected PostgreSQL module and daemon. The final diff whitespace check
passed. No build or test process remains running from this continuation.

The next lifecycle slice adds `MembershipChange`, a four-step Zdb saga shared
by membership role and state replacement. It reserves the application, updates
and reserves the membership, releases the membership, then publishes the app's
new authorization/version snapshot and releases it. Compensation restores the
membership snapshot and app availability; each write is a separate journaled
effect. The REST preparation checks local provenance, ETags, owners and version
overflow; identical replacements preserve versions. Existing derive macros and
the single saga executor owner are retained.

PostgreSQL `zum_member_change_20260910_1` reproduced the prior missing app-version
increment through HTTP. The expanded fixture checks membership/app version
increments, missing/stale ETags, unchanged PUT retries and refresh narrowing.
The new staged PostgreSQL recovery case covers each intent/effect boundary and
a stale membership snapshot after app reservation. The incremental build is
complete for source, unit and integration targets. The first integration compile
rejected the fixture's enum-to-int8 initializer; an explicit State::T conversion
fixed that test-only error. All 25 unit tests passed. The focused HTTP fixture
passed on PostgreSQL `zum_member_change_20260910_2`. The final combined
`make -C zum/itest test` passed all five cases on fresh PostgreSQL
`zum_member_restart_20260910_1` and `zum_member_change_20260910_3` (34.4 s).
It covers all nine membership saga boundaries, stale-snapshot compensation,
missing/stale ETags, unchanged role/state retries, rejection of zero/unknown/
duplicate role references without mutation, version publication, refresh
narrowing and drained restart preservation. These are staged durable recovery
images and graceful restarts, not crash/failover acceptance. The remaining
`make -C zum/itest -j3 && make -C zum/example -j3 &&
make -C zum/interop -j3` build finished successfully; `make -C zum/interop test`
passed the OpenSSL ES256 fixture. The final whitespace check passed. No build or
test process remains running from this continuation.

Role deletion formerly only tombstoned the definition and left its assignment
references in place. To support recoverable variable-cardinality reference
removal, Zdb's typed saga phases now support payload-sized repetitions:
one journal ordinal per effect, per-effect compensation, persisted repetition
metadata, zero-length phases and checked uint32 expansion. It retains the same
saga engine, derive macros and continuation-based table operations. Unit layout/
catalog tests passed in the 77-case Zdb unit suite. The PostgreSQL two-shard
fixture's first compile exposed a runtime count passed to a compile-time test
macro and incompatible tagged callback construction; both are corrected.
Admission location allocation is now inside its existing exception handler.
The corrected dependency build and all 77 Zdb unit tests passed. PostgreSQL
`zdb_repeat_20260910_1` passed live/compensating batches but crashed during
interrupted-batch teardown. The core (PID 1083208), inspected through libtool/GDB,
identified the cut before the third insert's effect. The new fixture had paused
before constructing its row payload, whose destructor is unconditional. Payload
construction now precedes the pause but still precedes commit; this preserves
the intended durable boundary. SQL confirmed three intents but only two inserted
batch rows at that cut. The fixture rebuild passed, and the full PostgreSQL suite
passed all 14 cases on `zdb_repeat_20260910_2` (7.4 s), including 14 interrupted
batch cuts, live/empty batches, reverse ordering and interrupted compensation.
The diagnostic core copy is `/tmp/zdb-repeat-AQqohq/core`. A targeted Memcheck
recheck on `zdb_repeat_20260910_3` passed all eleven saga cases and reported zero
memory errors with leak checking disabled. It also exposed late PostgreSQL
shutdown callbacks using a closed connection/event-loop descriptor; this is not
a clean teardown acceptance result. Store stop now posts its admission fence to
the store thread, checks both queues, requests event-loop stop once, and suppresses
send/recv after that drain boundary. Both incremental builds completed, through
Zum's own examples and interop only. All 25 Zum unit tests and the OpenSSL ES256
interop check passed. PostgreSQL `zdb_repeat_20260910_4` passed all 14 integration
cases; the targeted Memcheck run on `zdb_repeat_20260910_5` passed all eleven saga
cases with zero memory errors and no invalid-descriptor, closed-connection, or
out-of-order deactivation diagnostics (leak checking disabled). Zum's four
restart/recovery cases passed on `zum_repeat_restart_20260910_1`. The added
catalog HTTP coverage initially omitted the mandatory Idempotency-Key header;
the corrected fixture passed on fresh `zum_repeat_http_20260910_2`. It checks
publisher ownership, initial/missing/stale catalog ETags, successful standard
action/role publication, stable identical-revision replay, and conflicting
revision/digest rejection. All build/test processes from this slice have exited.
The internal saga_type schema gains a repeat boolean;
offline migration must add it as false for old fixed phases. No existing store
has been migrated by this change. Zdb's shard ownership and saga machinery are
the concurrency mechanism; no additional lock or transaction coordinator is
required. Before-images and API preconditions still need validation inside the
ordinary shard-owned saga continuations.

The role-deletion slice adds `RoleDelete` to the typed saga catalog. Thirteen declared
phases expand to one effect per affected membership, client/admin access, scope,
or role mapping, with compensation and stable role tombstones. The initial
source build passed. REST now prepares bounded, continuation-based snapshot
queries and submits the saga through the existing shared executor. PostgreSQL
fixtures cover empty/nonempty reference sets and rollback after the first map
deletion; HTTP checks cover all five reference kinds and a drained restart.
The adapter initially missed its direct ZumDB include; the corrected source
build passed. All seven restart/recovery cases passed on PostgreSQL
`zum_role_remove_20260910_1`, including all three new role-removal cases.
The HTTP fixture initially omitted the deletion operation's required
Idempotency-Key; corrected requests passed on `zum_role_http_20260910_2`.
That run verifies all five reference kinds, rejected preconditions, unrelated
reference preservation, logical role version/tombstone, drained restart,
idempotent deletion replay, and newer-revision restoration of the standard role
under its original ID without restoring deleted references. It checks the
restored definition and unchanged references through another restart.
The dependent unit/example builds completed; all 25 Zum unit tests and the
OpenSSL interoperability check passed. Error-envelope review added the required
message and correlation placeholder; the stricter HTTP check on `_3` caught the
missing placeholder before the final correction. The final incremental source
build and full HTTP fixture passed on `zum_role_http_20260910_4`, including
nonempty error messages and correlation IDs. All build/test processes from this
slice have exited. Every intermediate role-delete intent/effect and multi-page
removal scans still need dedicated tests. Catalog publication was still a
direct-write path at the end of that verified slice.

The catalog slice now has the verification evidence recorded below:
`CatalogPublish` prepares definition before/after images and submits one typed
Zdb saga, including omission retirement and same-revision tombstone repair.
Scope `catalogRoleIDs` records the publisher baseline separately from effective
bindings so unchanged publication cannot restore explicit removals. Schema
version is now 11; the explicit offline migration remains outstanding.
The initial nine-case PostgreSQL suite passed on
`zum_catalog_restart_20260910_1`, including catalog completion and compensation.
Expanded HTTP lifecycle cases await a successful run. The initial
build caught an invalid `Ztls::MD::start()` call; it was removed after that build
exited. The incremental `-j3` source and dependent builds then passed through
Zum's own examples and interoperability target. The HTTP fixture on
`zum_catalog_http_20260910_1` reached catalog
publication and failed with 400 on the accented-label manifest. Inspection shows
`ZfJSON::quote` emits Unicode escapes, unlike the digest contract's UTF-8;
its non-ASCII path also fails to advance past the complete UTF-8 sequence.
The fixture exited and drained its server. The next coherent, unbuilt batch
fixes framework quoting (full UTF-8 advancement, control escaping, optional
UTF-8 output), uses that output for catalog digests, adds focused Zf tests,
stages all 29 catalog intent/effect cuts, and expands HTTP retirement/rebinding
checks. Fresh `_2` restart and HTTP databases are prepared but unused. No new
build had started during editing/review. The batched incremental verification
pass is now starting with Zf source/tests, followed by affected dependencies and
Zum, with at most `-j3` and no clean or reconfiguration.
Zf source/tests rebuilt successfully and all 253 checks across its nine test
programs passed, including the new quoting cases. The dependency/Zum build chain
completed through Zum's own interoperability target. All 11 PostgreSQL recovery
cases passed on `zum_catalog_restart_20260910_2`, including all 29 catalog
intent/effect cuts and compensation after staged effects. All 25 Zum unit tests
and the OpenSSL interoperability check passed.

The HTTP run on `_2` passed Unicode digests and role repair before finding a
fixture error: scope-name lookup also requires audienceID. Correcting that query
needed no rebuild. The full fixture passed on `zum_catalog_http_20260910_3`,
including omission retirement, preserved disabled definitions, new scope binding
introduction without resurrecting removed bindings, and drained restarts.
Its new coverage report recorded successful calls for 37/70 administrative
operations; that does not prove the remaining per-operation acceptance matrix.

Additional read-only API cases on `zum_catalog_http_20260910_4` passed populated
credential/policy/session/consent/grant/signing-key pagination and redaction plus
empty local-only identity/evidence queries, then failed because an invalid
auth-policy appID was accepted as an empty successful lookup. Inspection found
numeric scanners' partial/null results being used unchecked in query
and path IDs; action ID zero was also wrongly rejected by the shared path parser.
The next unbuilt batch uses `ZuBox<uint64_t>::scan`, checking full input
consumption and the null sentinel, with no custom integer parser or overflow
machinery. It preserves valid zero-based action IDs, rejects empty query values,
and tests malformed/sentinel
filters plus action-zero lifecycle/ETag/restart behavior. Syntax and whitespace
checks passed. The incremental `zum/src` build subsequently completed successfully.
The fresh `zum_numeric_http_20260910_1` database has been created with extensions,
but no HTTP run was started: user direction is now code completion before further
builds/tests. The rotation fixture also adds ETag, overlap/retirement, redacted
replay and PostgreSQL restart scenarios; those remain unrun.

Code-completion batch: renamed the old `zumc.cc` example to `zumping.cc`, removed
its Zum header/library dependency, switched its resource to `/ping`, and added
file-based service/client/scope/callback configuration. OAuth parameters are
form-escaped; callback state is prepared before listening, duplicate/mismatched
callbacks do not complete login, and login has a finite deadline. Refresh tokens
are optional, with bearer token-type validation. The same callback/token fixes
are applied to the administrative `zum` client. These source changes are not
built or runtime-verified. The old DB-owning example is now replaced by
`zumpingd.cc` plus the private asynchronous `PingHTTP` transport. Its configured
issuer/client and environment-only `ZUM_CLIENT_SECRET` initialize `Zum::Service`;
startup publishes the ping action/role/scope before starting the listener. The
service exposes authorize/token/revoke forwarding and JWT-authorized `/ping`,
with no Zdb, private signer or upstream-provider integration. `Makefile.am`
declares only `zumpingd` and `zumping`; neither links the private server library,
and the latter has no Zum linkage. Sample non-secret configs and provisioning
instructions are in `zum/example`. Shutdown stops the listener, drains Service,
then finalizes transport and the scheduler. These implementations are unbuilt;
the next work remains completing REST lifecycle/outcome persistence and CLI
provisioning before consolidated verification. No gate is advanced by this batch.

The administrative and ping clients now use transport completion (including
terminal failure) to release their stack-owned results, instead of returning
early on an independent timeout. Each attempt has its own completion guard;
admin retries snapshot the request so a late completion cannot satisfy the next
attempt. Result/request buffers containing tokens are cleared on destruction.
Admin credential files are bound to issuer/client and checked for owner-only
regular-file permissions on Linux; persistence failures are reported. User
invitation/recovery capability responses now require protected `$secretOutput`,
as do client/app credentials. These changes remain unbuilt and untested under
the current code-completion-first instruction.

Interrupted HTTP request-status linkage, detailed catalog audit deltas, and
multi-page catalog retirement coverage remain unfinished. Concurrency safety
relies on native Zdb sharding and
sagas, not an additional application locking or transaction layer.

Request-outcome persistence now uses the typed `RequestOutcome` native saga for
keyed REST requests: reserve the pending request, insert its preallocated audit
record, then publish the terminal request status/result IDs. Compensation
restores the pending before-image and removes that audit record. Payload images
contain no response JSON, credentials or enrollment capabilities. Audit IDs use
the existing issuer counter; an interrupted reservation may leave an unused ID.
The REST callback no longer commits terminal request status separately before
writing its audit. The new saga/header/catalog entry and daemon wiring are
unbuilt and untested; PostgreSQL boundary coverage is still required.

Remaining business-to-outcome recovery gap: `SagaStepComplete::finish_` deletes the saga
intent and cleans step records before invoking the completion callback;
`SagaDB::sagas` replays with an empty callback. `idemFinish_` currently exists only
in the live HTTP callback chain, although it now submits the recoverable outcome
saga. A crash before that submission can still leave a completed business
mutation pending. The request's `sagaID` currently identifies its outcome saga,
not the business mutation. Merely filling it cannot close that gap. The next
implementation must preserve the non-secret outcome
and audit linkage within native recoverable processing, before the evidence
needed to recover it is removed. Do not infer success just because a saga ID is
absent. Add PostgreSQL cuts after business effects, during outcome persistence,
and before response delivery, covering both completion and compensation.

Saga/request/logging correction (source work; unbuilt and unverified):
The speculative Zdb finalization hook and persisted decision/rollback-direction
extension were removed. They were not built or used to open a database; no
migration for those removed fields is required. Unrelated Zdb changes remain.

The normative contract is now in `zum3.md`: successful request results belong
inside the business saga, compensation restores pre-saga application state, and
operational events use ZiLog/Ztc outside that saga. AppActionAdd no longer
contains audit steps or retains failed request state during compensation.
The common administrative event writer now emits ZiLog instead of writing DB
rows. Production audit schema registration, table derivation, FlatBuffers schema,
issuer counter, query/cleanup routes, and cleanup implementation are removed.
The Audit payload is now an ephemeral event without persistence metadata.
All former auditWrite calls now use callback-free logEvent; authentication,
passkey, token, revocation, and administration paths report their original result
without an audit-storage failure branch or logging-dependent completion. The request
table derivation header is now ZumRequestDB.hh. Schema version is 12; older
stores require the explicit migration path before activation. No existing
PostgreSQL database or its historical audit data has been modified or deleted.

The management registry retains 70 numeric slots with 68 live operations;
retired audit slots have no route/permission and are skipped by bootstrap.
Registry tests now check their absence. Former persisted-audit unit assertions
now match individual ZiLog events, with a logger-queue continuation providing
deterministic observation. The HTTP fixture checks removed endpoints return 404
and checks operation outcomes/correlation and secret redaction in the drained
server log. Unit checks also reject client-secret, recovery-capability, and
authorization-code leakage. Operation pagination skips retired slots and counts
only live entries; retired IDs cannot be assigned as administrative permissions.
This source batch has not been built or tested.

RequestOutcome and the daemon's separate outcome path still need removal;
action creation now bypasses both: its payload includes the request descriptor,
the first saga step inserts the pending request, compensation deletes it, and
the terminal step records successful result IDs. HTTP admission only looks up
replays for this operation; it does not create a separate pending row. The action
HTTP completion path logs/responds without outcome persistence. Other operations
still need conversion. Publication ordering and concurrent/recovery behavior
remain subject to verification, not assumed correct from source alone.
The action serialization checks and PostgreSQL staged-recovery fixture now cover
the request insert/result steps, successful result recovery at all 13 boundaries,
and request removal on duplicate-name compensation. These checks are unrun.

Application enrollment now also owns request insertion and successful result
storage in its saga, with request deletion on compensation. Its recovery and
serialization tests cover that payload; HTTP replay checks exclude client
secrets before and after restart. Shared request row functions invoke native
saga APIs with continuations and the caller's step, not a separate transaction.
All of this remains unbuilt/unverified.

Further steering: generally one REST operation is one saga, including business
logic and asynchronous non-database steps. Native runtime skipping is the
branching mechanism. Do not confine sagas to row mutations or retain the daemon's
operation-specific callback state machines as the business orchestrator.
AppEnroll_ and ActionAdd_ still perform preparation/business decisions outside
their sagas; request-result wiring alone does not finish that alignment.
Action allocation validation now runs in its saga, including a missing-app read
before native mutation preparation; the daemon snapshot supplies saved recovery
inputs without duplicating state/counter validation. Further simplification must
use replayed, idempotent step bodies for business preparation, not assume a step
is executed only once. A skipped mutation callback is not a skipped step body.
Zdb supplies replay idempotency; ensuring it again in Zum is explicitly not an
application responsibility. Review remaining owner/snapshot checks against real
business and compensation requirements, and remove any whose sole purpose is
duplicate-effect detection. HTTP request-key/result handling is a separate
protocol concern, not a replacement for Zdb's replay guarantee.
Action insertion no longer recognizes an existing identical action ID as a
special replay case. Its name-uniqueness check is applied by the native mutation
callback only when Zdb executes that insertion, leaving replay to Zdb.
Role deletion now also inserts/completes its HTTP request within its business
saga and deletes it on compensation. Its formerly terminal app publication step
has a matching inverse; the request result is the new terminal step. PostgreSQL
fixture assertions cover completed and compensated request state, including
the empty-reference branch. These source changes are unbuilt and untested.
Catalog publication now also owns its request admission/result steps and bypasses
the separate outcome saga. An unchanged manifest goes through the same saga:
native skip handles its application steps, while zero-length repeated row phases
perform no work. Its request result still completes without changing app versions
or assignments. Added an unchanged-catalog unit case and extended the PostgreSQL
staged fixture to the request insert/result boundaries. The fixture now covers
33 catalog boundaries; none of the new checks have been run. Manifest scanning
and business preparation still need consolidation into the operation saga.
Membership creation now submits a single MembershipAdd saga. Application/local
user validation, duplicate detection, membership insertion/publication, and the
successful request result execute inside it. The HTTP adapter generates the
saga ID and formats the response; it no longer reads or writes business rows.
Native compensation removes the pending request after rejection. Serialization
and unit cases cover successful creation, duplicate membership, missing app/user,
and rejection of external users; HTTP cases cover completed request replay
before/after restart and duplicate rejection without a retained failed receipt.
These changes remain unbuilt and unrun; staged mid-saga PostgreSQL recovery
coverage for MembershipAdd remains to be added.
Membership role/state changes now also own their request insert/result steps.
Their former terminal app-publication step is compensable, restoring versions,
authority version, timestamp, and reservation before earlier effects reverse.
An unchanged assignment uses native skips for the business steps while still
completing the request. The HTTP response preserves the unchanged ETag.
The PostgreSQL fixture now stages all 13 boundaries, checks completed request
state, and checks removal of the request on stale-snapshot rollback. Serialization
and unchanged-assignment unit checks were added. All remain unrun.
Membership role-reference validation now executes inside the membership saga:
the payload distinguishes role assignment from state-only changes, and a
continuation-based method in ZumSagas.cc reads and validates application-qualified
roles. Duplicate/zero IDs, inactive/tombstoned roles, and cross-app-only references
are rejected before the membership mutation. The HTTP role-reference callback
workflow is removed; the completion adapter reports the transient validation
error. Unit sources cover these rejections, request rollback, unchanged app/member
versions, and serialization of the role-assignment flag (not the transient error).
The local-user requirement now also runs within the saga's validation step,
including state-only and unchanged-assignment requests. The HTTP user lookup
is removed. Unit sources exercise external users and missing users even when
a membership row exists; recovery fixtures now include the required local user.
Application eligibility, membership state/version, and ETag checks now also
execute in the saga. Unchanged requests perform those checks before skipping
business effects. Missing app/member rows are business rejections before native
mutation preparation. The adapter only reads immutable compensation snapshots,
submits the saga, and formats its reported result; the post-failure DB lookup
has been removed. Tests now include wrong ETags for changed/unchanged requests,
stale application snapshots, and missing applications, checking request rollback
and unchanged application/membership state. These changes remain unbuilt/unrun.
Role-action assignment now uses an app-scoped RoleEdit saga instead of
ActionRefs_ followed by a direct adminUpdate. It validates action references and
the role ETag, updates/publishes the role, advances application authority/version,
and completes the request within one saga. Compensation restores the role
before-image and app versions/timestamp and deletes the pending request. The
old global RoleChange is not used for this endpoint. The HTTP adapter captures
snapshots and formats results only. Added serialization and unit sources for
duplicate rejection, wrong ETag rollback, and successful action replacement,
including completed/absent request state. PostgreSQL mid-saga recovery coverage
for RoleEdit remains pending. No build or test run validates this batch yet.
The same app-scoped saga now also handles role label edits; RoleActions was
renamed RoleEdit before this source batch was built or used with any database.
Role label updates no longer use direct adminUpdate or the separate outcome
saga. The saved payload selects action replacement versus label editing.
Label-only edits preserve the action bitmap and app authorization version,
while advancing role/app metadata versions and persisting the request result.
Serialization and unit sources cover that branch. No compatibility alias,
extra saga executor, or replay bookkeeping was introduced.
Role state changes now use RoleEdit as well; its saved edit kind selects actions,
label, or state. Unchanged state requests check app/role eligibility and ETag,
then use native skipping without advancing role/app versions or authority.
State changes advance app authority; label-only edits still do not. The HTTP
role-state direct write is removed. Missing snapshot rows carry version zero
so a subsequently created row cannot be mistaken for the captured before-image.
Added unit source cases for changed/unchanged role states and rejected ETags,
checking retained labels/actions, versions, and request completion/rollback.
This remains source-only progress, with builds and PostgreSQL verification deferred.
Scope role/state edits now use ScopeEdit instead of direct administrative writes.
The saga checks app-qualified role references, scope ETags/state, and app versions,
then updates/publishes the scope and app authority with a same-saga request result.
Unchanged scope state uses native skips after validation. Compensation restores
the scope before-image and application versions/timestamp. RoleEdit and ScopeEdit
share only the HTTP snapshot/submission/response adapter; their business rules
remain in the saga definitions. The scope serialization/catalog checks were
added; runtime and PostgreSQL boundary coverage for this new path are still
pending. No build/test execution has validated these changes.
Scope unit sources now exercise duplicate/zero role IDs, disabled/tombstoned
roles, cross-app-only references, successful assignment, state changes,
unchanged state, and stale ETags. They check scope/app versions and authority,
request completion versus removal on rollback, and preservation of audience,
catalog revision/origin, and catalogRoleIDs. These are added assertions, not
runtime evidence; PostgreSQL recovery coverage remains pending.
Shared app reservation, edited-row release, and app publication effects now
invoke native saga APIs through three continuation functions in ZumAppDB.hh.
RoleEdit/ScopeEdit retain their own step metadata, payloads, and business rules;
there is no separate transaction or replay mechanism. Action state updates now
use ActionEdit with these same effects and the shared HTTP snapshot adapter.
The direct action-state write and separate request outcome are removed. The saga
checks ETags, tombstones, state transitions, and app eligibility, supports
unchanged-state skips, and compensates using the action before-image and app
versions/timestamp. Added serialization/catalog assertions including action ID
zero. Runtime and PostgreSQL recovery assertions for ActionEdit remain pending;
the source batch is still unbuilt and unrun.
Action-state unit sources now exercise ID zero, changed and unchanged states,
and rejected ETags. They assert preservation of action identity/catalog fields,
role-independent action state, appropriate app authority/version increments,
and completed versus compensated request records. These assertions have not
been executed; PostgreSQL mid-saga recovery verification is still outstanding.
Application label/state updates now use AppChange instead of direct writes
and a separate request outcome. The four-step saga inserts the request,
validates/updates the application, publishes it, and completes the request.
Compensation restores the full application before-image. Unchanged states skip
business writes after ETag/eligibility checks; labels do not advance authority.
The shared HTTP adapter captures only the one application snapshot for this
case. Serialization/catalog checks are added; runtime and PostgreSQL recovery
checks for AppChange are pending. No rebuild/test run has validated this batch.
AppChange unit sources now cover label-only authority preservation, changed
and unchanged states, stale ETags on both paths, and request completion/rollback.
They compare full application snapshots, including catalog digest/revision,
service client ID, allocation counter, and timestamps. These checks are unrun;
PostgreSQL recovery verification and the remaining REST conversions still remain.
User profile/email and state updates now use UserEdit with same-saga request
results. Its payload records the selected profile fields, so omitted fields are
preserved and explicitly supplied empty values remain meaningful. State changes
retain local-user, revoked-state, and first-credential activation restrictions;
profile-only changes preserve authority. Unchanged state uses native skips.
The shared snapshot adapter now loads users directly for this path. Direct user
updates and their separate request outcome have been removed. Serialization
checks were added; runtime/recovery cases and execution remain pending.
UserEdit unit sources now cover profile/email partial updates, explicit profile
clearing, profile-only authority preservation, stale ETags, pending-user
activation rejection, suspension/no-op suspension, and external-user state
rejection. They compare identity/assignment fields and request results, and
check that a suspended user without an enrolled handle cannot be activated.
These cases remain unrun; they do not constitute runtime acceptance.
Credential label/state updates now use CredEdit with same-saga request results.
Authentication updates credential signCount/backedUp/updated without advancing
the administrative version, so admission checks the saved timestamp as well as
the ETag. Compensation restores only the administrative fields it changed
(label/state, version, timestamp, owner), not signCount or backedUp. The shared
snapshot adapter now supports byte-keyed credentials; their direct writes and
separate request outcome are removed. Serialization checks were added; runtime
counter-preservation and PostgreSQL recovery cases remain pending and unrun.
CredEdit unit sources now cover labels, state changes/no-ops, stale ETags,
stale timestamps, and request completion/rollback on rejection. They deliberately
use older signCount/backedUp values in the saved snapshot and check that forward
administrative edits preserve the live counters, key material, and user binding.
These cases are unrun and do not prove counter preservation during compensation
after a committed effect; that PostgreSQL recovery case is still required.
Audience name/state updates now use AudienceEdit with same-saga request results.
The HTTP adapter captures the globally keyed audience and its owning app; the
saga validates ETags, state and ownership, changes the audience, and publishes
the app version. Name edits preserve the authorization version; state edits
advance it, and unchanged state uses native skips. Compensation restores the
saved audience and app fields. Shared catalog release uses the framework primary
key metadata for both global audience and app-qualified catalog keys. Payload
round-trip, step catalog, and business-precondition test cases were added but
remain unrun; runtime and PostgreSQL recovery coverage are still required.
Provider configuration/state updates now use ProviderEdit, including their
request result in the same saga. Configuration patches preserve omitted fields;
the saved intent contains encrypted client-secret bytes only, never plaintext.
Business validation occurs in the mutation step; compensation restores the
saved provider record and unchanged states use native skips. Payload, field
preservation and precondition test cases were added but remain unrun; runtime
and PostgreSQL recovery checks are still pending.
Client configuration/state updates now use ClientEdit and same-saga request
results. Existing grant/type/redirect validation is shared with enrollment;
patches retain omitted fields and preserve credentials and app ownership.
ProviderEdit and ClientEdit share the native single-record update code, with
business validation inside the mutation callback and full before-image
compensation. Their HTTP snapshot adapter now derives its record type from the
payload rather than a growing nested type list. Client payload round-trip,
partial-update, invalid-grant/redirect, ETag and state tests were added but are
unrun. Mid-saga runtime and PostgreSQL recovery verification remain pending.
Signing-key retirement now uses KeyRetire: request insertion, retirement, and
successful result are one saga. The business step validates the retirement
deadline against the saved operation time, ETag, version exhaustion, and terminal
key states. Compensation restores retirement/state/version/timestamp fields
without changing key material. No application replay guard or new owner field
was introduced. The last direct adminUpdate caller is gone and that helper was
deleted. Payload and precondition tests were added but remain unrun; key creation,
cleanup, and end-to-end key lifecycle/recovery checks remain outstanding.
Client-access and admin-access state changes now use native sagas with same-saga
request results. Client-access state changes advance its authorization version;
unchanged states use native skips. Composite keys come from framework metadata,
and compensation restores the access before-image. The final adminState callers
and the direct state-update helper are removed. State/precondition, assignment
preservation, and payload test cases are present but unrun; live access revocation
and PostgreSQL recovery verification remain pending. Access replacement operations
still need conversion.
Role-mapping deletion now uses RoleMapDelete with application reservation,
mapping deletion, application authorization-version publication and request
result in one saga. Compensation restores the deleted mapping and application
before-image. Missing If-Match returns 428; ETag/ownership checks are in the
business step. Payload and precondition test cases were added but are unrun;
mapping replacement and PostgreSQL deletion/recovery verification remain pending.
Role-mapping creation/replacement now uses RoleMapPut. Native skipped steps
select insert versus update from the saved before-image; provider and role
reference checks execute inside the saga. The same saga publishes the app's
authorization version and completes the request. Rollback removes a created
mapping or restores the replaced mapping. The REST adapter formats the result
from the completed intent without a post-operation DB write. Create/update
preconditions and payload tests are present but unrun; asynchronous reference
rejection and PostgreSQL branch/recovery coverage remain required.
Authentication-policy creation/replacement now uses PolicyPut, with policy
validation and optional provider lookup inside the saga. The standalone branch
does not require a provider. Mapping and policy replacements share native
insert/update step code and preserve original creation timestamps on update;
their result formatting uses the same result construction as persistence.
Both branches publish the application's authorization version. Payload,
precondition, and result-construction tests were added but are unrun; policy
validation/reference rejection, runtime effects and PostgreSQL recovery remain
to be verified. Only access replacements still call the direct adminPut helper.
Client-access and admin-access replacement now use ClientAccessPut and
AdminAccessPut with same-saga request results and application publication.
Reference checks are in the saga: app-local roles, active client/audiences/scopes,
scope audience inclusion, and valid nonduplicate management operations. HTTP
still checks delegation and superuser admission. The direct adminPut helper and
the obsolete RoleRefs_/ClientAccessRefs_ callback classes have been removed.
Payload/result tests cover creation-time preservation and client-access authority
version increments but are unrun. Actor eligibility, reference rejection,
runtime delegation/revocation and PostgreSQL recovery still require verification.
Provider creation now uses ProviderAdd, including field validation, native
insertion/publication, and the generated provider ID in the same-saga request
result. Its saved payload contains only protected client-secret bytes. The REST
response uses the saga's result construction. Creation validation, result and
payload tests were added but remain unrun; uniqueness-conflict reporting and
PostgreSQL insertion/recovery verification remain required.
Audience creation now uses AudienceAdd, including application reservation,
native audience insertion/publication, application authorization-version update,
and generated audience ID in the request result. HTTP captures the before-images
and formats the result; business ownership/field checks execute in the saga.
Validation, payload and result tests were added but are unrun; duplicate URI
conflict reporting and PostgreSQL creation/recovery checks remain pending.
Role/scope creation now uses RoleAdd/ScopeAdd with same-saga request results and
application authorization-version publication. Both create Custom catalog
entries with empty action/role assignments. Scope validation resolves its active
app-owned audience inside the saga; the transitional audience URI is reconstructed
there rather than supplied by the HTTP adapter. Result/payload and app-mismatch
tests were added but are unrun; live audience reference rejection, uniqueness
conflict reporting and PostgreSQL creation/recovery remain to be verified.
User invitation now uses UserInvite: pending local user, enrollment grant,
publication and successful request result are one saga. The grant contains the
opaque capability digest, not its plaintext token; the HTTP completion alone
adds the one-time enrollment URL and clears its transient token. Compensation
removes both newly created records. Validation/result/payload tests were added
but are unrun; uniqueness, interrupted invitation recovery and enrollment URL
end-to-end checks remain pending. The invitation runtime test now uses UserInvite
to create both records and then starts enrollment with the issued capability.
It also forces a grant collision after user insertion and checks compensation
removes the new user while retaining the original grant. These source cases are
unrun. The obsolete enrollmentIssue API, implementation and configuration type
have been removed; there are no remaining C++ references to that path.
Administrative user recovery now carries its request through RecoveryIssueConfig
into RecoveryStart, with request insertion/completion in that same saga.
RecoveryStart v3 retains the user's previous state and timestamp, and compensation
restores state, authority version, row version and timestamp instead of leaving
the user suspended. Payload/catalog tests were updated and a grant-collision
rollback case was added; all are unrun. RecoveryIssue now captures the user
before-image and issues the opaque token; issuer/config validation and user
eligibility/version checks execute inside RecoveryStart. Redundant replay guards
were removed from its inverse deletion and publication steps. The stale-version
test supplies otherwise-valid intent so it reaches the version check.
PostgreSQL recovery validation remains pending. Old v2 recovery intents require
migration or settlement.
Client creation now uses ClientAdd with app reservation/publication, native
insertion and same-saga request results. Public clients are secretless; a
confidential client's saved intent contains its secret hash, with plaintext
returned only by the HTTP completion and cleared there. The temporary random
credential buffer is also cleared. Native-client result, payload and invalid
grant/auth-method cases were added but are unrun. Confidential enrollment,
uniqueness conflicts, retry secret suppression and PostgreSQL recovery remain
to be verified.
Client-secret rotation now uses ClientEdit's secret branch: new hash, optional
previous-hash overlap, secret/row versions, and request result belong to one
saga. Compensation restores the saved client including both hashes and deadline;
plaintext remains in the one-time HTTP response path. ClientEdit response
formatting now uses its public result record, including secretVersion. Overlap,
no-overlap, version-exhaustion and payload tests were added but remain unrun;
service restart, secret suppression on retry and PostgreSQL rotation recovery
are still required.
Signing-key creation now uses KeyAdd with a same-saga request result. It accepts
exactly one private-key source (provider reference or encrypted private material),
fixing the old helper's rejection of the encrypted-material REST path. Plaintext
private material is encrypted and cleared before submission. Payload/result and
source-selection tests were added but are unrun. The key lifecycle runtime test
now uses KeyAdd/KeyRetire; both obsolete direct-write APIs and their helpers are
removed. KeyRetire explicitly updates both retirement indexes through Zdb's
key-update parameter. JWKS retains Suspended/retiring keys until their retirement
deadline, and test sources check overlap visibility plus both updated indexes.
These cases are unrun. JWK/private-key consistency, key lifecycle and PostgreSQL
insertion/recovery verification remain pending. Operational logging is checked
at the REST boundary, not fabricated by direct saga unit tests.
Session, consent and grant revocation now submit one Revoke saga, using native
repeated steps over selected before-images. The HTTP adapter no longer mutates
these rows directly, including the single-grant path. Request persistence,
revocation and owner release are in that saga; rollback restores prior state,
versions and timestamps. Payload round-trip and eligibility test sources were
added, not run. Bulk selectors now page within user/app indexes until the
eligible-match limit or group end; previously they filtered only the first raw
page. Cleanup stops at the first unexpired grant in expiry order. Consent gains
a user-grouped index and Grant an app-grouped index (schema version 13).
User/app pagination keys explicitly include row-unique member fields: session
digest, consent client/app/audience and grant ID. Zdb does not append these
implicitly; without them nextRows could omit equal-key rows (or repeat a
group-only page). Key-distinctness tests were added and the grant secondary
lookup fixture was updated. Cleanup deliberately reads one bounded expiry page,
not nextRows on the non-unique expiry key; it may remove fewer than limit when
rows are owned. These source changes and test cases remain unverified.
PostgreSQL index/migration and multi-page selector tests, result replay counts,
and runtime rollback/recovery coverage remain pending.
The obsolete direct-write grantRevoke API and wrapper are now deleted. Native
Revoke test sources replace their callers and cover successful revocation,
already-revoked skipping, and restoration after a later missing-row failure.
The old unit expectation of an API-wrapper audit event was removed; REST outcome
logging remains at the HTTP boundary. Request insertion compensation now uses
the native saga delete callback directly, without an application replay guard.
All of these cases remain unrun.
Enrollment now uses enrollment.v2 with a saved ceremony before-image and a
terminal native deletion step. Its success callback only logs and responds;
it no longer deletes the ceremony outside the saga. Compensation restores the
ceremony's original state/owner instead of retaining a spent failed ceremony.
Serialization, catalog, restart fixture and duplicate-user rollback test sources
were updated. Precreated-user enrollment now snapshots the actual user, validates
its saved version in a native update callback, declares handle-index changes,
and restores the full before-image on rollback. Update and insertion occupy
separate native steps with runtime skips. A credential-collision test checks
restoration of roles/version/timestamps and preservation of the conflicting
credential. Enrollment rollback no longer has application replay guards: native
inverse callbacks restore/delete their records, while forward state and owner
checks remain. Consumption checks ceremony expiry and the saved challenge,
binding, digest and generation. Credentials for precreated users inherit the
saved user auth version. Other authentication sagas still need the same review.
Credential addition now uses credentialAdd.v2: its saga saves/restores the
ceremony and deletes it in a terminal native step. Its completion callback only
logs and responds. Enrollment and CredentialAdd share the native deletion effect.
Payload/catalog and credential-collision rollback test sources were added, unrun;
credential activation/release compensation now uses native inverse callbacks
without application replay guards. Recovery enrollment likewise moves ceremony
deletion into recoveryEnroll.v2 and saves ceremony/user before-images. Its handle
update declares index 1, increments the user row version, and rollback restores
the prior timestamp/version as well as handle/state. Recovery success callbacks
only log/respond. Serialization/catalog fixtures were updated; full recovery
rollback and PostgreSQL replay tests remain unrun.
Authorization-code exchange now uses codeFamily.v6 with the saved code record
and a terminal native delete. The token callback no longer deletes the code;
it retains its existing final authority check before releasing the response.
Code/family compensation no longer relies on application replay guards.
Consumed-grant deletion is shared with the registration/recovery sagas.
Payload/catalog tests were updated but have not run. Active v5 code-family
intents need draining/migration along with the other replaced saga definitions.
Code and refresh token flows now keep selected scope in the response as their
single retained value; persistence previously read m_scope after moving it into
that response. CodeFamily compares the original code scope/IDs/action ceiling
against its saved before-image, not against the potentially narrowed new scope.
Runtime test sources now require persisted family scope to equal response scope
after exchange and refresh. These regressions remain unrun.
Native runtime test sources now exercise refresh-family insertion collision
(the code must be restored and the conflicting family preserved), recovery
credential collision, and recovery user-version conflict after credential
insertion (the inserted credential must disappear and ceremony/user state must
be restored). Code-family success assertions now expect code deletion inside
the saga. Existing failure fixtures carry before-images so they reach the
intended business checks. No runtime tests or builds have been run for this batch.
AdminAccessPut now resolves its actor inside the saga, in addition to validating
operation and role references. User IDs use ZuBox with complete-parse and sentinel
checks; missing, saga-owned and terminal users are rejected. Pending invitations
remain assignable. Service actors must be active confidential/basic-auth clients.
Native negative fixtures cover missing users/clients, trailing numeric junk and
unchanged app state after rollback. These tests remain unrun.
The temporary canonical request buffer used for idempotency hashing is now
cleared after hashing, since administrative request bodies can contain secrets.
Constructor-ordinal inspection found duplicate ordinal zero in Cred metadata;
its userID and later fields now use the correct contiguous ordinals 1..12.
Zfb reconstructs records by these ordinals. A full credential round-trip fixture
checks identifiers, key/counter, flags, timestamps, owner and versions. Other
Zum record and saga declarations passed the source ordinal inventory. The new
fixture has not run; this is not generated-schema or compiler verification.
Signing-key creation now parses the public JWK with ZfJSON, requires matching
kid and EC/P-256 signature metadata, decodes coordinates with ZuBase64URL and
validates the point through Ztls COSE. A private d field is rejected rather than
persisted/published as public metadata. Parse scratch is cleared on return.
Key fixtures now use a public P-256 point; negative cases cover wrong kid,
private material and malformed JSON. Tests remain unrun. Matching an encrypted
private key or external signer to the public key remains an outstanding check.
REST private-key enrollment now imports the supplied EC scalar through Ztls and
compares its derived public point with the JWK before encryption. Scalar-one/two
test fixtures cover matching and mismatched keys. Encryption now uses the same
zum.sign_key authenticated-data domain as bootstrap/startup; the earlier signKey
domain was inconsistent. Runtime signing still needs to select/decrypt the actual
registered key: Daemon currently retains only the bootstrap signer and rejects
nonempty provider references. These changes and tests are unverified.
These changes are unbuilt; old v1 active
intents require migration/draining, not silent reuse under the new definition.
Grant cleanup now submits a GrantCleanup saga over expiry-index snapshots, with
native repeated deletion and before-image insertion on rollback. Its REST path
no longer invokes the direct-write cleanup API; that API and its request wrapper
have been deleted. Test sources cover payload serialization, successful deletion,
and restoration after a later missing-row failure. These tests have not run.
The route inventory contains 46 administrative writes, all now submitting
request-owning business sagas. The transitional requestInSaga predicate,
RequestOutcome saga/schema/header, direct request insertion and HTTP outcome
persistence have been removed. Admission only reads prior request state or
prepares the descriptor; completion only logs through ZiLog and responds.
GET ignores Idempotency-Key and does not create request rows. RequestStatus no
longer includes Failed: compensated requests are absent, not failure receipts.
Catalog-index test assertions were adjusted after removing RequestOutcome;
durable saga type names are unchanged for the remaining definitions.
Authentication paths still need their outstanding saga work. This source
inventory is not runtime proof. Builds and test execution remain deferred.
Do not treat the current source as satisfying crash/retry acceptance. Complete
the source batch before rebuilding and exercising PostgreSQL recovery tests.

Major remaining gate-5 work includes complete role-deletion boundary coverage,
multi-record management/catalog publication integrity, complete per-operation
HTTP coverage, and facade/authority race checks. CLI/browser acceptance, example
replacement, full local ping, federation, migration/key rotation and replicated
deployment checks remain in the original plan; none is waived by this slice.

## Milestones

These entries distinguish implemented functionality from full acceptance.
The dated continuation above identifies the current build and any regressions;
historical command notes below do not supersede it.

| Gate | Status | Evidence / remaining work |
| --- | --- | --- |
| 1: expanded schema | complete | All 22 records register in PostgreSQL. New-row roundtrips, required app-qualified keys, cross-app rejection, per-app allocation, and app authority tests pass. Transitional old-flow fields remain migration inputs and may not be used by new server operations. |
| 2: `zumd` skeleton | complete | `Makefile.am` declares and links the installed target with zrest/Zdb through a private convenience library. The installed `libZum` contains only the service client and resource-token verification and has no Zdb/server/schema linkage. `--once` activated against PostgreSQL and shut down cleanly; `Requests` admission follows Zdb up/down callbacks. |
| 3-4: bootstrap | complete | Fresh PostgreSQL reached `AdminPending`; restart preserved IDs/counts, a persisted `Core` boundary resumed, and a wrong database key failed promptly. Browser enrollment/`Ready` remains intentionally at gates 5-6. |
| 5: REST/OIDC server | automated acceptance passes | All 68 administrative operations have observed success/401/403 and standby503 coverage; applicable invalid-input, conflict, precondition, redaction and cross-app cases pass. Local OAuth/OIDC, TLS federation and PostgreSQL staged saga recovery pass. Native shutdown accounting passes retained-link TCP/TLS and PostgreSQL restart regressions. Logging uses ZiLog, not an audit table. Actual-browser and external-provider conformance remain external gates. |
| 6: admin CLI | implemented; browser acceptance pending | Real CLI login, enrollment and administrative mutations pass using the virtual browser/passkey driver, including HTTPS with a private test CA. An actual browser/platform authenticator has not been verified. |
| 7-8: examples | automated acceptance passes | Real zumpingd and zumping run; CLI enrollment and idempotent catalog publication across service restart pass. Same-service delegated login passes with unchanged service/client configuration. Missing/invalid service secrets, unavailable `zumd`, unavailable `zumpingd`, restart recovery and cross-app isolation pass. |
| 9-10: local ping | automated acceptance passes | Real user login, ping/pong and refresh pass with both servers running. Role-removal denial/restoration, other-app/admin token rejection, service restart and graceful shutdown pass. Actual-browser/platform-authenticator acceptance remains external. |
| provider completion | automated local-provider fixture passes | Independent downstream client and upstream TLS fixture pass ID-token mapping, `ClaimValues` and N:1 mapped-role policies, UserInfo source/subject validation, freshness, refresh/outage, local-admin availability and persisted identity reuse. Real-provider/Okta, browser logout/conformance and provider-isolation acceptance require external infrastructure. Replication/failover semantics remain a Zdb concern. |
| offline maintenance | automated flows pass | Explicit legacy-schema migration, staged migration-saga recovery, completed rerun, bootstrap handoff, resumable sensitive-field key rotation and archive restore pass against PostgreSQL. Migration covers the defined pre-application source schema; arbitrary process-kill boundaries and other historical schemas are not claimed. |

## Commands and results

- `wc -l GUIDELINES.md zum3.md`: inspected the complete authoritative guide
  and implementation contract.
- `git status --short`: recorded dirty-tree boundaries; no build run.
- `make -f Makefile -f ../util/zum3.mk -j8 zum3-fbs`: failed before C++
  compilation because new schemas placed `include` after declarations.  Make
  also automatically invoked Automake and `config.status` because the canonical
  input was newer; this was unintended and no database or program was run.
  Subsequent supplemental commands suppress configured-file remaking with
  `-o Makefile -o Makefile.am`.
- `make -o Makefile -o Makefile.am -f Makefile -f ../util/zum3.mk -j8 zum3-fbs`
  in `zum/src`: generated the 13 added FlatBuffers headers successfully.
- `make -o Makefile -o Makefile.am -j8` in `zum/src`: incremental clang debug
  build passed after registering all 23 rows and again after converting action,
  role, and scope keys to application-qualified indexes.
- `make -o Makefile -o Makefile.am -j8 && make -o Makefile -o Makefile.am test`
  in `zum/test`: 20 test programs/subtests passed.  Coverage includes all 13
  added row roundtrips, the 70-operation catalog, app-isolated metadata keys,
  duplicate action/role/scope identities across two apps, cross-app reference
  rejection, per-app action allocation, and app authority resolution.
- `git diff --check -- zum`: passed.
- `make -o Makefile -o Makefile.am -f Makefile -f ../util/zum3.mk -j8 zum3-zumd`
  in `zum/src`: linked the new installed-target source without regenerating the
  configured Makefile.
- Started the supplied PostgreSQL 16.3 test cluster, created disposable database
  `zum3_test` from `template0`, and enabled its required `uint` and `libz`
  extensions.  `template1` has a pre-existing libc collation-version mismatch,
  so its metadata was deliberately not modified.
- `./libtool exec ./zum/src/zumd --once` with the supplied Zdb module and
  `host=/tmp dbname=zum3_test`: passed; Zdb elected the standalone node,
  activated, printed `zumd: active`, and stopped cleanly.  PostgreSQL inspection
  showed exactly the 23 planned `public.zum.*` tables plus Zdb's own `meta`,
  `mrd`, `saga`, `saga_step`, and `saga_type` infrastructure tables.
- The first PostgreSQL activation exposed invalid `ORDER BY  LIMIT` SQL for
  grouped indexes without trailing order fields.  `ZdbPQ` now omits `ORDER BY`
  for that valid metadata shape; its incremental clang debug rebuild passed.
- `make -o Makefile -o Makefile.am -f Makefile -f ../util/zum3.mk -j8
  zum3-zumd` after adding server bootstrap: passed.  Bootstrap validates a
  base64-encoded 256-bit `ZUM_DB_KEY`, persists an HMAC-SHA-256 key check, and
  stores the generated ES256 private key in a versioned AES-256-GCM envelope
  bound to issuer/record/field associated data.
- `make -o Makefile -o Makefile.am -j8 ZumTest && ./ZumTest` in `zum/test`:
  all 20 subtests passed after adapting enrollment to complete the precreated
  pending bootstrap user.
- Created disposable `zum3_bootstrap_1` from `template0`.  First `zumd --once`
  run produced a mode-0600 capability file and 23 Zum tables.  The original
  schema-3 fixture contained the then-current 70 management actions and one
  role/scope; it is retained only as historical bootstrap evidence.
  The signing-key envelope is 66 bytes for a 32-byte P-256 private scalar and
  begins with the expected version/algorithm/key-ID header.  A repeat run with
  the same key exited zero without changing seeded counts or the persisted IDs.
- The initial wrong-key negative run exposed exception-path shutdown that left
  Zdb running.  `zumd` now drains request admission and stops/finalizes Zdb and
  the multiplex on both normal and exceptional exits.  Repeating the check with
  a different validly encoded key exited 1 within one second.
- Created disposable `zum3_resume_1`, moved the persisted bootstrap marker back
  to the committed `Core` boundary, removed only its test capability grant, and
  restarted with a distinct protected output path.  It returned to
  `AdminPending` with 70 actions, one replacement capability, and a mode-0600
  output file.
- Regenerated `zum_saga_fbs.h` after adding `appID` to authorization-code
  families.  The targeted clang debug build and all 20 `ZumTest` subtests pass;
  the opaque-token test now proves application identity survives authorization
  and refresh-family preparation.
- Application-qualified authority loading now resolves the active application,
  uses its action ceiling/version, and loads local roles from the active
  `(appID,userID)` membership.  The legacy user role vector is used only for
  transitional app ID zero tests.
- Created disposable `zum3_http_2`.  Explicit `--bootstrap-reissue` produced a
  distinct mode-0600 enrollment URL and rotated the persisted capability:
  the old token returned HTTP 400 and the replacement returned HTTP 200 from
  `/passkey/begin`.  Live/ready/admin pre-bootstrap checks returned 200/503/503
  with status-only or structured error bodies.
- Adding the generic administrative request family initially exposed an ODR
  layout mismatch in the supplemental build: `zumd-zumd.o` did not depend on
  `ZumDaemon.hh`, so its inline parser initializer used the old request-union
  size.  A debugger confirmed the differing member offsets.  The supplemental
  rule now names both daemon headers; rebuilding the object restored HTTP
  operation.  This affects only the temporary no-reconfigure build path.
- The authenticated administrative router now dispatches issuer/operation
  discovery and all 22 table-backed query operations.  Application-nested
  query routes filter records by the path application instead of exposing a
  cross-application scan.  Credential public keys, server session handles, and
  the previously protected secret fields are omitted from these projections.
- Administrative authorization now has two independent checks: the access
  token must carry the exact core `Zum.<operation>` action, and the current
  database state must prove either an active bootstrap-superuser membership and
  role or an active target-app `admin_access` delegation for the user/service
  actor.  Global operations remain bootstrap-superuser-only initially.
- The focused `zumd` link and all 20 `ZumTest` subtests passed after the query,
  redaction, and target-delegation changes.  Pagination/filter semantics and
  management mutations remain part of the unverified REST gate.
- Query items now carry row-version ETags where the schema has a `version`
  field.  Ten named lifecycle routes share strict `{state}` parsing, require
  `If-Match`, reject stale/missing preconditions, preserve terminal revocation,
  and increment the row version on a committed change.
- Initial mutation dispatch exists for `userInvite`, `actionAdd`, `roleAdd`,
  `membershipAdd`, and `membershipRoles`.  Role replacement validates every
  role against the target application's active catalog and rejects duplicates.
  These handlers are not yet gate-complete: invitation capability delivery,
  persisted idempotency replay, audit writes, reference checks on membership
  creation, and saga-backed multi-row authorization-version changes remain.
- Non-interactive authority loading now requires an active owning application
  and an active `client_access` row for the target application.  The selected
  audience, scopes, and workload roles come from that approval rather than the
  client row's transitional authority vectors.  App ID zero remains only for
  the existing legacy unit fixtures and is not used by new enrollment.
- Administrative idempotency keys are now persisted in `zum.request`, bind the
  actor, operation, target, precondition, and request body, and reject digest
  reuse.  Completion status is persisted before the response.  Result IDs and
  operation-specific replay responses are not yet persisted, so secret-loss
  recovery and full replay semantics remain incomplete.
- The core catalog now contains 70 one-to-one management actions plus three
  OAuth-facade actions.  Bootstrap seeds distinct `superuser` and `appService`
  roles and `zum.admin` and `zum.service` scopes; enrolled native services get
  the limited service role, never the superuser role.  Invitation and recovery
  issue bound one-use capabilities, and client-secret rotation supports a
  bounded previous-secret overlap without replaying plaintext.
- `make -o Makefile -o Makefile.am -f Makefile -f ../util/zum3.mk -j8
  ZumDaemon.o zumd-zumd.o`, the corresponding `libZum.la` relink, and
  `make -o Makefile -o Makefile.am -j8 ZumTest && ./ZumTest` passed.  All 20
  subtests and 239 assertions pass after the query, enrollment-capability,
  delegation-ceiling, idempotency-result, catalog-taxonomy, and secret-overlap
  slice.
- Created disposable schema-4 database `zum3_schema4_20260910`.  Bootstrap
  created exactly 23 Zum tables, 73 actions (IDs 0..72), two roles, two scopes,
  one client, and a mode-0600 enrollment URL.  Restart with the same database
  key completed cleanly without reseeding.  The first run initially crashed
  during finalization because the supplemental target had reused a stale
  `ZumBootstrap.o` built against the pre-rotation `Client` layout.  Rebuilding
  that object fixed both the PostgreSQL allocator warning and crash; the
  supplemental make fragment now includes dependency files for every private
  `zumd` object so header layout changes cannot recur silently.
- The PostgreSQL warning was subsequently traced to `zbitmap_recv`: the wire
  count is in 64-bit words while `zu_bitmap_new_` takes a bit count.  Converting
  words to bits before allocation eliminated the overwrite.  The extension was
  incrementally rebuilt against PostgreSQL 16.3; disposable database
  `zum3_bitmap_fix_20260910` bootstrapped and restarted without warnings, with
  the expected 73-bit superuser and five-bit service-role bitmaps.
- Every `MgmtOp` now has an explicit daemon dispatch case.  Provider client
  secrets and database-held signing material use the server AES-GCM envelope
  and are redacted from projections.  Catalog publication resolves stable
  app-owned action/role/scope definitions and records monotonic revision/digest,
  but removed-definition retirement and saga recovery remain unverified.
- Replaced `zum.cc`'s offline Zdb bootstrap implementation with the administrative
  REST client.  It loads non-secret issuer/client configuration, performs a
  finite browser authorization-code/S256-PKCE flow through a loopback callback,
  persists refresh/access tokens only to a configured mode-0600 file, dispatches
  the shared operation catalog's actual GET/POST/PUT/PATCH/DELETE method, expands
  and percent-encodes path fields, turns GET JSON fields into query parameters,
  sends mutations as JSON, and maps version/idempotency controls to headers.
  Potentially secret-producing enrollment/client creation/rotation requires an
  explicit protected output file and never prints that response to the terminal.
- Added registry-derived method discovery.  Known administrative paths called
  with the wrong method return 405 and an `Allow` header; a unit test covers
  multi-method collections, parameterized paths, query stripping, and unknown
  paths.  The incremental clang debug `libZum`, `zumd`, and `zum` links passed;
  all 20 `ZumTest` subtests and 242 assertions pass.
- Created disposable database `zum3_http_contract_20260910`, then ran the linked
  `zumd` on port 18080.  `GET /health/live` returned 200, readiness correctly
  returned 503 in `AdminPending`, `DELETE /admin/apps` returned 405 with
  `Allow: GET, POST`, and an unknown admin path returned 400.  Bootstrap again
  seeded 73 actions, two roles, two scopes, and a mode-0600 capability file.
  The server stopped cleanly and the temporary capability file was removed.
- `./zum --help` shows only the planned login/operation surface.  Loading a
  temporary valid config and naming an unknown operation rejected it before any
  connection attempt.  Authenticated CLI integration remains pending because it
  requires completing the real browser/passkey bootstrap ceremony; no production
  bypass was added.
- Moved the `MgmtOp` enum implementation and route registry into its own shared
  wire translation unit.  The canonical and supplemental `zum` targets now link
  that unit directly with zrest/zhttp and their transport dependencies, rather
  than linking server/database `libZum`.  `ldd .libs/zum` confirms that the admin
  client has no `libZum` or `libZdb` dependency.  The CLI no longer includes the
  server/database model through `ZumOAuth.hh`; its small form decoder is local
  protocol code and the management header contains only wire catalog types.  The
  post-split `zum` link and all 20 unit subtests pass.
- Schema version 5 persists the exact granted scope string and original OIDC
  nonce in grants and authorization-code families. Identity scopes are selected
  from each enrolled client's `identityScopes`, participate in the same
  downgrade-only refresh ceiling as resource scopes, and are rejected from the
  client-credentials grant. The `openid` scope causes authorization-code and
  refresh exchanges to return an ES256 ID token; `profile` and `email` release
  only their corresponding standard claims.
- Added OIDC discovery at `/.well-known/openid-configuration` and UserInfo at
  `/userinfo`. UserInfo verifies the issuer, signing key, signature, time,
  active client, active user, and `openid` scope before releasing claims from
  current local user state. A live PostgreSQL run initially exposed that these
  routes were absent from `zumd`'s composite request list despite passing the
  reusable-profile unit test. The daemon route list and its Basic/Bearer response
  header union were corrected. Live discovery then returned 200 with the ES256,
  public-subject, identity-scope, and claim metadata; unauthenticated UserInfo
  returned 401 with `WWW-Authenticate: Bearer error="invalid_token"`.
- `make -o Makefile -o Makefile.am -f Makefile -f ../util/zum3.mk -j8
  libZum.la zumd zum` passed after the OIDC changes. `make -o Makefile -o
  Makefile.am test` in `zum/test` passes all 21 subtests, including nonce and
  exact-scope persistence, identity-scope selection, ID-token signature/claims,
  UserInfo projection, and the delegated OIDC authorization-code ceremony.
- Fresh disposable PostgreSQL database `zum3_oidc_20260910` bootstrapped schema
  version 5 with 73 actions, two roles, and two scopes. A same-key restart was
  idempotent and the enrollment artifact remained mode 0600. Disposable database
  `zum3_oidc_http_20260910` supplied the live OIDC discovery and UserInfo HTTP
  checks above.
- A final verification command was mistakenly launched as top-level `make test`
  instead of from `zum/test`. It entered the normal recursive build, regenerated
  `zum/src/Makefile.in` and `zum/src/Makefile` through Automake/config.status,
  and began compiling the canonical `zumd` object before being interrupted. No
  downstream module after `zum` was entered and no source edit resulted, but
  this nevertheless violated the no-reconfigure rule. Verification was rerun as
  `make -o Makefile -o Makefile.am test` in `zum/test`; all 21 subtests passed.
- Added a pooled asynchronous upstream HTTPS transport built on `Zhttp`. It
  validates TLS, prefers HTTP/2 with HTTP/1 fallback, bounds response bodies to
  64 KiB, limits retries and concurrency, reuses clients per origin, and clears
  authorization/body secrets after completion. The daemon owns resolver and
  transport lifetime. An optional public-network probe verified both HTTP/1 and
  HTTP/2 response assembly; the default transport test is deterministic.
- Upstream OIDC discovery now validates and retains `userinfo_endpoint` when
  configured as the claim source. The token exchange requires an access token,
  validates the ES256 ID token first, calls UserInfo with Bearer authentication,
  requires an exact subject match, and only then maps role/eligibility claims.
  Provider-qualified identity tests prove that equal subjects from different
  providers do not merge.
- Schema version 7 adds the authority provider identifier to grants and carries
  provider, policy, evidence, and source provenance through authorization-code
  and refresh-family persistence. Delegated code redemption and refresh now
  require an active compatible application policy/provider and unexpired
  eligible evidence, re-evaluate active role mappings from retained raw claims,
  intersect with the original action ceiling, and cap access-token expiry at
  the evidence deadline. Expired evidence produces `invalid_grant`; it never
  falls back to local assignments.
- The clang-debug runtime test proves current mapping removal immediately
  restricts a pre-existing delegated code, mapping restoration cannot bypass
  the original ceiling, a deadline at 275 caps a token issued at 250 to 25
  seconds, and refresh at 276 fails. The combined focused suite passes:
  `Files=2, Tests=24` (`ZumTest` plus `ZumUpstreamTest`).
- Fresh disposable PostgreSQL database `zum3_schema7_20260910` bootstrapped and
  restarted idempotently with schema version 7, bootstrap phase `AdminPending`,
  73 actions, two roles, two scopes, and a mode-0600 enrollment artifact. A live
  run returned 200 for liveness and OIDC discovery and the expected 503 for
  readiness until the initial administrator completes passkey enrollment.
- Replaced the administrative query cursor placeholder with an opaque,
  operation/app-bound HMAC-SHA-256 keyset cursor over each table's primary key.
  Filtered queries scan bounded chunks and continue correctly across sparse
  matches; callers cannot alter a cursor or reuse it for another operation/app.
- Schema version 8 binds authorization transactions, codes, and refresh families
  to the initiating native service client.  `/service/authorize`,
  `/service/token`, and `/service/revoke` require the corresponding exact
  workload action and recheck current service-client/application ownership.
  Direct token redemption cannot consume a facade-bound code.  OAuth
  `resource`, `prompt`, and `max_age` are parsed as typed fields and the resource
  indicator must match the selected audience.
- Added public `Zum::Service`, with injected asynchronous HTTP transport,
  workload authentication, discovery/JWKS validation, local resource-token
  verification, typed facade calls, catalog publication, coalesced token renewal,
  coalesced rate-limited unknown-key refresh, and stop/drain handling.  Access
  tokens carry the resource application's `zum_app_id`; test fixtures now prove
  user authority comes from app membership and workload authority from
  `client_access`, without cross-app action inheritance.
- `make -o Makefile -o Makefile.am -f Makefile -f ../util/zum3.mk -j8
  libZum.la zumd zum` passed in the existing clang-debug build.  The current
  focused suite passes `ZumTest` (22 subtests; enrollment runtime 271 assertions)
  and `ZumUpstreamTest` (2 subtests).
- Fresh PostgreSQL database `zum3_schema8_20260910` bootstrapped with schema
  version 8 and restarted idempotently.  Both inspections showed bootstrap phase
  `AdminPending`, 73 actions, two roles, two scopes, and one pending capability;
  the enrollment artifact is mode 0600.  A live run returned 200 liveness, 503
  readiness as expected before enrollment, and valid OIDC discovery, then shut
  down cleanly on SIGINT.
- Schema version 9 adds persisted browser-session authority provenance and
  current-policy/current-evidence revalidation. Local and delegated sessions
  can silently authorize only while their current authority remains valid;
  removed upstream role mappings and expired evidence fail closed.
- Added identifier-first `POST /login`, explicit-consent staging and
  `POST /consent`, local provider `GET /login` and CSRF-protected
  `POST /logout`, and POST as well as GET UserInfo. Pending consent authority is
  stored in the authorization grant and reloaded/revalidated before approval;
  `prompt=none` returns `consent_required` rather than presenting UI.
- The first CSRF implementation used the 66-byte opaque session token directly
  with picotls HMAC, whose low-level API accepts at most the SHA-256 64-byte
  block size. Valgrind located the resulting one-byte overwrite. CSRF now keys
  HMAC with the token's independently parsed 32-byte digest. The normal focused
  suite passes all 22 subtests and 292 enrollment-runtime assertions.
- Production bootstrap no longer uses `ZmBlock` wrappers around Zdb operations.
  `serverBootstrap` is callback-based and its private `.cc` state machine chains
  all table finds/inserts/updates through continuations. The main executable
  waits only at its top-level startup boundary. The synchronous `insertRecord`
  helper remains test-fixture-only. `DBContext` and tuple scans now name the
  `ZdbTableDerive` table types explicitly; no hand-written explicit template
  instantiations were introduced.
- Disposable PostgreSQL database `zum9_async_20260910` bootstrapped schema 9
  with 1 issuer, 1 app, 73 actions, 2 roles, 2 scopes, and the expected single
  policy/client/user/membership/signing-key/grant rows. Its enrollment artifact
  is mode 0600. Restart preserved every count and the artifact's size and mtime.
- Earlier work incorrectly used supplemental makefiles and make `-f`/`-o`
  overrides. These were removed; `Makefile.am` is authoritative and ordinary
  incremental `make -j3` is used for `zum`, allowing normal Makefile regeneration.
- The post-fix Valgrind run has no CSRF/HMAC error and all assertions pass. It
  still reports three invalid teardown reads from the Grant-table cache, rooted
  in the existing `EnrollmentFinish_::saga_()` grant deletion, plus a 32-byte
  leak. This is tracked separately from the consent/CSRF work.
- `Makefile.am` now fully owns the build additions. The temporary `zum3.mk`
  fragments were deleted; shared server/test upstream code is an Automake
  convenience library. Ordinary `make -j3` regenerated the two Zum Makefiles
  and built `libZum`, `zumd`, `zum`, `ZumTest`, and `ZumUpstreamTest`.
- Application enrollment is a recoverable 12-step Zdb saga. The plaintext
  generated client secret remains response-only; the saga serializes only its
  one-way verifier. Recoverable secret fields use protected-envelope naming,
  and `ZUM_DB_KEY` remains environment-only with no configuration fallback.
- `make -j3 test` in `zum/test` passed both binaries: 24 TAP tests total.
- `make -j3` followed by `make -j3 test` in `zum/itest` passed the preserved-memory
  restart test. It proves an unfinished enrollment resumes to publication and
  a deterministic late-step failure rolls back its already-written app,
  audience, client, and client-access rows.
- After the restart fixture change, ordinary `make -j3 libZum.la zumd zum` and
  the two focused suites passed again (`zum/test`: 24 TAP tests;
  `zum/itest`: 2 restart tests). No target beyond `zum` was built.
- Server/database implementation now builds into the private
  `libZumServer.la` convenience library used by `zumd` and server tests.
  Installed `libZum` headers are limited to the service client, public wire
  types, and access-token verification; its shared object exports only that
  surface and has no Zdb, zrest, zhttp, Zfb, or generated-schema dependency.
  `ZumClientTest` links only the installed client library and its non-server
  dependencies. The focused suite passes 25 TAP tests across three binaries,
  and the preserved-memory restart suite continues to pass both tests.
- Management routes now classify audit behavior from the same method/path
  registry: every non-GET operation writes an `Administration` audit record
  after idempotency finalization, with exact operation ID, authenticated actor,
  target app/path, outcome, and per-request correlation ID. Query routes do not
  recursively grow audit. The registry test checks this invariant for all 70
  operations; the focused suite still passes 25 TAP tests and both restart tests.
- `actionAdd` now submits `AppActionAdd`, a four-step Zdb saga that reserves
  the app, inserts and publishes the action, then releases the app. Unit tests
  prove successful publication, stale-version rejection, duplicate-name rollback
  restoring the counter and both versions, preservation of the existing action,
  and successful subsequent allocation. Payload serialization also passes.
  After settling the header, the following sequential command completed without
  diagnostics from `zum/src`:
  `make -j3 && make -C ../test -j3 && make -C ../test -j3 test && make -C ../itest -j3 && make -C ../itest -j3 test`.
  Unit result: 3 binaries, 25 TAP tests passed. Preserved-memory restart result:
  1 binary, 2 TAP tests passed. `git diff --check -- zum` passed.

## Contract decisions

- Zdb's built-in saga tables remain infrastructure; no `zum.saga` table is
  introduced.
- Existing uncommitted management-catalog files are treated as user work and
  extended in place.
- The live asynchronous request class retains `Zum::Request`; the persisted
  `zum.request` row is named `Zum::IdemRequest` to avoid a C++ type collision.
- Legacy pre-server APIs currently operate on transitional app ID zero.  Their
  action, role, and scope lookups now use the new composite keys; the server
  management implementation will require explicit nonzero app IDs.
- `Makefile.am` is the sole build-system source of truth for these additions.
  Ordinary incremental `make -j3` performs any required Automake regeneration;
  no supplemental make fragments or command-line makefile overrides are used.

## Unverified requirements

- `userRecover` now enforces missing/stale `If-Match` at the administrative
  boundary and carries the expected row version through `RecoveryIssueConfig`
  into `recoveryStart.v2`. The saga checks local identity and the row version
  again while reserving the user, then increments the row version together
  with suspension/auth-version changes. Added stale-version tests at both the
  request and saga layers. `make -j3 && make -C ../test -j3 && make -C ../test -j3 test`
  completed from `zum/src`: 3 binaries, 25 TAP tests passed, including recovery
  stale-version rejection and the successful row-version increment. Actual HTTP
  precondition coverage remains pending.
  Active old recovery sagas must be settled by the required migration workflow
  before opening a store with the new saga catalog.

- Browser passkey completion previously used manual `fetch` redirect handling,
  whose opaque response cannot expose `Location`. The HTTP adapter now converts
  only passkey-finish redirects to 200 JSON `{redirectURI}`, preserving cookies;
  normal navigational OAuth redirects remain unchanged. The provider page reads
  that JSON or renders the server's consent HTML. Added HTTP-adapter unit tests
  for destination escaping, cookies, status, consent, and unchanged errors.
  The extracted page script passed Node VM checks with mocked WebAuthn/fetch
  for navigation, consent, and error paths. Also preserved the administrative
  completion callback when recovery/invitation request admission returns false;
  those paths previously invoked a moved-from callback instead of returning 503.
  Sequential `make -C zum/src -j3 && make -C zum/test -j3 && make -C zum/test -j3 test && make -C zum/itest -j3 && make -C zum/itest -j3 test`
  from the repository root completed successfully: server build, all 25 TAP
  unit tests across three binaries (including the new adapter assertions), and
  both preserved-memory restart tests passed. Ordinary make regenerated the
  integration Makefile after its README distribution entry was added. No
  downstream module was built. Actual browser/passkey login remains pending.

- `roleDelete` currently tombstones only the role. Membership, scope,
  client-access, admin-access, and role-mapping references remain. Since catalog
  publication can restore a tombstoned standard definition, those retained
  references can restore deleted grants. Gate 5 requires the planned recoverable
  reference-cleanup saga before role deletion/publication can be accepted.
  Framework inspection rules out simply nesting child submissions inside a
  parent replay step: `SagaDB::sagaSubmit` admits only active application work,
  whereas activation replays sagas in `Rebuilding` before app admission. The
  current `MSaga` step catalog/count is compile-time `Def::NSteps`. Any variable-
  cardinality cleanup must respect those actual Zdb recovery contracts; do not
  hide a multi-row mutation loop inside a single recorded step.

- `zumrestarttest` previously injected only `ZdbMem::Store`; earlier references
  to its results as PostgreSQL were incorrect. An explicit backend mode now
  reads `ZUM_TEST_MODULE` and `ZUM_TEST_CONNECT` for the saga test. The first
  PostgreSQL run on `zum_saga_pq_20260910_01` completed recovery assertions and
  left zero `zdb.saga` rows, but failed TAP planning. After correcting the fixed
  test plan, `make -j3 && make -j3 test` passed both memory tests, followed by
  `ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so ZUM_TEST_CONNECT='host=/tmp dbname=zum_saga_pq_20260910_02' make -j3 test`.
  That run passed both TAP tests, with the saga scenario using PostgreSQL.
  SQL confirms the successful app is active, the failed app is absent, and no
  unfinished saga remains. This proves Zdb stop/start enrollment recovery and
  compensation, not process-kill or replication behavior. See `itest/README.md`.
- `AppActionAdd` recovery fixture now stages all nine durable boundaries for
  its four steps (payload only, intent pending, effect committed), plus a
  duplicate-name failure after app reservation. The first memory run aborted
  during startup at boundary 6: action ownership had already been released,
  so the name precheck rejected its own committed insertion before Zdb could
  replay its UN. A debugger run confirmed the boundary. The precheck now
  rejects a different action ID and lets the same stable ID reach Zdb replay;
  it no longer treats a later ownership release as a conflicting insert.
  The loop uses `ZuTestRepeat` for correctly planned per-boundary subtests.
  The incremental `make -C zum/src -j3 && make -C zum/test -j3 && make -C zum/test -j3 test && make -C zum/itest -j3 && make -C zum/itest -j3 test`
  rebuilt successfully and passed all 25 unit TAP tests. Its final invocation
  had no explicit database settings and was correctly rejected by the updated
  fixture. Then `ZUM_TEST_MODULE=/home/count0/src/z/zdb_pq/src/.libs/libZdbPQ.so ZUM_TEST_CONNECT='host=/tmp dbname=zum_action_pq_20260910_01' make -C zum/itest -j3 test`
  passed both top-level tests, including all nine action-recovery boundaries
  and duplicate-name compensation, in 11.8 seconds. The fresh PostgreSQL database
  was created from template0 with the required extensions and is retained.
  SQL confirms actions `cut0` through `cut8` have IDs 0 through 8, app 9 has
  next action ID 9/version 11/auth version 10, all ownership is released,
  and both `zdb.saga` and `zdb.saga_step` are empty. Actual HTTP and process-kill
  coverage remain separate requirements, not proved by these staged images.
  Per the user's correction, all restart cases now use PostgreSQL. The
  `ZdbMemStore` include, injection path, and integration link dependency were
  removed. Missing test database settings fail rather than selecting memory.
  Historical preserved-memory results are not PostgreSQL restart evidence.
  `stopDB` waits for request drain followed by `Zdb::stop()`; source inspection
  confirms table close, PostgreSQL queue/in-flight pipeline drain, then shard
  callback drain before stop returns and the fixture finalizes/reopens the DB.

- The new PostgreSQL HTTP fixture (`itest/zumhttptest`, `zumhttp.py`) is built
  but has not yet passed end to end. Runs on fresh databases
  `zum_http_pq_20260910_01` through `_06` reached bootstrap registration begin;
  completion failed with `invalid_request`. A source-tree libtool/gdb run
  established that server entry had a decoded 16-byte ceremony ID and a
  73-byte cookie, but the retained cookie bytes no longer matched its name or
  token delimiter. No secret values were printed. `ZumHTTP.hh` retained
  callback-scoped header spans until body completion, contrary to the Zhttp
  parser lifetime contract. Its cookie/authorization fields and raw query
  storage now own their input, as the management adapter already does.
  Unit regressions overwrite the original query/header buffers before checking
  the captured values. The source rebuild succeeded; the `_07` HTTP run then
  passed cookie validation but failed registration input parsing with
  `access_denied`. A `_08` debugger run identified `WebAuthnError::Fields`.
  The raw request body had the same callback-lifetime defect, so `HTTPData`
  now owns its body as well, with a buffer-overwrite regression. The unit build
  also caught an old byte-span comparison against the now-string query; that
  comparison is corrected. The next source rebuild succeeded. HTTP `_09`
  completed registration, Ready, assertion, consent, and code issuance, then
  returned `server_error` on code exchange. Debugger run `_10` located the
  error at `CodeToken_::start`: the legacy guard required an external signing
  provider reference even for the encrypted local bootstrap key. Code,
  refresh, and client-credentials guards now require exactly one key source
  (external reference or local encrypted material), matching the schema.
  The HTTP fixture also exercises refresh rotation. The unit run crashed in
  the new overwrite test because its input string shadowed a literal; its
  four inputs now use owned, writable span copies. The next incremental build
  succeeded, and all three unit binaries/25 TAP tests passed. HTTP `_11`
  successfully issued a code token and rotated its refresh token, but the
  initial admin app query returned 403. Inspection found same-expression moves
  of `principal` and `target` into a continuation while passing them by reference
  to `adminAccess_`; the checker could therefore observe moved-from identity.
  The management handler now snapshots only subject/authentication kind and
  parsed app targeting before moving the full request. The matching idempotency
  hazard is fixed by calculating its digest before moving request fields, and
  copying its short key before transferring the continuation. No full request
  body or action-vector snapshot is needed. Source rebuild and HTTP retest are
  succeeded. HTTP `_12` still denied the user lookup. A `_13` debugger trace
  located the denial in the handle-index lookup; PostgreSQL confirmed the
  active user retained a zero-length handle. Enrollment and recovery mutate
  `User.handle`, but its metadata omitted `Mutable`, so neither durable updates
  nor secondary-index maintenance included it. The field is now mutable and a
  unit regression updates a pending user's handle, then resolves it through
  the secondary index. A batched source/unit/integration rebuild is pending;
  no successful end-to-end HTTP claim yet.
  The source portion of that rebuild succeeded. HTTP `_14` passed bootstrap,
  login, code/refresh issuance, app/action creation, enrollment retries/conflict,
  filtered GET, missing/stale ETags, then restarted and logged in again.
  Its final action-ID comparison failed: creation encoded uint32 action IDs as
  strings while query metadata encoded numbers. Creation now emits numbers and
  the fixture asserts matching types before restart as well. SQL confirms the
  32-byte user handle, updated app label/version 4, and active `ping` action 0
  survived, with zero saga/saga_step rows. Complete HTTP PASS is still pending;
  the current queued unit/integration build must finish before the next rebuild.
  A follow-up mutation audit also found grant fields `roleIDs`,
  `authoritySource`, `authorityProviderID`, and `scope` lacking `Mutable` while
  authorization/refresh paths assign them (`ZumOAuth.cc::authorizationFinish`,
  `ZumAuthorize.cc`, and `ZumDB.hh`). Their durable transition tests and metadata
  correction were deferred until the live rebuild ended. That build completed
  successfully with 25 unit TAP tests passing. A focused PostgreSQL run on
  `zum_restart_pq_20260910_14` passed both restart/recovery tests (11.8 seconds).
  All four grant fields are now mutable; `grantUpdate` in `zumrestarttest`
  performs an update, drains/stops/finalizes Zdb, then checks their values after
  reopening PostgreSQL. This checks metadata persistence, not full upstream SSO.
  The next batch also separates admin bearer validation (401) from missing
  operation authority (403), with an HTTP workload-token regression using the
  enrolled service's limited role. The incremental source/unit/integration build
  is running; these latest changes are not yet verified.
  Its source portion completed successfully. HTTP `_15` passed admin login and
  enrollment, then the new service client-credentials check failed with
  `invalid_scope`. `AuthorityLoad_` selected target-approved client-access scopes
  but retained the client's legacy home-app audience list. Candidate resolution
  now loads each scope's audience through the approved audience IDs, checks
  target app/state/ownership, and constructs the candidate audience list from
  those authoritative rows. This also rejects disabled or cross-app audiences.
  The preceding build completed successfully. The focused PostgreSQL suite on
  `zum_restart_pq_20260910_15` then passed all three tests (13.9 seconds),
  including the new grant-update persistence check. The audience-resolution
  change is now rebuilding incrementally with subsequent unit/itest relinks.
  The full HTTP flow is still pending. Also audit the remaining legacy
  `tokenRelease`/`refreshFinish` issuer-global auth-version checks before claiming
  independent multi-app issuance: they currently compare against issuer state
  although callers derive versions from the target app.
  `zumd: listening` is a non-secret startup event emitted after server start,
  allowing the fixture to wait without readiness polling.
  The HTTP fixture subsequently passed in full on PostgreSQL database
  `zum_http_pq_20260910_16` (one TAP test, 3.0 seconds), including workload
  issuance, 401/403 separation, retries, ETags, and post-restart login/data.
  Older unit workload fixtures omitted audience rows and access audience IDs;
  those fixtures now supply them. Additional checks reject missing, foreign,
  disabled, and unapproved audiences, and the successful cross-app case uses
  a distinct legacy home-app audience to prove target approval is authoritative.
  A test-only conditional enum narrowing error was corrected; the incremental
  unit build succeeded. The new workload checks all passed; one SSO fixture
  still used audience ID zero in its preseeded consent while its scope now
  uses ID 7. The consent fixture is aligned; the final ordinary incremental
  clang-debug build and `make -C zum/test test` passed (three binaries,
  25 TAP tests, 1.6 seconds).
  Both PostgreSQL integration fixtures passed through `make -C zum/itest test`
  on `zum_restart_pq_20260910_17` and `zum_http_pq_20260910_17`
  (two binaries, four TAP tests, 16.4 seconds). There is no in-memory
  restart/persistence acceptance path.
  Follow-up: `tokenRelease`, `refreshFinish`, and CodeFamily's activation step
  now check the target application's active/unowned state and authorization
  version, not the legacy issuer-global counter. Release and rotation also
  bind the refresh family to that application. Workload issuance retains the
  resolved target app ID through asynchronous signing (not its owning app ID).
  The changed saga contract is `codeFamily.v5`; old in-flight saga payloads
  must be settled before migration, not silently replayed under new semantics.
  Unit fixtures now give early code/refresh grants a real app, and cover
  different issuer/app versions, missing/zero/disabled/owned apps, stale
  versions, and wrong-app families (including equal app versions).
  Verification passed: `make -C zum/src -j3 && make -C zum/test -j3 &&
  make -C zum/itest -j3`, followed by `make -C zum/test test` (25 TAP tests,
  1.6 seconds). Focused `prove -j1 zumhttptest` on PostgreSQL
  `zum_http_pq_20260910_18` passed (3.2 seconds), and `prove -j1 zumrestarttest`
  on `zum_restart_pq_20260910_18` passed all three tests (13.3 seconds), using
  the documented test module/connect variables. The HTTP test exercises the
  updated code/refresh paths; staged restart coverage remains enrollment/action
  sagas and grant-field persistence, not every CodeFamily boundary.
  Remaining client/membership/policy version fences and app-local mutation
  versioning still require their own audit.
  The generic administrative state handler now guards saga ownership and
  version exhaustion, increments a record's `authVersion` when that field
  exists, and preserves both versions on unchanged-state requests. HTTP
  coverage adds application and client-access suspend/resume with denied
  issuance, successful reactivation, ETags, and post-restart versions.
  On PostgreSQL `_19`, app lifecycle checks passed, then client-access GET
  crashed. A `libtool exec gdb` reproduction on `_20` located dangling
  `ZuFwdTuple` references captured by `adminFind` across a scheduler post.
  The query/state/update/replacement adapters now accept owning table key
  types; replacement callers snapshot their keys before moving the value.
  HTTP coverage also exercises string-keyed client updates and client-access
  replacement. The previous unit suite passed; the corrected daemon is
  rebuilding and the expanded HTTP fixture is not yet passing evidence.
  The corrected incremental source/test/itest build subsequently passed.
  `make -C zum/test test` passed all 25 tests (1.6 seconds); the expanded
  `prove -j1 zumhttptest` passed against PostgreSQL
  `zum_http_pq_20260910_21` (3.7 seconds). This verifies string-keyed query,
  update, replacement, state transitions, denied/resumed workload issuance,
  and durable versions/client labels after a drained server restart.
  Saga-ownership/version-exhaustion guards are source-reviewed, not separately
  fault-injected by this HTTP fixture. Grant `clientVersion` and
  `membershipVersion` remain unpopulated by issuance paths; role/action changes
  still need recoverable app-version updates, and role deletion still needs
  reference cleanup. None of those wider requirements is satisfied by this fix.
  A follow-up authority audit found that local code/refresh loading retained
  grant role IDs without reloading the local membership. `AuthorityLoad_` now
  loads the app/user membership, rejects missing, inactive, or owned rows, and
  resolves its current roles against the existing scope/action ceiling.
  External assignments still use provider/evidence/mapping authority, not local
  membership. New isolated regressions cover suspension, ownership, removed
  roles, restored roles, and an empty original action ceiling. The source build
  passed; dependent unit/itest builds and runtime verification are pending.
  This does not implement persisted client/membership-version race fencing.
  The PostgreSQL HTTP fixture passed on `_22` (3.7 seconds). Unit membership
  suspension/ownership/removal/restoration checks passed, but the new empty
  action-ceiling case failed: variable-length bitmap `&=` leaves left-hand
  trailing words unchanged when its right operand is shorter. Zum's existing
  `intersectActions` now truncates to the limit's length after intersection,
  and every Zum authorization bitmap intersection uses that helper. Unit
  coverage includes empty and shorter ceilings with high action IDs. The
  corrected source/unit/itest build is running; the ceiling fix is not yet
  runtime-verified.
  Further source audit: `ZumDB.hh::authorizationFinish` still compares the
  supplied app authorization version to `Issuer.authVersion` before issuing a
  code. This is another pre-token issuer-global check to migrate; the existing
  HTTP fixture logs into the core app and does not prove independent app login
  after its authorization version diverges. Prior token release/refresh fixes
  do not satisfy that requirement.
  Verification of current-membership resolution and action-ceiling fixes:
  incremental `make -C zum/src -j3 && make -C zum/test -j3 &&
  make -C zum/itest -j3` passed; `make -C zum/test test` passed all 25 tests
  (1.6 seconds), including the previously failing empty-ceiling regression.
  `make -C zum/itest test` passed all four PostgreSQL tests (16.9 seconds),
  using `zum_restart_pq_20260910_23` and `zum_http_pq_20260910_23` with the
  documented module/connect variables. No full provider-completion claim.
  The remaining `authorizationFinish` pre-code check now resolves the grant's
  real application and validates active/unowned state plus its authorization
  version, rejecting app ID zero. The final grant update retains that app
  binding. Test-only passkey fixtures now supply their real app ID, and new
  cases exercise successful code issuance with app version 17 versus issuer
  version 10, plus stale/missing/zero/disabled/owned app rejection.
  The incremental source/unit/itest build is running; these latest changes
  have not yet passed runtime verification. Independent-app HTTP login remains
  a required broader acceptance check, not proved by these isolated cases.
  Independent-app login audit while that build remains active:
  `AuthorizeRequest_::issuer_` still seeds ceremonies with issuer-global
  `authVersion`, and `scope_` reads legacy `client.scopeIDs`/audiences rather
  than target-approved `client_access`. `clientAdd` intentionally does not
  populate those legacy fields. The next authorization-request change must
  resolve the real app and approved audience/scope catalog together; do not
  patch enrollment to populate legacy fields or claim independent-app login
  based only on core-app HTTP tests.
  `zumhttp.py` now provisions an independent native OAuth client through REST,
  with app-local ping role/scope, local user membership, and explicit
  client-access approval; `login` accepts that client/scope/resource. The first
  `_24` run reached client-access creation and exposed a fixture expectation
  error (new PUT correctly returns 201); the fixture was corrected. PostgreSQL
  `_25` passed all existing core/lifecycle/restart checks and the entire new
  administrative setup, then `/authorize` returned 302 instead of the expected
  login page. The expanded HTTP test remains failing until request-side
  authority resolution is implemented. Source build passed; its dependent
  unit/itest build is still active.
  The unit relink then completed and `make -C zum/test test` passed all 25
  tests (1.6 seconds), including independent app/issuer version code creation
  and invalid app-state cases. Only the integration-test relink remains active;
  the new independent-app HTTP failure above remains unresolved.
  Implemented shared asynchronous `clientScopes` loading for interactive
  authorization: resolve active/unowned app, load current client-access
  approval, and load each active scope through an approved, active same-app
  audience. Legacy client audience/scope lists are cleared, not populated as
  enrollment compatibility data. No resource approval is required for
  identity-only scopes; inactive/owned approval fails closed.
  `AuthorizeRequest_` now uses the resolved app version and catalog, and
  `AuthorityLoad_` reuses the loader for code/refresh/passkey authority checks.
  Unit browser fixtures now provide explicit client-access records. The
  incremental `-j3` source/unit/itest build is running; this implementation
  has not yet passed tests or the independent-app HTTP scenario.
  The independent-app fixture now verifies ES256 access-token signatures using
  the server JWKS and checks issuer, audience, client/app IDs, scope, expiry,
  and exactly the ping action. The loader build found a `ZmFn` variadic
  deduction error from untyped empty result arguments; its error path now uses
  explicit `App{}`, `Client{}`, and `ScopeVec{}`. The failed build fully exited
  before the ordinary incremental source/unit/itest build was resumed.
  The resumed source/unit/itest build passed; all 25 unit tests passed with
  shared scope loading (1.6 seconds). PostgreSQL HTTP `_26` now reaches the
  independent app's login and consent and receives a code, but code exchange
  returns `invalid_grant`. SQL shows its grant has kind Code but state Pending:
  pure `authorizationFinish` changed the kind without leaving the consent
  state. It now sets Active on successful issuance, with a Pending-consent
  unit regression. The next incremental build is running; full independent-app
  token verification remains pending.
  The consent-state correction's incremental source/unit/itest build passed,
  followed by all 25 unit tests (1.6 seconds). PostgreSQL HTTP `_27` passed
  independent-app login, consent, code exchange, refresh, and signature/claims
  verification (4.0 seconds). `_28` additionally passed role-removal narrowing,
  no re-expansion after role restoration, and membership-suspension refresh
  denial before/after another drained restart (4.9 seconds).
  `_29` passed with explicit `openid ping`, independent ID-token ES256 and
  nonce/client-audience verification, plus those access/lifecycle checks
  (4.6 seconds). The focused command must run from `zum/itest`; one invocation
  from the workspace root failed to locate the Python fixture before touching
  the DB, then passed from the documented directory. This verifies a native
  independent local OIDC client; confidential-client/provider conformance,
  upstream federation, CLI/browser acceptance, and the remaining gates are
  still outstanding.
  Confidential interactive clients were incorrectly rejected by
  `authorizeClient`, `interactivePrincipal`, and `interactiveClaims`; all three
  now accept Confidential registrations with the authorization-code grant.
  Workload-only registrations remain excluded. Unit regressions cover the
  type/grant checks and exact web redirects. The HTTP fixture also enrolls an
  independent confidential client, verifies signed access/ID tokens, rejects
  missing/wrong secrets for code and refresh, and rejects wrong PKCE even with
  the correct secret. Its ordinary client query is checked for redaction.
  The initial incremental build and all 25 unit tests passed. PostgreSQL
  restart `_30` passed, but HTTP `_30`/`_31`/`_32` completed those new protocol
  checks and hung during the final shutdown. A libtool/GDB attachment on the
  diagnostic `_32` run found the main thread looping in Grant-cache secondary
  index 2 cleanup after worker shutdown. No secret-bearing frame arguments
  were requested. The fixture timed out and killed the hung test processes;
  these failed runs are not graceful-restart acceptance.
  Consent staging, code issuance, and enrollment beginning changed indexed
  grant user/expiry fields without declaring every changed key to ZdbTable.
  They now use `findUpd<0, ZuSeq<1, 2>>`; a unit regression looks up the issued
  code through the user/app cache index. The ordinary incremental source,
  unit, and integration rebuild is running. These index corrections remain
  unverified; fresh PostgreSQL `_33` HTTP and restart databases are prepared
  for the next run.
  The source rebuild passed, and PostgreSQL HTTP `_33` then passed the entire
  native/confidential OIDC fixture, negative secret/PKCE cases, authority
  narrowing, and drained restart checks (5.7 seconds). The teardown hang did
  not recur. The dependent unit/itest rebuild remains active; the new secondary
  cache-index unit regression and refreshed saga-restart run remain pending.
  The complete incremental source/unit/itest build passed. All 25 unit tests
  passed, including lookup through the changed grant user/app index (1.6 s).
  PostgreSQL restart `_33` passed all three cases and staged saga boundaries
  (13.8 s). Targeted libtool/Valgrind `ZumTest` completed all assertions with
  no invalid reads/writes, including no earlier Grant-cache teardown reads.
  It exited 99 for two definite-leak contexts: the previously tracked 32-byte
  ZmThread_Main TLS allocation and an HTTP unit fixture that initialized the
  same response-builder union twice without destroying its prior value
  (192 direct plus 192 indirect bytes). The fixture now uses separate builder
  lifetimes; that test-only edit awaits its incremental rebuild/recheck.
  Diagnostic log: `/tmp/zum-grant-memcheck-ERAw5v/memcheck.log`.
  The test-only incremental rebuild and all 25 unit tests passed (1.6 s).
  The targeted Valgrind recheck completed all assertions with no invalid
  reads/writes and no HTTP response fixture leak. Its only remaining error is
  the previously tracked 32-byte definite leak in ZmThread_Main TLS; therefore
  this is not a clean Memcheck pass (exit 99).
  Recheck log: `/tmp/zum-grant-memcheck-ERAw5v/recheck.log`.
  No builds or test processes remain running at this checkpoint. Gate 5 and
  later milestones remain incomplete; these results establish native and
  confidential local OIDC code/refresh behavior and the corrected grant-cache
  lifecycle, not full provider conformance or completion of the plan.

- Transitional compatibility fields (`issuer.nextActionID`,
  `issuer.authVersion`, `user.roleIDs`, `user.oidcSub`, `scope.audience`, and
  legacy client authority fields) whose consumers must move to app membership,
  external identity/evidence, audience IDs, and client-access rows; their removal
  is part of replacing the old server/bootstrap paths and the offline migration.
- The PostgreSQL allocator-consistency warnings were isolated to
  `zdb_pq/ext/zbitmap.c`: the binary receiver read a count of 64-bit words but
  passed it to an allocator taking a bit count, then overwrote every word after
  the first.  The receiver now converts words to bits.  All four extension
  objects were incrementally rebuilt against the running PostgreSQL 16.3
  headers (stale objects initially carried an incompatible module magic) and
  installed into the supplied disposable PostgreSQL tree.  Fresh database
  `zum3_bitmap_fix_20260910` bootstrapped and restarted without allocator
  warnings; SQL inspection proves the 128-bit stored role bitmaps contain the
  expected 73 superuser bits and five app-service bits.
- All later gates remain unverified until their listed runtime checks pass.
- Zdb saga rows now persist an optional absolute `ZuTime` recovery deadline.
  The null sentinel means never expires; zero and other past values are valid
  one-shot deadlines. Admission and initial forward execution ignore the
  deadline. Recovery of an expired saga removes an unapplied final intent or
  compensates the already-applied prefix through the existing saga state
  machine, without running later forward business steps. The `Complete`
  callback is captured with the same decay/forward ownership pattern as
  `Submit`. Focused `ZdbSagaTest` passed all 79 tests, including seven zero-
  deadline recovery cuts. PostgreSQL `zdbsagatest` passed all 12 tests against
  fresh database `zdb_saga_expiry`, including zero-deadline cuts before and
  after every batch step intent with drained reconnects.
- The recovery-only `m_abort` flag introduced with saga deadlines was removed.
  Existing `m_fwd` direction plus the recovered step intent and its UN fully
  describe the state: an unapplied intent is removed through normal reverse
  completion before compensating the applied prefix. Rebuilt `ZdbSagaTest`
  passed 79/79 and PostgreSQL `zdbsagatest` passed 12/12 against fresh database
  `zdb_saga_no_abort_20260911`, including every zero-deadline recovery cut.
- The speculative `requestCleanup` operation, saga, secondary index, and tests
  were removed. Saga deadlines are native Zdb recovery metadata; Zum supplies
  an absolute deadline only when initially submitting a saga. There is no Zum
  saga cleanup scanner or administrative cleanup endpoint.
- Final completion verification on 2026-09-11 used only the configured clang
  debug build, incremental `make -j3`, and fresh PostgreSQL databases with the
  `uint` and `libz` extensions. The coherent source, unit, integration and
  example build passed. `make -C zum/test test` passed all 31 tests. The
  PostgreSQL restart suite passed all 14 scenarios, including every staged role
  deletion and catalog publication intent/effect boundary. The live HTTP fixture
  passed with observed success, HTTP 401 and HTTP 403 coverage for all 68/68
  administrative operations, plus the real `zumd`, `zum`, `zumpingd` and
  `zumping` bootstrap/enrollment/login/ping/refresh paths. The TLS/OIDC
  federation fixture, two-daemon activation fixture (68 standby HTTP 503
  routes), upstream transport fixture (3 scenarios), and offline migration
  fixture (12 stages) also passed. No release, GCC, ASan, MinGW, clean build,
  reconfiguration or unconditional Valgrind run was performed.
- Zum's saga submission wrapper now forwards Zdb's optional absolute `ZuTime`
  recovery deadline. Existing authoritative expiries are supplied for
  authorization-code creation, refresh-family creation, enrollment and
  credential ceremonies, and recovery issue/completion. Administrative,
  catalog, migration and rekey sagas retain the null sentinel and can recover
  without an invented TTL. Initial execution remains unaffected; only recovery
  after an existing non-null deadline selects native compensation. The rebuilt
  unit, live HTTP and federation fixtures passed with this handoff.
- The current source-only OAuth cleanup separates a protected service's exact
  `issuerURL` from the core `managementIssuerURL` used by its catalog publisher.
  Catalog-client enrollment now owns that confidential client and its
  client-access row under the core application while retaining target-app
  `admin_access`; `libZum` discovers and verifies management and protected JWKS
  independently and always publishes to the protected app derived from
  `issuerURL`. The HTTP fixture, restart expectations, ping example config, and
  documentation are aligned with this split and the `zum.catalog` scope.
  Per instruction, these latest C++ changes have not yet been rebuilt.
