# Zum integration tests

`zumdztctest.py` runs disposable SQLite backed `zumd` processes with isolated
publisher IDs, rings, PID registries and alert paths. It checks the default
off state, node/environment/CLI enable inputs, App/Mx/DB ring replies before
administrator enrollment, late collector attachment, subscription shutdown,
`--once` and `--rekey` cleanup, malformed node boolean rejection, and cleanup
after publisher and IAM startup failures. `zumdztcgctest.py`
kills an attached ring reader without detaching it and verifies that ZiRing's
writer-open PID liveness check reclaims its unread message.
The `ztc/itest/ztchubtest.py` fixture also starts `zumd` publishing before
an agent exists, then attaches a real `ztcagent` and confirms its App telemetry
reaches a WSS front end through the hub.

`zumdztccluster.py` starts a persistent standby publisher, promotes it after
its leader exits, joins a deliberately ahead peer to deactivate it, and
promotes it again after that peer exits. It checks the same publisher PID
registration and App/DB request path throughout. The ahead peer is made from a
disposable SQLite snapshot followed by a real isolated IAM write; Zdb ranks
database state before active status and host priority.

All three are run by `make test`; a focused run after building
`zumdztcprobe` is:

```sh
export ZDB_MODULE=$PWD/../../zdb_sqlite/src/.libs/libZdbSL.so
prove ./zumdztctest.py ./zumdztccluster.py ./zumdztcgctest.py
```

`zumvaulttest` exercises OAuth-token and service-secret persistence through
`Ztls::Vault`, including client identity separation. It uses a disposable
Vault home and needs no external service.

The real `zumd` fixtures select `--vault-store=file --vault-test-store` only
with disposable `ZUMD_HOME` directories; deployment must select a secure
KeyRing or Module store explicitly. The HTTP fixture provisions `global/dbKey`
from `ZUM_DB_KEY`, then restarts without that variable to exercise Vault loading.
An explicitly supplied wrong key fails rather than falling back to the stored
key. The focused `zumdrekey.py` fixture loads the old key from Vault and supplies
the new key through `ZUM_DB_KEY`; failed rotations retain the old key, and a
successful rotation persists the new key. It models interruption between
durable database commit and Vault publication by restoring the old disposable
Vault image, then checks same-key retry without ciphertext changes.
`zumdvault.py` separately checks
node-file Vault selection, `--once`, first provision, bootstrap reissue over an
existing output file with owner-only permissions and truncation, reissue after
administrator enrollment without changing the identity or assignments, old-link
and interrupted-ceremony rejection, login with both old and added passkeys,
capability expiry, symlink rejection,
file-operation and startup errors routed to the configured log, restart without
environment values and administrator login across a persisted restart,
startup without bootstrap CLI options before and after enrollment, rejection of
missing initial bootstrap options without seeding the issuer, and reissue using
the persisted administrator without invalidating the grant on missing output,
explicit repair of dropped administrator string vectors followed by OIDC login,
explicit wrong or malformed key, missing stored key,
native KeyRing failure with a deliberately unsupported D-Bus address. It also blocks
Vault publication after bootstrap and checks that the daemon never reports
successful activation. Source ordering keeps request admission behind publication.
The SSF delivery fixture uses client REST registration and timer renewal. It
checks callback credential encryption, lease expiry, unreachable receiver
removal, and durable notification delivery across daemon restart.
These fixtures never use a real home or native credential store.

`zumrestarttest` uses SQLite for both the basic restart and saga recovery
scenarios. There is no in-memory fallback. `make test` creates fresh disposable
SQLite files and supplies all test-specific variables. For a focused manual run
from `zum/itest`:

```sh
export ZDB_MODULE=$PWD/../../zdb_sqlite/src/.libs/libZdbSL.so
export ZDB_CONNECT=/tmp/zum-restart.db
export ZUM_CLUSTER_PEER_CONNECT=/tmp/zum-cluster-b.db
make -j3 && make -j3 test
```

