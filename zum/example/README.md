# Ping example

`zumpingd` is a service using installed `libZum`, not the provider or a database
client. `zumping` is a pure REST/OAuth native client with no Zum library linkage.
Both `zumd` and `zumpingd` must be running for browser login and code redemption.

Enroll a native-service application using the administrative `zum` client. Use
its issued confidential client ID as `zum.clientID` in `zumpingd.cf`; configure
the provider origin as `zum.issuerURL`, and inject the client secret as
`ZUM_CLIENT_SECRET` using the deployment secret manager. Never put the secret in
the config file or command line. Configure the enrolled stable resource audience
URI and audience record ID. The latter is catalog metadata, not a second app ID.
Start `zumpingd --config zumpingd.cf`. Startup authenticates to the configured
issuer and idempotently publishes one action, one standard role and one scope,
all named `ping`, before opening its listener. It never creates user assignments.

Enroll a separate public native client under that app with authorization-code
and optional refresh grants, the ping audience/scope, and the loopback redirect
`http://127.0.0.1:8081/callback`. Put its issued public client ID in `zumping.cf`.
Use `zum` to enroll the local user, complete the user's passkey registration,
and assign app membership with the ping role. Then run:

```sh
./zumping --config zumping.cf
```

The client opens a browser (`--no-browser` prints the authorization URL), receives
the state-bound loopback callback, redeems the code with PKCE through the service,
and calls GET `/ping` with the access token. The expected response is
`{"reply":"pong"}`. When a refresh token is issued, it also rotates that token
through the service and repeats the request. Tokens remain in memory.

The example listener is loopback HTTP; put a TLS reverse proxy in front of it
for remote deployment. The audience remains a stable HTTPS resource identifier,
independent of listener ports. Provider connections use HTTPS except explicit
loopback development origins. No upstream-provider configuration lives here.
For a private issuer CA, set non-secret `caPath` in `zumpingd.cf` to a CA bundle
or native CA directory. Empty/omitted uses system trust; verification stays on.
This configures trust in `zumd`, not the upstream provider delegated by `zumd`.

Both programs build. The PostgreSQL HTTP fixture has run the real programs with
a virtual WebAuthn authenticator: CLI enrollment and assignment of user to the
ping role, login through the service, pong before/after refresh rotation, and
repeat login after service restart all pass. A role removed between code issuance
and redemption is rejected, and restoring it permits a fresh login. The client
also fails without a pong when either server is unavailable and succeeds after
`zumd` returns. Real-browser acceptance remains outstanding.
