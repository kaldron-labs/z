# Zum integration tests

`zumrestarttest` uses PostgreSQL for both the basic restart and saga recovery
scenarios. There is no in-memory fallback. Supply both test-specific variables
and use a fresh disposable database with the `uint` and `libz` extensions
already installed. From `zum/itest`:

```sh
export ZUM_TEST_MODULE=$PWD/../../zdb_pq/src/.libs/libZdbPQ.so
export ZUM_TEST_CONNECT='host=/tmp dbname=YOUR_DISPOSABLE_DATABASE'
export ZUM_HTTP_CONNECT='host=/tmp dbname=YOUR_OTHER_DISPOSABLE_DATABASE'
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
The `grantUpdate` case separately verifies persistence of changed role IDs,
scope, authority source, and provider ID across a drained PostgreSQL reopen;
it is a metadata test, not upstream OIDC interoperability coverage.
Shutdown first closes request admission and waits for its drain. It then waits
for `Zdb::stop()`, which closes tables, drains the PostgreSQL write queue and
in-flight pipeline, and drains shard callbacks before returning. Only then does
the fixture finalize the old DB and open another instance.

The fixture writes rows and requires an empty database;
do not point it at an existing user store. It does not create or drop databases.
The ordinary `ZDB_MODULE` and `ZDB_CONNECT` variables do not select its database.
This is Zdb stop/start recovery, not a process-kill or replicated failover test.

`zumhttptest` invokes `zumhttp.py`, requiring Python 3 and its `cryptography`
package, plus the already-built `../src/zumd`. It uses a separate fresh
PostgreSQL database selected by `ZUM_HTTP_CONNECT`, with the same extensions
and store module. For a focused run, use `prove -v -j1 zumhttptest` here.
It launches the source-tree wrapper, waits for the listener startup event,
and exercises bootstrap registration and OAuth authorization-code login with
PKCE using independently generated and signed WebAuthn messages, rejects code
reuse, and rotates refresh tokens. It then creates an application and action
through HTTP, checks enrollment retries and changed-input conflicts, gracefully
stops the server, and verifies the records, credential, and non-secret retry
result after restarting on the same database.
It also checks filtered GET queries, rejects unknown filters, requires an ETag
for updates (428), rejects stale ETags (412), and verifies the accepted label
update after restart.
An enrolled service also obtains a client-credentials token; its limited core
role must not permit listing all applications (403), while no bearer token
returns 401.
Application and client-access suspension/reactivation are exercised through
HTTP: both row and authorization versions advance once per actual transition,
unchanged-state retries preserve the ETag, stale ETags fail, and suspended
authority prevents fresh workload issuance. Reactivation restores issuance;
the version changes and usable credentials are checked again after restart.
The fixture also enrolls a separate public native OIDC client using REST only,
creates an app-local ping role/scope, assigns the local user to that app, and
approves the client through `client_access`. It requests `openid ping` with
PKCE and nonce, completes consent, exchanges the code and refreshes, and
independently verifies ES256 access/ID-token signatures against JWKS plus their
issuer, audience, client/app, scope/action, and nonce bindings.
A separate confidential web client uses an exact HTTPS
redirect, PKCE, and `client_secret_basic` for code exchange and refresh. Missing
and incorrect secrets fail without consuming the code or refresh token; an
incorrect verifier fails even with the correct secret. Its access and ID tokens
are independently verified, and ordinary client queries must redact credentials.
Role removal
narrows refreshed access; restoring the role does not expand that narrowed
family. Membership suspension rejects refresh before and after another drained
PostgreSQL-backed server restart. This is not third-party OIDC conformance or
upstream-provider interoperability coverage.
This is a virtual-authenticator protocol fixture, not a real-browser,
TLS-conformance, process-kill, or replicated-failover test.

The HTTP fixture generates its encryption key in memory and supplies it only
through the server environment. Bootstrap material and server diagnostics live
in a private temporary directory; no secrets are printed. These temporary
credentials are discarded on exit, so use a new disposable database for each
invocation. Neither fixture creates or drops its database.