The fixture loads the configured store module, reconstructs the Zdb instance
between staging and recovery, and runs the enrollment completion and
compensation checks. Action creation additionally reconstructs the database at
each of its four saga steps, both with an intent awaiting its effect and after
the effect is committed, plus the initial payload-only image. It checks stable
IDs, exactly-once allocation/version changes, released ownership, empty saga
journals, and duplicate-name compensation after a committed app reservation.
These are explicitly staged durable images, not process-kill injection.
The new `invitationRecovery` case stages all17 intent/effect boundaries of
UserInvite.v2 with an existing external identity matching the invited local
name. It checks exactly one external authority/version increment, released
owners, pending local invitation/capability and completed request. A stale
external snapshot after the local user and capability effects forces
compensation, leaving the external record unchanged and removing the invitation.
See the ledger for execution status; written cases are not evidence of a pass.
This invitation case and the complete 14-scenario SQLite restart suite pass
against the current schema; detailed commands/results are in the ledger.
The `memberRecovery` case stages all nine intent/effect boundaries of the
four-step assignment change saga. Recovery must publish the assignment's role
and state replacement together with exactly one assignment/app version advance,
release both owners, and empty the journals. A stale assignment snapshot after
the application reservation must compensate without changing either record.
The `grantUpdate` case separately verifies persistence of changed role IDs,
scope, authority source, and provider ID across a drained SQLite reopen;
it is a metadata test, not external OIDC-provider interoperability coverage.
`roleRemoval` stages the typed role-deletion payload and every intent/effect cut
across its 24 expanded steps, reopening SQLite for all 49 durable images.
The repeated reference branches contain two rows in each of assignment, client
access, admin access and role mapping. Separate cases cover empty
reference sets and a stale second mapping that forces compensation after the
first mapping has been deleted. Checks include retained unrelated roles and
settings, restored before-images on rollback, logical versions, released owners
and empty journals. The complete boundary matrix passes in the current
SQLite restart suite.
Catalog recovery first covers payload-only completion and compensation. The
expanded fixture additionally stages all 29 intent/effect cuts for its 14 phases
with one inserted and one replaced row per action and role definition, plus a
stale role after earlier effects have committed. It checks app publication,
original IDs, role removals, logical versions, owners and empty journals. The complete
catalog boundary matrix passes in the current SQLite restart suite.
Shutdown first closes request admission and waits for its drain. It then waits
for `Zdb::stop()`, which closes tables, drains the SQLite store queue and
in-flight work, and drains shard callbacks before returning. Only then does
the fixture finalize the old DB and open another instance.

The fixture writes rows and requires an empty database;
do not point it at an existing user store. It does not create or drop databases.
This is Zdb stop/start recovery, not a process-kill or replicated failover test.

`zumclustertest` starts two real daemons with separate fresh SQLite stores,
one shared encryption key and issuer, and native Zdb host/priority configuration.
Supply `ZDB_MODULE`, `ZDB_CONNECT` and `ZUM_CLUSTER_PEER_CONNECT`.
Both nodes must expose liveness; neither is ready before admin enrollment.
The standby must reject all 60 active administrative routes with 503. The source-tree
fixture extracts method/path declarations from ZumMgmt.cc and verifies its
catalog count, avoiding a second manually maintained endpoint list. Requests
have no credentials and use minimal bodies: this tests inactive admission,
not authenticated authorization or mutation validation on a standby.
Both must shut down on
SIGTERM. The expanded scenario enrolls the initial administrator through the
production WebAuthn flow, then stops the leader normally and awaits Zdb
activation of the survivor. The promoted daemon must retain bootstrap IDs,
become ready, accept a fresh passkey login, verify its token from replicated
signing state, and serve the replicated administrator and credential records
to the bearer token issued before promotion. It requires `sqlite3` on `PATH`
for read-only bootstrap snapshots. This proves two-node active-service
continuity, not a replication durability or failover SLA claim.

`zumoidchttptest` exercises the real OIDC HTTP transport against reserved loopback
ports, without persistent storage, certificates, or an external IdP. Non-listening sockets
refuse connections while retaining their ports; alternating origins exercises
idle-slot eviction. A listening socket holds a TLS handshake pending while a
second origin tests saturation, followed by cancellation/drain on shutdown.
Immediate shutdown after submission checks startup callback ownership. Completion
uses framework continuations and main-thread waits, not timing-based sleeps.
These tests do not establish TLS certificate validation or OIDC interoperability.

