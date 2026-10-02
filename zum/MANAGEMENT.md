# Management operations

[ZumMgmt.hh](src/ZumMgmt.hh) is the shared wire catalog for Zum
administration. Request and response JSON fields and management query
parameters use snake_case (`app_id`, `client_id`, `role_ids`, etc.).
C++ fields and native configuration retain their existing names. Its 61
`MgmtOp` values each identify exactly one remote call and one core action named
`Zum.<operation>`. The route registry in
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
`zum.admin` role contains every management action. The distinct
`zum.catalog` role contains only operation-status lookup and catalog
publication. Enrolled catalog publishers never receive `zum.admin` implicitly.

A bearer token must carry the exact operation action. Current database state
must additionally prove either the active core superuser assignment or an
active `admin_access` delegation for the authenticated user/client and target
application. The service which owns an application may publish that
application's available action/standard-role catalog. Each role name is also
the corresponding OAuth resource-scope name at runtime; catalog publication
never assigns roles to users or restores deleted privileges.

## Areas

| Area | Operations |
| --- | --- |
| Issuer/catalog | `issuerQuery`, `operationQuery`, `catalogPublish` |
| Applications | `appQuery`, `appEnroll`, `appUpdate`, `appState` |
| Users/credentials | `userQuery`, `userInvite`, `userUpdate`, `userState`, `userRecover`; `credentialQuery`, `credentialUpdate`, `credentialState` |
| Assignments | `assignmentQuery`, `assignmentAdd`, `assignmentRoles`, `assignmentState` |
| Actions/roles | `actionQuery`, `actionAdd`, `actionState`; `roleQuery`, `roleAdd`, `roleUpdate`, `roleActions`, `roleState`, `roleDelete` |
| Clients | `clientQuery`, `clientAdd`, `clientUpdate`, `clientState`, `clientSecretRotate` |
| Delegation | `clientAccessQuery`, `clientAccessSet`, `clientAccessState`; `adminAccessQuery`, `adminAccessSet`, `adminAccessState` |
| Federation | `providerQuery`, `providerAdd`, `providerUpdate`, `providerState`; `authPolicyQuery`, `authPolicySet`; `roleMapQuery`, `roleMapSet`, `roleMapDelete`; `identityQuery`, `evidenceQuery` |
| Runtime state | `sessionQuery`, `sessionRevoke`; `consentQuery`, `consentRevoke`; `grantQuery`, `grantRevoke`, `grantCleanup` |
| Keys | `signKeyQuery`, `signKeyAdd`, `signKeyRetire` |

The route registry defines the method/path table.
[Zum terminology](../zum_terminology.md) defines the application, principal,
role, action and scope vocabulary. Schema version 24 requires reprovisioning existing databases. It renames
the assignment table, saga identities, authority-version fields and management
operations; previous schemas and REST names are unsupported.

## HTTP rules

External identities remain bound to provider/issuer/subject. Their projected
names are synthetic `oidc:<provider>:<user_id>` identifiers; a browser login hint
does not rename or link that identity. Inviting a local user with the same
stored projected name invalidates the external user's existing session/grant
authority versions within the invitation saga. A local account's presence also
blocks new external issuance for that name, including when the local account is
pending or suspended. No identity, assignment or role assignment is transferred.
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
data model. The HTTP fixture checks ZiLog attribution, outcome, correlation
and secret redaction.

## Administrative client

The administrative interface is:

```sh
zum --config FILE login
zum --config FILE app add NAME AUDIENCE > enrollment.json
zum --config FILE RESOURCE [SUBRESOURCE] VERB REQUIRED_ARGS... [OPTIONS]
```

Offline database-secret key rotation is a server maintenance operation, not a
REST administrative action. Stop all writers and run
`zumd --rekey --issuer=URL` with the normal Zdb connection/configuration and
the same secure Vault store and account as the running daemon. The old key is
loaded from Vault; supply the new base64-encoded random 256-bit key as
`ZUM_DB_KEY`, never on the command line. Maintenance does not start an HTTP
listener. An incomplete rotation blocks normal readiness; retry with the same
new key. Completion means both the database store drain and Vault publication
succeeded. Retain the new key until completion, and retain old keys separately
for old backups. Afterward, normal starts can omit `ZUM_DB_KEY`. A completed
retry verifies ciphertext without rewriting it. This changes encrypted secret
fields only, not signing keys, OAuth client-secret values, user assignments or
ordinary database fields.
See IMPLEMENTATION.md for the current validation status of this operation.

The non-secret configuration contains the exact application `issuerURL`
(`https://auth.example/oauth2/APP_ID`) and the independent administrative
`managementURL` origin. It may also contain
`clientID` (default `zum-admin`), `scope` (default `zum.admin`), `callbackPort`,
`loginTimeout`, and optional `caPath` for a private issuer CA. Empty `caPath`
retains native system trust. Login uses an external browser, a loopback
redirect, authorization code, and S256 PKCE. The CLI stores credentials through
`Ztls::Vault` under `$HOME/.zum` (or `ZUM_HOME`) and opens the vault only for
each load/save. A missing stored credential triggers browser login.

