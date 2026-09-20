# `ztchub_client`

The standalone native client discovers endpoints from the exact application
issuer, completes
browser/passkey authorization code with PKCE, then connects to `ztchub` over
WSS. It receives attributed telemetry, unsubscribes, rotates its refresh token,
repeats the subscription, and revokes the refresh token on exit. Tokens stay in
memory and never appear in URLs. The executable does not link `libZum`.

Create an OAuth configuration:

```text
issuerURL: "https://auth.example/oauth2/42",
clientID: "registered-public-client",
scope: "Client offline_access",
callbackPort: 8081,
loginTimeout: 180
```

Register `http://127.0.0.1:8081/callback` on the public OAuth client. Run:

```sh
./ztchub_client --config=client.cf --device-id=SERVICE_PRINCIPAL_ID \
  --wss=wss://ztchub.example/ztc
```

`--no-browser` prints the authorization URL for an external browser or the
automated fixture. `--ca` supplies WSS trust; `caPath` in the OAuth configuration
supplies authorization-server trust.

The user needs the `Client` role (`Request` action); the device service account
needs the `Agent` role (`Telemetry` action). Device ID, principal ID, and the
agent's service-account ID are identical. Requests and responses share the
existing `Ztc.fbs.Msg` contract; there is no separate front-end envelope or
protocol versioning.

The OAuth adapter and protocol session are separate within the example so
future clients can replace browser interaction and authentication carriers
while reusing the supported contract. The process fixture exercises this
executable independently of its protocol fixture clients.
