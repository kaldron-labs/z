# Ping example

`zumpingd` is a service using installed `libZum`, not the provider or a database
client. `zumping` is a pure REST/OAuth native client with no Zum library linkage.
Both `zumd` and `zumpingd` must be running for browser login and code redemption.

Enroll the `zumpingd` application using the administrative `zum` client. Its
service credential is used only by `zumpingd` to publish its manifest. Configure
the protected application's exact issuer (`.../oauth2/APP_ID`) as
`zum.issuerURL`, the core management issuer as `zum.managementIssuerURL`, and
the independent administrative resource origin as `zum.managementURL`. Inject
the service secret as `ZUM_CLIENT_SECRET` and the SSF callback Authorization value
as `ZUM_SSF_AUTH` on the first successful start. `zumpingd` saves the
pair in `Ztls::Vault`; later starts may omit both variables and load it from
the Vault. Supplying either variable requires both, and a successful start
replaces the stored pair. The default Vault home is `$HOME/.zumpingd`,
overridable with `ZUMPINGD_HOME`. Never put secrets in the config file or
command line. The service publishes the
catalog for the protected application identified by `zum.issuerURL`; this
includes the public native `zumping` client and its `ping` role grant. The
end-user client does not need to exist before `zumpingd` starts.
Configure the enrolled stable resource audience URI and audience record ID. The
latter is catalog metadata, not a second app ID. Start `zumpingd --config
zumpingd.cf`. Startup idempotently publishes one action, one standard role and
the runtime `ping` scope is derived from that role before opening its listener.
It never creates user assignments. The role name is also the OAuth
resource-scope name used at runtime.

The manifest-created `zumping` client is recognized by the application. Enroll
additional OAuth clients under that protected service application for each
caller type if needed. A mobile, browser, desktop, and CLI client share the same
application issuer and access-token audience, but each has its own `client_id`,
redirect policy, grants, and token family. For this example, enroll a public
native CLI client with authorization-code
and optional refresh grants, the ping role-derived scope, and the loopback redirect
`http://127.0.0.1:8081/callback`. `zumping.cf` already contains the stable
manifest client ID, `zumping`.
Use `zum` to enroll the local user, complete the user's passkey registration,
and assign app assignment with the ping role. Then run:

```sh
./zumping --config zumping.cf
```

The client discovers the configured application issuer, opens its advertised
authorization endpoint in a browser (`--no-browser` prints the URL), receives
the state-bound loopback callback, redeems the code with PKCE directly at
`zumd`, and calls the separate service's GET `/ping` with the access token. The
expected response is
`{"reply":"pong"}`. When a refresh token is issued, it rotates that token
at `zumd`, repeats the resource request, and stores the current credentials
through `Ztls::Vault`. On a later run it refreshes the stored token and calls
the resource without a browser login. The vault is opened only for each
load/save; tokens needed for the active run remain in memory and are cleared
before exit. The default vault home is `$HOME/.zumping`, overridable with
`ZUMPING_HOME`.

Application endpoint admission is live database state, not router
configuration. Enrolling an active application enables its issuer immediately;
suspension closes it, reactivation reopens it, and `Disabled` is the logical
removal state that closes it without a `zumd` restart. Requests already admitted
with an active application snapshot are allowed to finish.

The example listener is loopback HTTP; put a TLS reverse proxy in front of it
for remote deployment. The audience remains a stable HTTPS resource identifier,
independent of listener ports. Authorization-server connections use HTTPS except
explicit loopback development origins. No external OIDC-provider configuration lives here.
For a private issuer CA, set non-secret `caPath` in `zumpingd.cf` to a CA bundle
or native CA directory. Empty/omitted uses system trust; verification stays on.
This configures trust in `zumd`, not an external OIDC provider used by `zumd`.

The persistent HTTP fixture exercises the real programs with a virtual WebAuthn
authenticator: CLI enrollment and assignment of a user to the ping role, direct
login through `zumd`, pong before and after refresh rotation, and repeat login
after service restart. It also covers role removal between code issuance and
redemption and unavailable-server behavior. Real-browser acceptance remains a
separate deployment test.