`zumfederationtest` invokes `zumfederation.py` and requires a separate fresh
SQLite file in `ZDB_CONNECT`, plus `ZDB_MODULE` and the
same Python dependencies as `zumhttptest`. Run `prove -v -j1 zumfederationtest`.
It uses `zumidp.py`, an independent local TLS/OIDC fixture with an ephemeral CA
and ES256 signing key. The daemon's non-secret `oidc.caPath` configuration
selects that CA; certificate verification is not bypassed and system trust is
not modified. Test credentials/keys stay in its private temporary directory.
The same fixture CA authenticates a test-only TLS reverse proxy in front of
`zumd`; its canonical issuer and OIDC callback are HTTPS. The Python browser
driver verifies that certificate too. Production HTTPS requirements are unchanged.
It checks discovery, Basic-authenticated code exchange with PKCE, signed ID-token
claims, N:1 role mapping, automatic external identity projection, downstream token issuance
and refresh, and local administrator login during an external provider outage.
This is not Okta interoperability or full external-provider
freshness/revocation acceptance.
Both supported eligibility modes are exercised: `mappedRole`, plus `claimValues`
with accepted scalar/array strings, unknown-value denial, and wrong-type denial.
Provider isolation is exercised with a second enrolled provider record using the
same independent provider issuer and subject: the second application cannot use
the first provider's mappings, then succeeds only after its own mappings exist;
its projected identity and evidence remain provider/application-qualified.
The expanded fixture also enrolls the ping service through the real admin CLI,
checks real `zumping` login/pong/refresh before and after service restart, and
checks denial after fresh provider claims contain only unmapped roles. The local
HTTP fixture additionally requires `zumping` to fail without a pong while either
`zumd` or `zumpingd` is stopped, then proves recovery after `zumd` restarts. These
fixtures also require `zumpingd` startup to reject a missing or invalid
`ZUM_CLIENT_SECRET` without logging either the issued or presented secret. These
additional checks require the rebuilt CLI/service binaries with private-CA config.
It also switches the provider to UserInfo claims, supplies roles different from
the ID token, and verifies that only the selected source populates evidence.
A mismatched UserInfo subject must fail without modifying users or evidence.
Signed ID tokens with a mismatched nonce, audience or issuer must fail before
UserInfo is fetched and without changing assignment evidence.
The evidence-expiry case configures a five-second assignment lifetime, verifies
later browser-session deadlines and capped access-token expiry, waits until the
actual evidence deadline, and checks refresh denial without provider requests
or evidence extension. A fresh provider login must restore resource access.
This deadline wait tests protocol time; it is not synchronization for concurrent
work. The fixture does not edit persisted records behind Zdb.
Mapping removal and mapped-role disablement are tested against still-fresh
evidence. Refreshed signed tokens lose their actions without provider requests;
restoring configuration cannot expand the narrowed refresh family. A new
authorization flow is required to regain the action.
See `../IMPLEMENTATION.md` for the execution status of newly added checks.
The optional manual transport probe in `ZumOIDCHTTPTest` also accepts
`ZUM_OIDC_TEST_CA` alongside `ZUM_OIDC_TEST_URL` for a private CA.

