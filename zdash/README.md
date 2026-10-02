# Local dashboard setup

`zdash` is a GTK dashboard. It authenticates through `zumd` using a browser
and passkey, then subscribes to `ztchub` over WSS. `ztchub` publishes its IAM
catalog to `zumd`; telemetry comes from separately authenticated agents.

This walkthrough uses a source-tree build on Linux, SQLite for IAM persistence,
and one machine for every process and the browser. All listeners bind to
loopback. Use these origins consistently: `localhost` and `127.0.0.1` are not
interchangeable in issuer URLs or WebAuthn configuration.

| Endpoint | Purpose |
| --- | --- |
| `http://localhost:8090` | `zumd`, browser/passkey login and management |
| `wss://localhost:8443/ztc` | Hub agent and dashboard connections |
| `https://localhost:8444/ssf` | Hub Shared Signals Framework callback |
| `http://127.0.0.1:8081/callback` | Native OAuth client callback |

HTTP is enabled explicitly for local OAuth development; hub connections still
use TLS with certificate verification. For a remote deployment, use HTTPS for
the authorization server as well.

## Build and prepare a local workspace

Build with GTK and SQLite enabled (do not pass `-G` or `-S` to `z.config`). See
the [repository README](../README.md) for dependencies. In an already configured
checkout, run `make -j4` followed by `make -C zdash -j4`; for a new build,
for example:

```sh
./z.config -P /opt/z
make -j4
make -C zdash -j4
```

The remaining commands run from the repository root in Bash. They require
`jq`, OpenSSL, curl, a graphical desktop and a browser with a usable passkey
authenticator. Source-tree executable wrappers load the build's shared libraries;
installation is not required. Current `zum`/`zumd` JSON represents 64-bit IDs as
strings. The `jq -r` filters below extract their decimal text directly; no
arithmetic or special jq number support is needed.

```sh
export ZROOT="$PWD"
export ZLOCAL="$HOME/.local/state/zdash-local"
umask 077
mkdir -p "$ZLOCAL"
```

Keep this directory between runs: it contains the IAM database, its encryption
key, and local credentials. This walkthrough provisions a fresh database at
schema version 24. `zumd` rejects non-empty databases at other schema versions;
use a separate workspace and reprovision when upgrading from an older schema.
Initialize these files only on the first run:

```sh
openssl rand -base64 32 > "$ZLOCAL/db.key"
openssl rand -hex 32 > "$ZLOCAL/ssf.secret"
openssl req -x509 -newkey rsa:2048 -nodes -days 30 \
  -subj /CN=localhost \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' \
  -keyout "$ZLOCAL/hub-key.pem" -out "$ZLOCAL/hub-cert.pem"
cp zum/itest/zumd.cf "$ZLOCAL/zumd.cf"
mkdir -p "$ZLOCAL/alerts"
cat >> "$ZLOCAL/zumd.cf" <<EOF
,
ztcPublish: true,
ztc: {id: "zumd-local", alertPrefix: "$ZLOCAL/alerts/zumd"}
EOF
```

The copied node configuration defines a standalone Zdb host and its isolated
database/store threads. Its `${MODULE}` and `${CONNECT}` substitutions are
supplied by `zumd` from the environment below. The self-signed certificate is
trusted explicitly by the native clients; no browser certificate exception is
needed for the HTTP login origin.

## Start `zumd` and bootstrap the administrator

In a dedicated terminal, set `ZROOT` and `ZLOCAL` to the same absolute paths and
run:

```sh
export ZUM_DB_KEY="$(cat "$ZLOCAL/db.key")"
export ZDB_MODULE="$ZROOT/zdb_sqlite/src/.libs/libZdbSL.so"
export ZDB_CONNECT="$ZLOCAL/iam.db"
export ZTC_RING=zdash-local ZTC_DIR=zdash-local

"$ZROOT/zum/src/zumd" --config="$ZLOCAL/zumd.cf" \
  --vault-store=keyring \
  --issuer=http://localhost:8090 --addr=127.0.0.1 --port=8090 \
  --rp-id=localhost --admin=admin@localhost \
  --bootstrap-output="$ZLOCAL/bootstrap-url"
```

Wait for `zumd: listening` and `zumd: active`. In the setup terminal, first
record the core application ID.

The core application ID is generated, not a fixed value. The enrollment page's
source contains `/oauth2/CORE_APP_ID/v1/passkey/begin`. Record that decimal ID
as `CORE_APP_ID` in the setup terminal. For example, the following extracts it
from the enrollment page before completing enrollment:

