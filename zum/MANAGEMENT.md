# Management operations

[ZumMgmt.hh](src/ZumMgmt.hh) is the shared wire catalog for Zum
administration. Its 68 active `MgmtOp` values each identify exactly one remote
call and one core permission named `Zum.<operation>`; two retired slots retain
their numeric IDs without permissions or routes. The route registry in
[ZumMgmt.cc](src/ZumMgmt.cc) is the sole method/path mapping used by `zumd`,
the `zum` client, and registry tests.

The catalog is intentionally operation-oriented rather than the Cartesian
product of CRUDQ and tables. Query operations are collection GETs with exact
indexed filters or bounded pagination. Lifecycle, assignment, cleanup,
publication, and secret rotation have distinct operations because they have
different authorization and consistency rules. Zdb saga infrastructure has no
public management operation.

## Authorization ownership

Bootstrap seeds the catalog in the core Zum application. The core
`superuser` role contains every management action. The distinct
`catalogPublisher` role contains only operation-status lookup and catalog
publication. Enrolled catalog publishers never receive `superuser` implicitly.

A bearer token must carry the exact operation action. Current database state
must additionally prove either the active core superuser membership or an
active `admin_access` delegation for the authenticated user/client and target
application. The service which owns an application may publish that
application's available action/standard-role/scope catalog; catalog publication
never assigns roles to users or restores deleted privileges.

## Areas

| Area | Operations |
| --- | --- |
| Issuer/catalog | `issuerQuery`, `operationQuery`, `catalogPublish` |
| Applications | `appQuery`, `appEnroll`, `appUpdate`, `appState` |
| Users/credentials | `userQuery`, `userInvite`, `userUpdate`, `userState`, `userRecover`; `credentialQuery`, `credentialUpdate`, `credentialState` |
| Memberships | `membershipQuery`, `membershipAdd`, `membershipRoles`, `membershipState` |
| Actions/roles/scopes | `actionQuery`, `actionAdd`, `actionState`; `roleQuery`, `roleAdd`, `roleUpdate`, `roleActions`, `roleState`, `roleDelete`; `scopeQuery`, `scopeAdd`, `scopeRoles`, `scopeState` |
| Audiences/clients | `audienceQuery`, `audienceAdd`, `audienceUpdate`, `audienceState`; `clientQuery`, `clientAdd`, `clientUpdate`, `clientState`, `clientSecretRotate` |
| Delegation | `clientAccessQuery`, `clientAccessSet`, `clientAccessState`; `adminAccessQuery`, `adminAccessSet`, `adminAccessState` |
| Federation | `providerQuery`, `providerAdd`, `providerUpdate`, `providerState`; `authPolicyQuery`, `authPolicySet`; `roleMapQuery`, `roleMapSet`, `roleMapDelete`; `identityQuery`, `evidenceQuery` |
| Runtime state | `sessionQuery`, `sessionRevoke`; `consentQuery`, `consentRevoke`; `grantQuery`, `grantRevoke`, `grantCleanup` |
| Keys | `signKeyQuery`, `signKeyAdd`, `signKeyRetire` |

The precise method/path table, typed mutation fields, scope semantics, catalog
ownership, federation freshness, and lifecycle contracts are normative in
[zum3.md](../zum3.md). Append operations; never renumber a catalog already
seeded in a database.

## HTTP rules

External identities remain bound to provider/issuer/subject. Their projected
names are synthetic `oidc:<provider>:<userID>` identifiers; a browser login hint
does not rename or link that identity. Inviting a local user with the same
stored projected name invalidates the external user's existing session/grant
authority versions within the invitation saga. A local account's presence also
blocks new external issuance for that name, including when the local account is
pending or suspended. No identity, membership or role assignment is transferred.
Issued access tokens retain their documented expiry boundary. The current
UserInvite definition and physical schema are covered by the SQLite staged
recovery suite. Prior schemas and saga definitions are unsupported; backward
compatibility and data migration are non-goals.

Listener availability is separate from database authority. Passive nodes expose
`/health/live`; `/health/ready` and database-dependent operations return 503
until their respective activation/readiness requirements are met. Zdb activation
starts bootstrap and signing preparation before administrative admission opens.
Deactivation closes admission; it does not require closing the health listener.
The `zumd: listening` startup event therefore does not indicate IAM readiness.
`zumd: active` indicates completed active-node preparation, but a new store still
requires initial-admin enrollment before `/health/ready` succeeds. Invalid key
or schema checks may fail after passive listening starts; they must never open
active admission. See the implementation ledger for runtime verification status.

Administrative paths have the `/admin` prefix. GET carries filters in the query string
and no body. Mutations carry JSON; DELETE has no body. Known paths called with
an unsupported method return 405 with a registry-derived `Allow` header.

Existing-record writes require `If-Match`; PUT creation requires
`If-None-Match: *`. Creation, publication, rotation, and bounded bulk
mutations require `Idempotency-Key`. Secret digests, private/protected key
material, provider secrets, tokens, credential public keys, and server session
handles are never returned by ordinary queries.

After authentication and authorization, non-GET management operations
emit request-level `Administration` events through ZiLog. An event carries the exact
catalog operation ID, authenticated actor, target application and path,
success/failure outcome, and the same opaque correlation ID returned in an
error response. Request bodies, query values, credentials, and secrets are not
copied into event metadata. Domain workflows may additionally emit their more specific
credential, revocation, key-rotation, or principal-change events.

