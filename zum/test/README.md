# Zum unit tests

Build the default targets in `zum/src` before this directory, using the existing
configured toolchain and `make -j8`. Run `make test` here after building.

`ZumSecretTest` exercises the private server AES-256-GCM secret envelope and
offline rekey primitive: round trips, fresh nonces, wrong keys, associated-data
substitution, tampering, rekey retries and empty plaintext. Keys are generated
inside the test; it needs no database or environment secrets and prints no key
material. This is not the offline maintenance command or its persistence test.

`ZumRecordsTest`, `ZumMgmtTest`, `ZumHTTPTest`, `ZumOAuthTest`,
`ZumAuthTest`, `ZumDiscoveryTest`, `ZumJWTTest`, and `ZumWebAuthnTest`
cover server records and protocols by function. `ZumSagaIdentityTest`,
`ZumSagaOrganizationTest`, `ZumSagaAdminTest`, `ZumSagaSessionTest`, and
`ZumSagaKeysTest` cover saga descriptors, payload round trips, and repeated
phase layouts. `ZumRuntimeManagementTest` covers action, catalog, assignment
and administrative edit outcomes. `ZumRuntimeRoleTest` covers role-name
uniqueness and races. `ZumRuntimeConsentTest` covers consent and code issuance.
`ZumRuntimeRevocationTest` covers saga revocation, grant cleanup and SSF delivery.
`ZumRuntimeIdentityTest` covers bootstrap, enrollment, credential and session
setup against a fresh in-memory database. `ZumRuntimeOIDCTest` covers OIDC
federation and SSO using the identity fixture on its own database.
`ZumRuntimeTokenTest` covers authorization and token issuance;
`ZumRuntimeRecoveryTest` covers recovery and second-credential flows;
`ZumRuntimeKeysTest` covers signing key add/retire behavior. These tests use
their own databases and run the needed identity, OIDC and token setup.
`ZumClientTest` and `ZumOIDCHTTPTest`
cover service-client behavior and OIDC HTTP transport respectively. `ZumScopeTest`
checks the retained scope-selection context: token deduplication, application
boundaries, disabled/owned/tombstoned roles, grant restrictions, identity and
`offline_access` policy, candidate reset and output growth. The focused runtime tests exercise
Zdb-backed scope/authority loading. `ZumRuntimeRoleTest` checks concurrent custom
creators, custom/catalog races and rollback without leaked app ownership. SQLite restart,
full HTTP/service flows and clustered activation tests live in `../itest`.

OIDC lifecycle source cases in `ZumOIDCHTTPTest` cover init/final without
starting workers, a borrowed stopped resolver startup failure, explicit
start/stop, restart, completion-driven final and a retained fn() after final.
`ZumRuntimeOIDCTest` also exercises protocol init/final, repeated operations,
restart, and stop while a discovery response is deliberately held. That case
requires stop to wait for the response and retains the fired completion through
final. These additions are unbuilt and unrun; historical verification below
is not evidence for the lifecycle batch.

The embedded provider fixture in `ZumRuntimeOIDCTest` now explicitly starts and
stops Server. It adds never-started final, repeated operations, restart without
init and completion-driven final. `ZumRuntimeTokenTest::signingCredit` holds a signing completion, cancels request
admission, and checks that the helper retains its caller credit after the
application cancellation result. It then releases signing while retaining and
repeating the fired completion, requiring one application result and complete
credit retirement. The credit has an erased polymorphic reference, so this also
checks concrete destructor dispatch. These source additions remain unbuilt and
unrun; they are not runtime evidence.

`ZumClientTest::serviceKeys` covers retained service JWKS lookup at one, two,
three, seven, eight, 33 and 64 keys, repeated local verification, exact ID matching,
hash collisions, duplicate rejection and skipped invalid/unsupported records.
It checks failed-refresh preservation, growth/shrinkage, replacement of key
material under an existing ID, unknown-ID refresh/introspection, and rejection
of known-ID invalid signatures without introspection. Distinct issuer keys
exercise management, resource and SSF verification, including the management
snapshot used for SSF after management renewal. Its transport is in-process;
no external service or database is required.

Service-key lookup verification (2026-10-13, Linux x64, clang 23.1.1):

- Built the default `zum/src` and `zum/test` targets with `make -j2` in the
  current debug configuration. `ZumClientTest`, `ZumOIDCHTTPTest` and
  `ZumSecretTest` pass. The full `make test` gate fails in `ZumTest::actions`
  (`interactivePrincipal`/`clientPrincipal`) and
  `ZumScopeTest::offlineAccess`. Both failures reproduce with the saved
  pre-repair service implementation rebuilt against the current dependencies.