```sh
CORE_APP_ID=$(curl --fail --silent --show-error "$(cat "$ZLOCAL/bootstrap-url")" |
  jq -Rrse 'capture("/oauth2/(?<id>[0-9]+)/v1/passkey/begin").id')
```

Now open the URL in `bootstrap-url` in the
local browser and create the administrator passkey. The capability expires
after 900 seconds by default. If it expires before enrollment, stop the daemon
and repeat the command with `--bootstrap-reissue` to issue another capability.
This replaces the existing `bootstrap-url` file and restricts its permissions
to the owner. The same command also works after administrator enrollment: it
issues a new single-use link to add another passkey to the existing administrator.
Existing passkeys, the user identity and application assignments remain valid;
there is no need to delete the database. Reissue invalidates the previous link
and any unfinished registration ceremony.
Keep the issuer, RP ID and database key unchanged on subsequent starts.
Normal restarts can omit `--admin` and `--bootstrap-output`; the administrator
identity and bootstrap state are persisted. Reissuing enrollment still requires
`--bootstrap-output`.

`/health/live` indicates a running listener. `/health/ready` returns 503 until
the initial administrator has enrolled and IAM is ready:

```sh
curl --fail http://localhost:8090/health/ready
```

## Log in with the administrative CLI

Back in the setup terminal:

```sh
cat > "$ZLOCAL/admin.cf" <<EOF
issuerURL: "http://localhost:8090/oauth2/$CORE_APP_ID",
managementURL: "http://localhost:8090",
clientID: "zum-admin",
scope: "zum.admin offline_access",
callbackPort: 8081,
loginTimeout: 180,
loopbackTest: true
EOF
"$ZROOT/zum/src/zum" --config="$ZLOCAL/admin.cf" login
```

Complete the browser passkey login and consent. The consent page identifies the
requesting client and application and lists each requested scope. The CLI stores
credentials through `Ztls::Vault` and uses them for subsequent operations. Set `ZUM_HOME`
to a private directory to select its vault home; the default is `$HOME/.zum`.
`zdash` uses its own vault home, `$HOME/.zdash` or `ZDASH_HOME`, for refresh
credentials and can resume a later session without browser login. `--no-browser`
prints a URL to open manually. Run native login commands sequentially because
they share callback port 8081.

If the seeded administrator client is missing its default redirect URIs or
identity scopes, login can return `invalid_request` even though readiness is
healthy. Stop `zumd` and run this offline maintenance command in its terminal,
with the same environment, database key and issuer. The database must already
be at the current schema version:

```sh
"$ZROOT/zum/src/zumd" --config="$ZLOCAL/zumd.cf" \
  --vault-store=keyring --issuer=http://localhost:8090 \
  --admin=admin@localhost --bootstrap-output="$ZLOCAL/bootstrap-url" \
  --once --repair-admin-client
```

This restores missing default redirects and identity scopes through Zdb without
reissuing enrollment. Restart `zumd` with its normal command and retry CLI login.

Select the administrative configuration for the remaining commands and use
owner-only permissions for redirected enrollment responses. `jq -r` extracts
raw values, preserving embedded quotes in strings such as ETags:

```sh
export ZUM_CONFIG="$ZLOCAL/admin.cf"
export PATH="$ZROOT/zum/src:$PATH"
umask 077
```

`zum` selects configuration from `--config FILE`, then `ZUM_CONFIG`, then
`$ZUM_HOME/zum.cf` (or `$HOME/.zum/zum.cf`). Required fields are positional;
optional fields use named options. Run `zum app --help`, `zum user --help`,
`zum assign --help` or `zum client access --help` for group-specific syntax.

These are first-run provisioning commands. `zum` generates idempotency keys;
retain each successful response and do not blindly rerun creates. For a failed
request, retain the retry key reported on stderr and pass it with
`--idempotence KEY` when retrying the same operation. Existing-record changes
use their current ETag via `--if-match`, while creation of an access grant uses
`--if-none-match '*'`. See [management operations](../zum/MANAGEMENT.md).

## Enroll and start `ztchub`

Enroll the hub application and its confidential catalog-publishing client:

```sh
zum app add ztchub-local http://localhost:8090/admin > "$ZLOCAL/hub-enrollment.json"
APP_ID=$(jq -er '.item.app_id' "$ZLOCAL/hub-enrollment.json")
HUB_CLIENT_ID=$(jq -er '.item.client_id' "$ZLOCAL/hub-enrollment.json")
```