Required request fields are positional, in the order shown by `zum --help`.
Commands form a hierarchy, such as `zum user add`, `zum user list`, and
`zum client access set`. Use `zum user --help` or `zum client access --help`
to show a command group's syntax. The `assign` commands call the
`assignmentQuery`, `assignmentAdd`, `assignmentRoles` and `assignmentState`
operations under `/admin/apps/{app_id}/assignments`.
Optional fields use named options (`--label`, `--redirect-uris`,
`--refresh-allowed`, etc.). Lists are comma-separated; an empty argument denotes
an empty list. Boolean values are `true` or `false`. `--grants` takes named
flags separated by commas: `AuthCode`, `ClientCredentials`, and `Refresh`.
For example, `--grants AuthCode,Refresh` selects authorization code and refresh.
The REST `grants` field uses the same comma-separated names. The nested catalog
supplied to `catalog publish` is an individual JSON object argument. `zum` constructs the
request JSON, quotes strings, and encodes IDs without floating-point conversion.
`app add` enrolls an application (`appEnroll` is its wire operation).
`user add NAME` invites a user to enroll a passkey. `user list [NAME]` accepts
an optional positional name filter; omit it to list users or filter by `--id`.
The `assign` group manages a user's application assignment: `assign add APP_ID
USER_ID` creates it, `assign roles APP_ID USER_ID ROLE_IDS` replaces its roles,
and `assign state APP_ID USER_ID STATE` changes its lifecycle state.

Configuration is selected by `--config FILE`, then `ZUM_CONFIG`, then
`$ZUM_HOME/zum.cf` (default `$HOME/.zum/zum.cf`). Browser-login prompts and
diagnostics go to stderr; HTTP response JSON goes to stdout, including
secret-bearing enrollment, invitation, recovery and client responses. Use
`umask 077` before redirecting those responses to a new file.

Transport controls are command-line options:

| Option | Meaning |
| --- | --- |
| `--if-match ETAG` | `If-Match` header; preserve the ETag's embedded quotes |
| `--if-none-match '*'` | `If-None-Match` header for creation |
| `--idempotence KEY` | Optional explicit `Idempotency-Key` for retrying a request |

For operations which need idempotency, `zum` generates a random 128-bit key once
per invocation and retains it through token refresh and request replay. A failed
HTTP request reports that key on stderr for an explicit retry. A new invocation
without `--idempotence` is a new operation; retain successful responses and do
not blindly rerun creates. Use `zum operation list --operation appEnroll
--idempotence KEY` to look up an enrollment's status with its key.

For example:

```sh
export ZUM_CONFIG=/path/to/admin.cf
umask 077
zum app add ztchub-local http://localhost:8090/admin > hub-enrollment.json
zum role list APP_ID --name Client
zum assign add APP_ID USER_ID > assignment.json
zum assign roles APP_ID USER_ID ROLE_ID --if-match '"ETAG"'
zum client add APP_ID native --label "Local zdash" \
  --redirect-uris http://127.0.0.1:8081/callback \
  --grants AuthCode,Refresh --refresh-allowed true > dashboard-client.json
```

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
the ping application and user as described in the example README, inject only
`ZUM_CLIENT_SECRET` for its same-name default client into `zumpingd`, and run
`zumping` without `--no-browser`.
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
| GET | `/.well-known/oauth-authorization-server/oauth2/{app_id}` | OAuth authorization-server metadata. |
| GET | `/oauth2/{app_id}/.well-known/openid-configuration` | OpenID Provider metadata. |
| GET | `/oauth2/{app_id}/v1/authorize` | Authorization-code initiation with S256 PKCE. |
| POST | `/oauth2/{app_id}/v1/token` | Authorization-code, refresh-token, and client-credentials exchange. |
| POST | `/oauth2/{app_id}/v1/revoke` | Token revocation. |
| GET | `/oauth2/{app_id}/v1/keys` | Active public signing keys for that issuer. |
| GET/POST | `/oauth2/{app_id}/v1/userinfo` | Claims selected by an access token carrying `openid`. |

An interactive request may combine a role-derived resource scope with the enrolled client's
`openid`, `profile`, `email`, and (when refresh tokens are enabled)
`offline_access` identity scopes. `offline_access` always requires interactive
consent; `prompt=consent` explicitly selects that path, while `prompt=none`
fails with `consent_required` when consent is needed. The exact granted scope
string and original nonce survive code exchange and refresh. An approved
`offline_access` request creates the refresh-token family; an ordinary code
exchange does not. `openid` causes an ES256 ID token to be returned; `profile`
and `email` control only their corresponding ID-token and UserInfo claims.
Identity scopes are not accepted for client-credentials grants.

## Application audience

`appEnroll` requires `audience`, the immutable token audience of the
application/service. `appQuery` returns this field. Application state controls
its availability. `clientAccessSet` accepts `role_ids`; the URL identifies the
target application. `consentRevoke` can filter by user, client and application.

`ssfRegister` (`POST /admin/apps/{app_id}/ssf`) registers or renews a client's
SSF receiver. It uses the application's existing `catalogPublish` delegation
and requires a workload token. The JSON fields are `receiver_id`, `delivery_url`,
`callback_auth`, and `expires_in` (default 300 seconds). The response supplies the
server-capped `expires_in` and Unix `expires`. No idempotency key is required:
each successful call extends the lease. Receiver IDs are scoped by application
and authenticated client; the notification audience is the application's
immutable audience. Clients must renew before expiry. `Zum::Service` handles
registration and timer renewal automatically.
