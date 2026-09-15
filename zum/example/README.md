# Ping example

`zumpingd` is a service using installed `libZum`, not the provider or a database
client. `zumping` is a pure REST/OAuth native client with no Zum library linkage.
Both `zumd` and `zumpingd` must be running for browser login and code redemption.

Enroll a catalog-client application using the administrative `zum` client. Use
its issued confidential client ID as `zum.clientID` in `zumpingd.cf`. Configure
the protected application's exact issuer (`.../oauth2/APP_ID`) as
`zum.issuerURL`, the core management issuer as `zum.managementIssuerURL`, and
the independent administrative resource origin as `zum.managementURL`. Inject
the client secret as `ZUM_CLIENT_SECRET` and the SSF callback Authorization value
as `ZUM_SSF_CALLBACK_AUTH` using the deployment secret manager;
never put it in the config file or command line. The catalog publisher is a
management OAuth client. It authenticates at the management issuer but publishes
the catalog for the protected application identified by `zum.issuerURL`.
Configure the enrolled stable resource audience URI and audience record ID. The
latter is catalog metadata, not a second app ID. Start `zumpingd --config
zumpingd.cf`. Startup idempotently publishes one action, one standard role and
the runtime `ping` scope is derived from that role before opening its listener.
It never creates user assignments. The role name is also the OAuth
resource-scope name used at runtime.

Enroll separate OAuth clients under that protected service application for
each caller type. A mobile, browser, desktop, and CLI client share the same
application issuer and access-token audience, but each has its own `client_id`,
redirect policy, grants, and token family. For this example, enroll a public
native CLI client with authorization-code
and optional refresh grants, the ping role-derived scope, and the loopback redirect
`http://127.0.0.1:8081/callback`. Put its issued public client ID in `zumping.cf`.
Use `zum` to enroll the local user, complete the user's passkey registration,
and assign app membership with the ping role. Then run:

```sh
./zumping --config zumping.cf
```

The client discovers the configured application issuer, opens its advertised
authorization endpoint in a browser (`--no-browser` prints the URL), receives
the state-bound loopback callback, redeems the code with PKCE directly at
`zumd`, and calls the separate service's GET `/ping` with the access token. The
expected response is
`{"reply":"pong"}`. When a refresh token is issued, it also rotates that token
at `zumd`, repeats the resource request, and revokes the final refresh token.
Tokens remain in memory and secret values are cleared before exit.

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