Replies go to stdout; shell redirection saves this secret-bearing response. The
enrollment result contains `app_id`, `client_id` and `client_secret`. The
management client receives catalog publication authority, not full admin
authority. It is distinct from the dashboard and device clients below.

This recipe uses the management audience, `http://localhost:8090/admin`, as
the hub application's immutable audience, matching the live hub integration
fixture. Use that exact value throughout this setup; it is an OAuth audience
identifier, not the WSS connection URL.

Configure trust for the hub's HTTPS SSF callback. Append this once to `zumd.cf`:

```sh
cat >> "$ZLOCAL/zumd.cf" <<EOF
,
oidc: {caPath: "$ZLOCAL/hub-cert.pem"},
ssf: {receiverMax: 1024, leaseMax: 300, errorMax: 5}
EOF
```

Stop `zumd` with Ctrl-C and restart it with the same command. After a successful
start, `ZUM_DB_KEY` may be unset: its value is in the selected secure Vault store.
The `oidc.caPath` setting supplies trust for outgoing SSF HTTPS delivery.

The hub registers its callback with `zumd` using its workload credentials and
renews the registration on a timer at half the granted lease. `zumd` removes
expired receivers and receivers reaching `errorMax` consecutive delivery errors.
Callback credentials belong to the hub; the registration supplies them to
`zumd`, which stores them encrypted. To rotate the callback credential, restart
the hub with a replacement `ZUM_SSF_AUTH`; its registration updates the daemon.

Create the hub configuration:

```sh
cat > "$ZLOCAL/ztchub.cf" <<EOF
listeners: [{
  bind: "127.0.0.1", port: 8443, path: "/ztc",
  cert: "$ZLOCAL/hub-cert.pem", key: "$ZLOCAL/hub-key.pem",
  ssfPort: 8444, browserPath: "/session",
  origins: ["https://localhost:8443"]
}],
issuer: "http://localhost:8090/oauth2/$APP_ID",
audience: "http://localhost:8090/admin",
managementIssuer: "http://localhost:8090/oauth2/$CORE_APP_ID",
managementURL: "http://localhost:8090",
managementClientID: "$HUB_CLIENT_ID",
ssfCallbackPath: "/ssf",
ssfDeliveryURL: "https://localhost:8444/ssf", ssfLease: 300,
actions: ["Request", "Telemetry"],
roles: ["Client", "Agent"],
expectedAgents: 2, publishersPerAgent: 64,
activeFrontEnds: 2, subscriptionsPerFrontEnd: 16
EOF
```

In a second service terminal, with the same `ZROOT` and `ZLOCAL`, start the hub:

```sh
export ZUM_CLIENT_SECRET=$(jq -er '.item.client_secret' "$ZLOCAL/hub-enrollment.json")
export ZUM_SSF_AUTH="Bearer $(cat "$ZLOCAL/ssf.secret")"
"$ZROOT/ztc/src/ztchub" --config="$ZLOCAL/ztchub.cf"
```

Wait for `ztchub ready`. Startup idempotently publishes the `Request` and
`Telemetry` actions and the corresponding `Client` and `Agent` roles. It does
not create dashboard clients or assign users. The standalone hub does not need
`ZDB_MODULE`, `ZDB_CONNECT` or `ZUM_DB_KEY`.

## Grant dashboard access

Query the published roles and the enrolled administrator:

```sh
zum role list "$APP_ID" > "$ZLOCAL/roles.json"
CLIENT_ROLE=$(jq -er 'first(.items[] | select(.name == "Client") | .id)' "$ZLOCAL/roles.json")
AGENT_ROLE=$(jq -er 'first(.items[] | select(.name == "Agent") | .id)' "$ZLOCAL/roles.json")
zum user list admin@localhost --source Local > "$ZLOCAL/user.json"
USER_ID=$(jq -er '.items[0].id' "$ZLOCAL/user.json")

zum assign add "$APP_ID" "$USER_ID" > "$ZLOCAL/assignment.json"
```

Assignment creation and role assignment are separate operations. Preserve the
ETag's embedded quotes when assigning the `Client` role:

```sh
zum assign roles "$APP_ID" "$USER_ID" "$CLIENT_ROLE" \
  --if-match "$(jq -er '.item.etag' "$ZLOCAL/assignment.json")"

zum client add "$APP_ID" native --label "Local zdash" \
  --redirect-uris http://127.0.0.1:8081/callback \
  --grants AuthCode,Refresh --refresh-allowed true > "$ZLOCAL/dashboard-client.json"
DASH_CLIENT_ID=$(jq -er '.item.id' "$ZLOCAL/dashboard-client.json")

zum client access set "$APP_ID" "$DASH_CLIENT_ID" "$CLIENT_ROLE" --if-none-match '*'
```