- The final `ZumClientTest` passes Valgrind 3.25.1 with full leak checking:
  zero errors, zero definitely/indirectly/possibly lost bytes, no suppressions.
  It also passes clang ASan with `detect_leaks=1:halt_on_error=1`, without
  diagnostics. Sanitizer dependencies and the installed client library/test
  were built separately under `/tmp/repair3-asan` using `z.config -D -G -P`;
  the workspace remains a debug build. This sanitizer check covers the client,
  not the daemon test suite.
- On this build the owning table object is 32 bytes and each bucket is
  64 bytes. Default hash parameters reserve at least eight buckets: 544 bytes
  per small installed table, excluding payload buffers/allocator overhead.
  With default parameters each load acquires one pooled table object and one
  bucket allocation sized from the bounded wire count, then moves accepted
  key payloads into it. Configured smaller tables can grow through normal
  framework resizing.
  The management-to-SSF startup snapshot copies its payloads once. Lookup
  uses spans and adds no allocation. Old/candidate tables coexist only during
  replacement; failure destroys the candidate and finalization releases the
  installed tables.

Complete verification measurements below are microseconds, **before / after**.
The saved baseline retains the original size-dependent paths. Both versions
use identical current dependencies and the debug toolchain; dynamic-loader
binding confirms the baseline service is selected. Runs are pinned to CPUs
14 and 15 and alternate candidate/baseline three times. Each hit/miss row is
the median of three run medians, each with 100 warmups and 2,000 samples.
Hits include submission, scheduler completion, JWT parsing and ES256
verification. Misses use cached JWKS with introspection disabled. Refresh
uses 100 forced unknown-ID requests per run and includes fake HTTP callbacks,
JWKS parsing/validation, construction, publication and destruction.

| Keys | Local verification hit | Cached miss | JWKS refresh |
| --- | --- | --- | --- |
| 1 | 116.21 / 115.31 | 23.62 / 23.35 | 85.30 / 85.87 |
| 2 | 116.11 / 115.18 | 23.84 / 23.42 | 102.07 / 101.87 |
| 3 | 116.35 / 115.13 | 23.58 / 23.56 | 117.61 / 118.28 |
| 7 | 116.32 / 115.25 | 23.64 / 23.91 | 184.74 / 186.35 |
| 8 | 117.07 / 115.32 | 24.13 / 23.78 | 202.73 / 201.87 |
| 33 | 117.06 / 115.77 | 23.91 / 23.55 | 605.48 / 604.00 |

All measurements returned the expected authorization result. Differences are
small (roughly 1–2% or less); these debug measurements show no material
whole-operation regression and do not establish a release-build speedup.
The temporary reproduction harness is `/tmp/repair3bench.cc`, with build rules
in `/tmp/repair3-bench.mk`; raw runs are `/tmp/repair3-perf-{old,new}-{1,2,3}.log`.
Final client memory-check results are `/tmp/repair3-client-final-valgrind.*`
and `/tmp/repair3-client-asan.*`; module and baseline gate reports are
`/tmp/repair3-module-final.log` and `/tmp/repair3-module-baseline.log`.


Lifecycle repair source coverage (2026-10-18; not yet built or run):

- ZumMgmtTest::requests checks duplicate deactivation from cancellation,
  pending activation during drainage, retirement-slot bounds, and late
  completion after the request controller is released.
- ZumRuntimeRevocationTest::transmitterLifecycle uses a real in-memory DB
  receiver/outbox row and holds the remote HTTP reply. Both stop callbacks
  must wait for that reply and terminal DB cleanup; retaining the fired
  completion object must not retain its work credit. The fixture also checks
  init/final without start and restart without init.
- ZumClientTest covers the service engine cases described in the module
  README, including retained authentication/callback secrets on restart and
  final from a completed stop callback.

These cases join the large repair.md/repair2.md implementation batch. Defer
rebuilds and runs to that plan's implementation milestone, then incrementally
refresh libraries and dependents with the existing configuration and artifacts.
The historical validation above does not validate these source changes.


`ZumDaemonTest` covers no-init final before borrowed mx startup, stopped daemon
init/final with an independently running borrowed mx and no DB runtime, repeated
final, configuration after final, and partial initialization
where the provider initialized but HTTP rejected port zero. It links the real
daemon, admin and transmitter objects and defines the same response/parser
catalogs as the application. No listener is started or bound in these isolated
cases. Execution results belong to the repair acceptance logs. Listener startup, rollback, restart, pending opposite operations and
outstanding administrative/maintenance work still need lifecycle acceptance
coverage at the final milestone.

Requests lifecycle cases in `ZumMgmtTest` wait for owner-thread activation,
cover pending activation from cancellation, and deterministically supersede a
queued stop before startup's owner continuation. The latter reports false
without claiming an actual stop occurred; a final explicit deactivation drains
the controller. Runtime DB fixtures signal activation readiness after the
admission-owner callback.
