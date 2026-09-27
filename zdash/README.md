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
checkout, run `make -j8`; for a new build, for example:

```sh
./z.config -P /opt/z
make -j8
```

The remaining commands run from the repository root in Bash. They require
Python 3, OpenSSL, curl, a graphical desktop and a browser with a usable
passkey authenticator. Source-tree executable wrappers load the build's shared
libraries; installation is not required.

```sh
export ZROOT="$PWD"
export ZLOCAL="$HOME/.local/state/zdash-local"
umask 077
mkdir -p "$ZLOCAL"
```

Keep this directory between runs: it contains the IAM database, its encryption
key, and local credentials. Initialize these files only on the first run:

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
export ZUM_SSF_ZTCHUB_AUTH="Bearer $(cat "$ZLOCAL/ssf.secret")"

"$ZROOT/zum/src/zumd" --config="$ZLOCAL/zumd.cf" \
  --vault-store=keyring \
  --issuer=http://localhost:8090 --addr=127.0.0.1 --port=8090 \
  --rp-id=localhost --admin=admin@localhost \
  --bootstrap-output="$ZLOCAL/bootstrap-url"
```

Wait for active-node preparation. In the setup terminal, first record the core
application ID.

The core application ID is generated, not a fixed value. The enrollment page's
source contains `/oauth2/CORE_APP_ID/v1/passkey/begin`. Record that decimal ID
as `CORE_APP_ID` in the setup terminal. For example, the following extracts it
from the enrollment page before completing enrollment:

```sh
CORE_APP_ID=$(curl --fail --silent --show-error "$(cat "$ZLOCAL/bootstrap-url")" |
  python3 -c 'import re,sys; print(re.search(r"/oauth2/([0-9]+)/v1/passkey/begin", sys.stdin.read())[1])')
```

Now open the URL in `bootstrap-url` in the
local browser and create the administrator passkey. The capability expires
after 900 seconds by default. If it expires before enrollment, stop the daemon
and repeat the command with `--bootstrap-reissue` to issue another capability.
Keep the issuer, RP ID and database key unchanged on subsequent starts.

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

Complete the browser passkey login and consent. The CLI stores credentials
through `Ztls::Vault` and uses them for subsequent operations. Set `ZUM_HOME`
to a private directory to select its vault home; the default is `$HOME/.zum`.
`zdash` uses its own vault home, `$HOME/.zdash` or `ZDASH_HOME`, for refresh
credentials and can resume a later session without browser login. `--no-browser`
prints a URL to open manually. Run native login commands sequentially because
they share callback port 8081.

Define two conveniences for the remaining commands. `zumop` reads a JSON
request from standard input. `jget` extracts a field without losing precision
in 64-bit numeric IDs:

```sh
zumop() {
  cat > "$ZLOCAL/request.json"
  "$ZROOT/zum/src/zum" --config="$ZLOCAL/admin.cf" "$1" \
    --json "$ZLOCAL/request.json"
}
jget() {
  python3 -c 'import json,sys
v=json.load(sys.stdin)
for key in sys.argv[1].split("."):
    v=v[int(key)] if isinstance(v,list) else v[key]
print(v)' "$1"
}
```

These are first-run provisioning commands. Retain each successful response;
do not blindly rerun creates with new idempotency keys. Existing-record changes
use their current ETag via `$ifMatch`, while creation of an access grant uses
`$ifNoneMatch: "*"`. See [management operations](../zum/MANAGEMENT.md).

## Enroll and start `ztchub`

Enroll the hub application and its confidential catalog-publishing client:

```sh
zumop appEnroll <<EOF
{"name":"ztchub-local","audience":"http://localhost:8090/admin",
 "\$idempotencyKey":"$(openssl rand -hex 16)",
 "\$secretOutput":"$ZLOCAL/hub-enrollment.json"}