`zumhttptest` invokes `zumhttp.py`, requiring Python 3 and its `cryptography`
package, `sqlite3` on `PATH`, plus the already-built `../src/zumd`, `../src/zum` and
`../example/zumpingd` and `../example/zumping`. It uses a separate fresh
SQLite file selected by `ZDB_CONNECT` and the configured store module. For
a focused run, use `prove -v -j1 zumhttptest` here.
After draining/stopping the daemon, the fixture changes only the disposable
store's issuer version marker, checks explicit unsupported-schema refusal with
no active admission or added users/apps, and restores the marker. A passive
health listener may start before validation. This tests rejection without
modifying the unsupported store.
It launches the source-tree wrapper, waits for the listener startup event,
using standalone defaults on its first start and `--config zumd.cf` on subsequent
starts. That fixture keeps the store unchanged but changes numeric thread order,
checking that request dispatch resolves the named `shard` thread. This exercises
node configuration loading, not multi-node replication or failover.
It exercises bootstrap registration and OAuth authorization-code login with
PKCE using independently generated and signed WebAuthn messages, rejects code
reuse, and rotates refresh tokens. It then creates an application and action
through HTTP, checks enrollment retries and changed-input conflicts, gracefully
stops the server, and verifies the records, credential, and non-secret retry
result after restarting on the same database.
The session lifecycle scenario reuses a confidential-client login silently
(`prompt=none`) for a native business client and the separate Zum admin app.
It checks that `prompt=login` displays authentication, invalid logout CSRF does
not end the authenticated session, and valid provider logout rejects reuse of
the old cookie while leaving independent application refresh grants usable.
The forced-login check abandons its new transaction cookie and restores the
original session handle for the separate logout checks; it does not claim a
completed reauthentication or account-switching test.
The same scenario accepts a sufficiently large `max_age`, crosses one real
protocol-second boundary, and requires `login_required` for `max_age=0` while
the original session remains usable without that freshness restriction.
After refresh rotation it replays the consumed token and requires rejection of
the newest token too, proving family revocation for both business and admin
clients rather than merely stale-token rejection.
The fixture also launches `zum login --no-browser`, completes its authorization
using the virtual authenticator, delivers the state-bound loopback callback, and
checks owner-only credential-file permissions. Administrative coverage now
requires a successful response,401 and403 for every catalog operation. Service
delegation suspension/restoration supplies operationQuery's forbidden case.
Reported400/412/503 counts only identify observed statuses, not complete
invalid-input, cross-app or inactive-node verification for each route.
Specific cross-app checks submit the core admin role ID to a business
app's assignment, client-access and admin-delegation endpoints. Rejection
must preserve target records, app authority versions and the core role catalog.
The source role ID is verified absent in the target app; identical-ID or
identical-name catalog isolation is also exercised by the independent orders
application and `zumpingd`, which both define a `ping` action and role whose
scope is derived at runtime;
each other's valid resource tokens remain rejected by the service.
The real ping-service checks additionally present a valid core-admin token and
another application's valid token carrying an action named ping. Both must
return401 before and after service restart. The fixture verifies signatures
and expiry first, so expired or malformed tokens cannot satisfy that check.
GET and POST UserInfo accept the openid access token and return exactly the
matching ID-token subject when no profile/email scope was granted. Missing or
malformed credentials, ID-token substitution and an admin access token without
openid all fail with401 invalid_token.
Suspending the client through REST also makes both UserInfo methods reject its
otherwise valid token. Restoring that client permits the same unexpired token
again, without changing the subject or extending its lifetime.
A second `zum app list` process
uses that file to query the core application. This exercises the real CLI's
PKCE/token/callback and REST wiring, not an actual browser or platform authenticator.
Application enrollment, local user invitation, assignment creation and role
assignment use the real CLI as well. Secret-bearing operations must print only
the delivery-file receipt and write their response into an owner-only file;
subsequent HTTP queries and retries verify the resulting records and request IDs.
Definition lifecycle cases create a role, client and provider, then exercise
role/provider updates and each definition's state endpoint.
They check missing/stale ETags, exactly one version increment on accepted changes,
and unchanged records after rejected stale updates.
User/credential cases cover profile and label updates, credential state,
recovery preconditions, one-time recovery response redaction on retry, and
credential queries by exact ID or indexed user group. Bounded revocation cases
check sessions, consents and grants for the disposable user: one record at a
time, unrelated users unchanged, remaining records revoked and then zero work.
Exact grant revocation uses the ID returned by the query, checks unpadded
base64url encoding and verifies that a second request reports zero new effects.
Grant-cleanup cases reject missing request keys, invalid limits and caller-chosen
cutoffs, then verify the deletion bound, expiry of any removed records and
unchanged surviving records. A zero-removal run does not establish expired-row
deletion coverage. Auth-policy replacement and role-map deletion check ETag
preconditions and persisted results. Signing-key retirement checks the future
verification window and continued verification of an existing token.
The offline DB-key rotation gate stops the server, corrupts one late signing
envelope in the disposable SQLite store, and requires rotation to fail with
the pending marker retained. It checks old-key startup is refused while pending,
a different target key cannot resume or alter ciphertext, and the original
target can resume after the fixture repairs the corrupted envelope. Completed
retry must leave ciphertext byte-identical; old-key startup then fails and
new-key startup/login succeeds. This is a partial-error/resume test, not a
process-kill test. Native `zumrestarttest` additionally stages each ciphertext
field and key-binding update before step-UN persistence, after it, and after
the mutation, then drains/reopens SQLite and checks recovery. The native
recovery gate and real archive restore passed on 2026-09-11; see
IMPLEMENTATION.md for exact evidence and remaining limits.

Backward compatibility and data migration are non-goals. The HTTP fixture
verifies that `zumd` rejects a non-empty store whose schema version does not
exactly match, without changing its version marker or authority-row counts.