Logging is outside business sagas; compensation restores application state, not
log history. Alert persistence, distribution, replay, and retention use existing
Ztc facilities. There is no Zum audit table or audit query/cleanup API in the
target design. The two retired operation IDs remain reserved and have no route
or usable permission name; later action IDs retain their values. Old audit-row
tests have been replaced with ZiLog attribution, outcome, correlation and secret-
redaction checks in the HTTP fixture.

## Administrative client

The initial interface is:

```sh
zum --config FILE login
zum --config FILE OPERATION --json FILE
```

Offline database-secret key rotation is a server maintenance operation, not a
REST administrative action. Stop all writers and run
`zumd --rekey --issuer=URL` with the normal Zdb connection/configuration.
Inject the old key as `ZUM_DB_KEY` and the new key as `ZUM_DB_NEW_KEY`; both are
base64-encoded random 256-bit secrets, never command-line arguments. Maintenance
does not start an HTTP listener. An incomplete rotation blocks normal readiness;
resume with the same two keys. Keep both until the command reports completion
after the store drain, then restart every node with the new `ZUM_DB_KEY`.
Keep old keys separately for old backups. A completed retry verifies ciphertext
without rewriting it. This changes encrypted secret fields only, not signing
keys, OAuth client-secret values, user assignments or ordinary database fields.
See IMPLEMENTATION.md for the current validation status of this operation.

The non-secret configuration contains the exact application `issuerURL`
(`https://auth.example/oauth2/APP_ID`) and the independent administrative
`managementURL` origin. It may also contain
`clientID` (default `zum-admin`), `scope` (default `zum.admin`), `callbackPort`,
`loginTimeout`, optional `caPath` for a private issuer CA, and an owner-only
`credentialFile`. Empty `caPath` retains native system trust. Login uses an external
browser, a loopback redirect, authorization code, and S256 PKCE. Without a
credential file, operation invocations perform login and retain tokens only in
memory.

Request JSON supplies normal path/query/body fields. These reserved fields are
removed from the payload and mapped to transport controls:

| Field | Meaning |
| --- | --- |
| `$ifMatch` | `If-Match` header |
| `$ifNoneMatch` | `If-None-Match` header |
| `$idempotencyKey` | `Idempotency-Key` header |
| `$secretOutput` | Explicit owner-only file for a secret-bearing response |

Potentially secret-producing application enrollment, client creation, and
client-secret rotation require `$secretOutput`; their response is never
printed to the terminal.

### Manual browser/passkey acceptance

For the required manual acceptance run, use a fresh PostgreSQL store and a real
HTTPS issuer origin (normally a TLS reverse proxy to the loopback `zumd`
listener). Inject `ZUM_DB_KEY`, `ZDB_MODULE`, and `ZDB_CONNECT` through the test
deployment, then start the daemon with the intended issuer, RP ID, administrator
login, and an owner-only bootstrap output file, for example:

```sh
zumd --issuer=https://iam.example.test \
  --rp-id=iam.example.test --admin=admin@example.test \
  --bootstrap-output=/run/secrets/zum-bootstrap-url
```

Open the one-time URL from that file in a normal browser and create the initial
platform passkey. Configure `zum` with the core application's issuer URL shown
by discovery, the `zumd` origin as `managementURL`, the `zum-admin`
client, a loopback callback, and (when applicable) the private test CA, then run
`zum --config FILE login`. The browser must complete the passkey assertion and
return to the state-bound loopback callback. Use the authenticated CLI to enroll
the ping service/client and user as described in the example README, inject only
`ZUM_CLIENT_SECRET` into `zumpingd`, and run `zumping` without `--no-browser`.
Record the browser/platform authenticator and observed login/pong result in the
implementation ledger. The virtual authenticator fixtures do not satisfy this
manual gate.

## Public OAuth 2.0 and OIDC endpoints

Outbound OIDC-provider connections use system TLS trust by default. For a private
CA, add `oidc: {caPath: "/path/to/ca.pem"}` to the `zumd --config` node file.
The path can name a CA bundle or native CA directory; it is non-secret deployment
configuration. Certificate and hostname verification remain enabled. OIDC-provider
client secrets remain encrypted in the database, not in this section.

The administrative routes above are separate from the issuer's public protocol
surface:

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/.well-known/oauth-authorization-server/oauth2/{appID}` | OAuth authorization-server metadata. |
| GET | `/oauth2/{appID}/.well-known/openid-configuration` | OpenID Provider metadata. |
| GET | `/oauth2/{appID}/v1/authorize` | Authorization-code initiation with S256 PKCE. |
| POST | `/oauth2/{appID}/v1/token` | Authorization-code, refresh-token, and client-credentials exchange. |
| POST | `/oauth2/{appID}/v1/revoke` | Token revocation. |
| GET | `/oauth2/{appID}/v1/keys` | Active public signing keys for that issuer. |
| GET/POST | `/oauth2/{appID}/v1/userinfo` | Claims selected by an access token carrying `openid`. |

An interactive request may combine a resource scope with the enrolled client's
`openid`, `profile`, `email`, and (when refresh tokens are enabled)
`offline_access` identity scopes. `offline_access` always requires interactive
consent; `prompt=consent` explicitly selects that path, while `prompt=none`
fails with `consent_required` when consent is needed. The exact granted scope
string and original nonce survive code exchange and refresh. An approved
`offline_access` request creates the refresh-token family; an ordinary code
exchange does not. `openid` causes an ES256 ID token to be returned; `profile`
and `email` control only their corresponding ID-token and UserInfo claims.
Identity scopes are not accepted for client-credentials grants.