EOF
APP_ID=$(jget item.appID < "$ZLOCAL/hub-enrollment.json")
HUB_CLIENT_ID=$(jget item.client_id < "$ZLOCAL/hub-enrollment.json")
```

Secret-bearing replies go to the explicitly named owner-only file. The
enrollment result contains `appID`, `client_id` and `client_secret`. The
management client receives catalog publication authority, not full admin
authority. It is distinct from the dashboard and device clients below.

This recipe uses the management audience, `http://localhost:8090/admin`, as
the hub application's immutable audience, matching the live hub integration
fixture. Use that exact value throughout this setup; it is an OAuth audience
identifier, not the WSS connection URL.

Configure the daemon's outgoing SSF delivery. Append this once to `zumd.cf`:

```sh
cat >> "$ZLOCAL/zumd.cf" <<EOF
,
oidc: {caPath: "$ZLOCAL/hub-cert.pem"},
ssf: {
  issuer: "http://localhost:8090/oauth2/$APP_ID",
  receivers: [{
    receiverID: "$HUB_CLIENT_ID", appID: $APP_ID,
    audience: "http://localhost:8090/admin",
    deliveryURL: "https://localhost:8444/ssf",
    secretName: "ZUM_SSF_ZTCHUB_AUTH", revision: 1
  }]
}
EOF
```

Stop `zumd` with Ctrl-C and restart it with the same command. After a successful
start, `ZUM_DB_KEY` and `ZUM_SSF_ZTCHUB_AUTH` may be unset: their values are in
the selected secure Vault store. To rotate the callback authorization value,
restart `zumd` with the replacement variable set.
The `oidc.caPath` setting also supplies trust for outgoing SSF HTTPS delivery.
The callback Authorization value must match on both processes, including its
`Bearer ` prefix. This wires refresh-family revocation delivery to the hub.

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
actions: ["Request", "Telemetry"],
roles: ["Client", "Agent"],
expectedAgents: 2, publishersPerAgent: 64,
activeFrontEnds: 2, subscriptionsPerFrontEnd: 2
EOF
```

In a second service terminal, with the same `ZROOT` and `ZLOCAL`, start the hub:

```sh
export ZUM_CLIENT_SECRET=$(python3 -c \
  'import json,sys; print(json.load(open(sys.argv[1]))["item"]["client_secret"])' \
  "$ZLOCAL/hub-enrollment.json")
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
zumop roleQuery <<EOF > "$ZLOCAL/roles.json"
{"appID":$APP_ID}
EOF
CLIENT_ROLE=$(python3 -c 'import json,sys; print(next(r["id"] for r in json.load(sys.stdin)["items"] if r["name"]=="Client"))' < "$ZLOCAL/roles.json")
AGENT_ROLE=$(python3 -c 'import json,sys; print(next(r["id"] for r in json.load(sys.stdin)["items"] if r["name"]=="Agent"))' < "$ZLOCAL/roles.json")
zumop userQuery <<EOF > "$ZLOCAL/user.json"
{"name":"admin@localhost","source":"Local"}
EOF
USER_ID=$(jget items.0.id < "$ZLOCAL/user.json")

zumop membershipAdd <<EOF > "$ZLOCAL/membership.json"
{"appID":$APP_ID,"userID":$USER_ID,
 "\$idempotencyKey":"$(openssl rand -hex 16)"}
EOF
```

Membership creation and role assignment are separate operations. Preserve the
ETag's JSON quoting when assigning the `Client` role:

```sh
python3 - "$ZLOCAL/membership.json" "$APP_ID" "$USER_ID" "$CLIENT_ROLE" <<'PY' | zumop membershipRoles
import json, sys
member = json.load(open(sys.argv[1]))["item"]
print(json.dumps({"appID": int(sys.argv[2]), "userID": int(sys.argv[3]),
                  "roleIDs": [int(sys.argv[4])], "$ifMatch": member["etag"]}))
PY