The ping-service scenario enrolls another application through `zum`, starts
`zumpingd` with its client secret in the environment (no DB credentials), checks
unauthenticated `/ping` returns401, and queries its single ping action and role,
with the corresponding scope derived from that role.
A graceful service restart must leave those catalog records unchanged. This
scenario now also invites `user`, enrolls its virtual passkey, assigns only the
ping role (no core assignment), registers a public native client, and launches
`zumping`. The fixture drives its browser redirect and loopback callback; the
client must redeem directly through `zumd`, receive pong from `zumpingd`, rotate its refresh token,
and receive pong again. Repeat login after service restart. This extended user
flow also removes the ping role through `zum` between code issuance and redemption,
requires the real client to fail without pong, then restores the role and requires
a fresh login to succeed. This revocation-race addition passed on 2026-09-11.
The preceding positive user
flow passed against fresh SQLite on 2026-09-11, including direct issuer
authorization. Browser interactions use the virtual authenticator;
this is not evidence of a real browser/platform-authenticator run.
It also checks filtered GET queries, rejects unknown filters, requires an ETag
for updates (428), rejects stale ETags (412), and verifies the accepted label
update after restart.
An enrolled service also obtains a client-credentials token; its limited core
role must not permit listing all applications (403), while no bearer token
returns 401.
Catalog publication checks reject a human administrator and a service targeting
another app, require the initial revision ETag, reject stale ETags, and publish
a standard action/role using the owning service. Identical revision/digest
replay preserves the definitions and app versions without requiring an ETag;
conflicting digests and missing ETags on newer revisions do not change records.
Each request supplies the required Idempotency-Key, with separate keys to test
the catalog's own revision/digest replay rather than only request-cache replay.
The role-deletion regression supplies all five reference kinds, checks missing
and stale ETags, removes the role, and verifies cleanup across a drained restart.
The expanded catalog regression checks same-revision tombstone repair under the
original ID without recreating deleted references, newer-revision role
preservation, content-digest validation, omission retirement, and preservation
of disabled/custom definitions. It also exercises normalized empty labels and
Unicode/control-character digests without a separate scope catalog. These cases
passed after correcting the Unicode formatter; resource scope names are derived
from the published role names.
The fixture enumerates all 60 live management operations, verifies their seeded
actions, rejects missing/invalid bearer tokens, checks denial of operations
outside the workload token's authority, and checks unsupported methods and
their `Allow` headers. This does not replace authorized success and input
validation coverage for every operation.
After a complete run, the fixture reports observed successful administrative
operations and names those without a successful-call scenario. It retains only
method/path/status for this report, never query strings or response bodies.
These counts guide missing tests; they do not establish each operation's input,
cross-app authorization, persistence or redaction invariants.
Expanded collection-query checks cover pagination, field redaction and empty
external projections in standalone mode. Malformed/trailing/null-sentinel
numeric filters and zero-based action-state lifecycle coverage pass in the
current HTTP fixture.
An invited user cannot bypass credential enrollment by transitioning through
Suspended or Disabled to Active. Missing/stale ETags and rejected transitions
are checked, including unchanged records on denial. After graceful shutdown
drains ZiLog, the fixture checks log events for actor, outcome and correlation
IDs and verifies that secret material is absent. The removed audit endpoints
must return 404; there is no audit table or exactly-once log replay contract.
Client-access and role-mapping collections are checked across paginated and
full queries, including multiple clients/providers, repeated role values in
different apps, malformed/MAC-altered cursors, cross-operation/app cursor
rejection, and unchanged records after restart. Collection indexes group by
application; mapping values from different providers must not be skipped.
Application and client-access suspension/reactivation are exercised through
HTTP: both row and authorization versions advance once per actual transition,
unchanged-state retries preserve the ETag, stale ETags fail, and suspended
authority prevents fresh workload issuance. Reactivation restores issuance;
the version changes and usable credentials are checked again after restart.
The fixture also enrolls a separate public native OIDC client using REST only,
creates an app-local ping role with its derived scope, assigns the local user to that app, and
approves the client through `client_access`. It requests `openid ping` with
PKCE and nonce, completes consent, exchanges the code and refreshes, and
independently verifies ES256 access/ID-token signatures against JWKS plus their
issuer, audience, client/app, role-derived scope/action, and nonce bindings.
A separate confidential web client uses an exact HTTPS
redirect, PKCE, and `client_secret_basic` for code exchange and refresh. Missing
and incorrect secrets fail without consuming the code or refresh token; an
incorrect verifier fails even with the correct secret. Its access and ID tokens
are independently verified, and ordinary client queries must redact credentials.
Role removal
narrows refreshed access; restoring the role does not expand that narrowed
family. Role and state replacements check both assignment and application
versions, missing/stale ETags, and identical PUT retries with unchanged records.
Zero, unknown and duplicate role references are rejected without changing either
the assignment or application.
Assignment suspension rejects refresh before and after another drained
SQLite-backed server restart. This is not third-party OIDC conformance or
external OIDC-provider interoperability coverage.
This is a virtual-authenticator protocol fixture, not a real-browser,
TLS-conformance, process-kill, or replicated-failover test.

The HTTP fixture generates its encryption key in memory and supplies it only
through the server environment. Bootstrap material and server diagnostics live
in a private temporary directory; no secrets are printed. These temporary
credentials are discarded on successful exit. On failure, the fixture retains
its private temporary directory and prints only its path, preserving diagnostics
without printing secrets. The encryption key remains process-local and is not
retained, so use a new disposable database for each invocation. Neither fixture
creates or drops its database.