`AuthCode,Refresh` enables authorization-code and refresh-token flows. This
is a public native client: do not give the dashboard the hub's client secret.
Both the user's assignment and the client's access grant must allow `Client`.
Being a core administrator alone does not grant access to this application.

To use a separate local user, run `zum user add NAME > user-enrollment.json`,
open the returned `item.enrollment_url` to register their passkey, then apply
the assignment commands to that user's `item.id`.

## Start `zdash`

```sh
cat > "$ZLOCAL/zdash.cf" <<EOF
issuerURL: "http://localhost:8090/oauth2/$APP_ID",
clientID: "$DASH_CLIENT_ID",
scope: "Client offline_access",
callbackPort: 8081, loginTimeout: 180, loopbackTest: true,
wssURL: "wss://localhost:8443/ztc",
caPath: "$ZLOCAL/hub-cert.pem",
gtkGlade: "$ZROOT/zdash/src/zdash.glade",
telRing: {name: "zdash-local", size: 1048576},
groups: ["Heap", "Hash", "Thread", "Mx", "Queue", "Hub", "DB"],
filter: "*", interval: 1000, maxSubscriptions: 16
EOF

export ZDASH_HOME="$ZLOCAL/dashboard-home"
"$ZROOT/zdash/src/zdash" --config="$ZLOCAL/zdash.cf"
```

Complete browser login as the user assigned above and approve consent. The
dashboard sends its access token only in the WSS upgrade Authorization header;
the hub's `/session` cookie endpoint is for browser WSS clients and is not
needed here. `caPath` (or `--ca`) configures dashboard trust for both OAuth
HTTPS and WSS. The absolute `gtkGlade` path allows launching from any directory.

The dashboard maintains an `App` inventory subscription across devices and
automatically subscribes to the configured detail groups for each publisher.
An empty dashboard is expected until an agent and publisher are active. Groups
appear when their first real records arrive. Expand populated branches to browse
heaps, hash tables, threads, multiplexers/connections, queues, pools,
engines/links and databases/hosts/tables.
Heaps are grouped by ID, with individual arena rows beneath each ID showing
partition, size, alignment and sharding. Selecting an arena shows its full
telemetry in the detail table.
The size, alignment and sharding headings appear once on the `heaps` row;
ID subgroup rows show only their ID.
Left collapses an expanded row; on a leaf or collapsed row it selects the
parent. Right expands the selected
parent. GTK's Backspace and `+`/`-` bindings remain available.
New objects and updates arrive while
branches are collapsed as well as expanded; selection does not change delivery.

`interval` sets publisher sampling in milliseconds. Continuing subscriptions
survive snapshot EOS without repeated frontend requests. EOS completes a
snapshot and leaves displayed objects intact. Agent reconnection preserves
rows, selection and expansion; a changed publisher start time or explicit
`Shutdown` retires that publisher's old objects. The tree uses `ID`
and adjacent key columns, with field labels on group rows. Compact colored
indicators show RAG status; row text and selection use the GTK theme's colors.
The initial window is 1600 by 900 with a 1000-pixel tree pane; drag the divider
to adjust it. Clicking an object displays its full telemetry in the adjacent
Field/Value table, with alternating row shading, unquoted strings and
right-aligned numbers with thousands separators. Date/time fields use their
framework date formatter. Details follow incoming updates and clear when the
selected object retires. Messaging hands records to a bounded queue, and the
GTK thread performs all presentation updates.

`groups` selects detail groups; an omitted or empty list uses the seven groups
shown above.
`filter` applies to detail objects. `--device-id`/`deviceID` and `publisherID`
restrict the publishers browsed. Inventory uses one frontend subscription slot,
plus one slot per selected group per publisher: `1 + 7*N` with the default
groups. The recipe allows two publishers (15 slots) within its 16-slot hub and
dashboard limits. Increase `maxSubscriptions`, hub `subscriptionsPerFrontEnd`
and queue budgets for larger workloads. Capacity exhaustion and rejected
subscriptions are logged; unrelated subscriptions continue.