zumop clientAdd <<EOF
{"appID":$APP_ID,"label":"Local zdash","profile":"native",
 "redirectURIs":["http://127.0.0.1:8081/callback"],
 "grants":5,"refreshAllowed":true,
 "\$idempotencyKey":"$(openssl rand -hex 16)",
 "\$secretOutput":"$ZLOCAL/dashboard-client.json"}
EOF
DASH_CLIENT_ID=$(jget item.id < "$ZLOCAL/dashboard-client.json")

zumop clientAccessSet <<EOF
{"appID":$APP_ID,"clientID":"$DASH_CLIENT_ID",
 "roleIDs":[$CLIENT_ROLE],"\$ifNoneMatch":"*"}
EOF
```

Grant mask `5` enables authorization code (`1`) and refresh token (`4`). This
is a public native client: do not give the dashboard the hub's client secret.
Both the user's membership and the client's access grant must allow `Client`.
Being a core administrator alone does not grant access to this application.

To use a separate local user, call `userInvite` with `name`, `$idempotencyKey`
and `$secretOutput`, open the returned `item.enrollmentURL` to register their
passkey, then apply the membership commands to that user's `item.id`.

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
telRing: {size: 1048576},
group: "App", filter: "*", interval: 1000
EOF

"$ZROOT/zdash/src/zdash" --config="$ZLOCAL/zdash.cf"
```

Complete browser login as the user assigned above and approve consent. The
dashboard sends its access token only in the WSS upgrade Authorization header;
the hub's `/session` cookie endpoint is for browser WSS clients and is not
needed here. `caPath` (or `--ca`) configures dashboard trust for both OAuth
HTTPS and WSS. The absolute `gtkGlade` path allows launching from any directory.

With no `deviceID`, the default `App`/`*` subscription requests publisher
inventory across devices. An empty dashboard is expected until an agent and
publisher are active. To target a device, use `--device-id=DEVICE_CLIENT_ID`.
Other subscription groups require a device ID; `publisherID`, `group`, `filter`
and `interval` can be selected in the configuration.

The current dashboard runs one WSS session per login. It does not renew the
live connection automatically when its access token expires; restart it to
refresh the credential stored in Vault and open a new session.

## Optional: supply local telemetry

Create a server-side device client with grant mask `2` (`client_credentials`)
and allow the `Agent` role:

```sh
zumop clientAdd <<EOF
{"appID":$APP_ID,"label":"Local telemetry device","profile":"server",
 "redirectURIs":[],"grants":2,
 "\$idempotencyKey":"$(openssl rand -hex 16)",
 "\$secretOutput":"$ZLOCAL/device-client.json"}
EOF
DEVICE_ID=$(jget item.id < "$ZLOCAL/device-client.json")
zumop clientAccessSet <<EOF
{"appID":$APP_ID,"clientID":"$DEVICE_ID",
 "roleIDs":[$AGENT_ROLE],"\$ifNoneMatch":"*"}
EOF
```

Provision the agent's confidential client secret through its environment.
The agent discovers the token endpoint and obtains a short-lived `Agent` token
before each WSS connection:

```sh
export ZTC_ISSUER="http://localhost:8090/oauth2/$APP_ID"
export ZTC_CLIENT_ID="$DEVICE_ID" ZTC_DEVICE_ID="$DEVICE_ID"
export ZTC_CLIENT_SECRET=$(jget item.client_secret < "$ZLOCAL/device-client.json")
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
| Login succeeds but WSS is denied | Check both user membership and client access grant for `Client`; device tokens require `Agent` and `client_credentials`. |
| Dashboard cannot open | Check the GTK display and absolute `gtkGlade` path. |
| Dashboard has no rows | Check agent acceptance, publisher/ring settings and selected device/group/filter. |
| Session stops after a while | The hub closes expired-token connections; obtain new credentials and reconnect. |

For automated verification, see [dashboard tests](test/README.md) and the
[hub process fixture](../ztc/itest/ztchubtest.py). The instructions above require
a real browser/passkey login; virtual-authenticator tests do not establish
that a particular desktop's passkey integration works.
