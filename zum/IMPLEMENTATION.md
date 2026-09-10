# Zum multi-application IAM implementation ledger

This ledger tracks implementation of [`../zum3.md`](../zum3.md).  A milestone
is complete only when its gate has the required observable evidence.

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

## Milestones

| Gate | Status | Evidence / remaining work |
| --- | --- | --- |
| 1: expanded schema | complete | All 23 records register in PostgreSQL. New-row roundtrips, required app-qualified keys, cross-app rejection, per-app allocation, and app authority tests pass. Transitional old-flow fields remain migration inputs and may not be used by new server operations. |
| 2: `zumd` skeleton | complete | `Makefile.am` declares and links the installed target with zrest/Zdb through a private convenience library. The installed `libZum` contains only the service client and resource-token verification and has no Zdb/server/schema linkage. `--once` activated against PostgreSQL and shut down cleanly; `Requests` admission follows Zdb up/down callbacks. |
| 3-4: bootstrap | complete | Fresh PostgreSQL reached `AdminPending`; restart preserved IDs/counts, a persisted `Core` boundary resumed, and a wrong database key failed promptly. Browser enrollment/`Ready` remains intentionally at gates 5-6. |
| 5: REST/OIDC server | pending | All 70 management operations dispatch and wrong-method routing is conventional. OIDC discovery, ES256 ID tokens, exact identity-scope release, nonce preservation, DB-driven local-first routing, upstream ID-token/UserInfo claims, provider-qualified projections, mapping re-evaluation, assignment freshness, opaque keyset cursors, the native-service facade, browser sessions, explicit consent, local logout, and identifier-first login routing are implemented. Application enrollment has forward and rollback restart-recovery evidence. Every authorized mutating management call now has request-level audit persistence and correlation; remaining multi-row management sagas and end-to-end audit HTTP evidence remain. |
| 6: admin CLI | pending | REST/PKCE client links and resolves the complete catalog, but a real bootstrap passkey/browser login and authenticated mutation sequence has not been run. |
| 7-8: examples | pending | Zum's own examples are in scope; implementation follows gates 5-6. |
| 9-10: local ping | pending | In scope; acceptance follows the example implementation. |
| provider completion | pending | Independent client, federation, freshness, logout, migration, replication. |

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