The dashboard uses its refresh token to renew credentials at 80% of the access
token lifetime. It saves each rotated refresh token in Vault, reconnects WSS
with the new access token, and restores inventory and detail subscriptions.
The GTK window, tree expansion and selection remain intact during renewal.
A failed renewal is reported and closes the session; restarting uses the
last saved credential or opens the login page if that credential is rejected.

## Optional: supply local telemetry

Create a server-side device client with the `ClientCredentials` grant
and allow the `Agent` role:

```sh
zum client add "$APP_ID" server --label "Local telemetry device" \
  --grants ClientCredentials > "$ZLOCAL/device-client.json"
DEVICE_ID=$(jq -er '.item.id' "$ZLOCAL/device-client.json")
zum client access set "$APP_ID" "$DEVICE_ID" "$AGENT_ROLE" --if-none-match '*'
```

Provision the agent's confidential client secret through its environment.
The agent discovers the token endpoint and obtains a short-lived `Agent` token
before each WSS connection:

```sh
export ZTC_ISSUER="http://localhost:8090/oauth2/$APP_ID"
export ZTC_CLIENT_ID="$DEVICE_ID" ZTC_DEVICE_ID="$DEVICE_ID"
export ZTC_CLIENT_SECRET=$(jq -er '.item.client_secret' "$ZLOCAL/device-client.json")
export ZTC_WSS_URL="wss://localhost:8443/ztc"
export ZTC_CA_PATH="$ZLOCAL/hub-cert.pem"
export ZTCAGENT_HOME="$ZLOCAL/agent-vault"
export ZTC_RING=zdash-local ZTC_DIR=zdash-local
printf 'loopbackTest: true, maxFrame: 65536, vaultStore: "file", vaultTestStore: true\n' > "$ZLOCAL/agent.cf"

"$ZROOT/ztc/src/ztcagent" --config="$ZLOCAL/agent.cf" &
AGENT_PID=$!
```

`zumd` now supplies real local App, multiplex and database telemetry through
the shared ring. It starts publishing before administrator enrollment; the
agent can attach after `zumd` starts. The hub should report
`agent accepted DEVICE_ID`, and the dashboard should show publisher
`zumd-local`. The device ID equals the authenticated service client's ID/token
subject; `zumd-local` identifies the publisher within that device. `zumd` and
the agent must share `ZTC_RING` and `ZTC_DIR`.

The disposable local Vault home stores the client secret after the first
successful exchange. A later agent restart can omit `ZTC_CLIENT_SECRET`.
Production deployments must select a confidential Vault backend. See the
[collector README](../ztc/README.md).

## Shutdown, restart and troubleshooting

Close the dashboard, stop the agent, then stop the hub
and `zumd` with Ctrl-C/SIGTERM. For the background processes above:

```sh
kill "$AGENT_PID"
wait "$AGENT_PID"
unset ZTC_CLIENT_SECRET
```

Restart in the order `zumd`, `ztchub`, agent/publishers, `zdash`, reloading the
same credentials and configuration. Enrollment and role assignments persist;
do not regenerate `db.key` or repeat first-run provisioning. The local TLS
certificate expires after 30 days; renew it and restart the processes using it.

| Symptom | Check |
| --- | --- |
| IAM readiness remains 503 | Complete bootstrap passkey enrollment; inspect `zumd` errors for key/schema/store failures. |
| Passkey fails in the browser | Use `http://localhost:8090` and RP ID `localhost` consistently, on the machine running the browser. |
| OAuth callback fails | Port 8081 must be free; the registered redirect must match `http://127.0.0.1:8081/callback`. |
| OAuth issuer rejected | Use the full `/oauth2/APP_ID` issuer and `loopbackTest: true` for HTTP. Admin uses the core app ID; dashboard uses the hub app ID. |
| Hub startup fails | Check management client credentials, daemon readiness, audience and both required role/action names. |
| WSS TLS fails | Check certificate expiry/SAN and `caPath`; keep verification enabled. |
| Login succeeds but WSS is denied | Check both user assignment and client access grant for `Client`; device tokens require `Agent` and `client_credentials`. |
| Dashboard cannot open | Check the GTK display and absolute `gtkGlade` path. |
| Dashboard has no rows | Check agent acceptance, publisher/ring settings and selected device/group/filter. |
| Credential renewal fails | Check IAM availability and refresh-token validity; restart to retry the saved credential or log in again. |

For automated verification, see [dashboard tests](test/README.md) and the
[hub process fixture](../ztc/itest/ztchubtest.py). The instructions above require
a real browser/passkey login; virtual-authenticator tests do not establish
that a particular desktop's passkey integration works.
