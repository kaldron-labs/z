# Implemented OAuth 2 baseline for the `zrest` examples

## Status and outcome

`zrestd`, `zrest`, and their Go interoperability fixtures now implement a
compact OAuth 2 demonstration.  This is the working baseline for the
application-scoped issuer changes described in `precursor.md`.

The example has three logical roles:

- `zrestd` is the authorization server for the demonstration application.
- `zrestd` also hosts the `ping` resource server, with authorization-server and
  resource-server protocol handling kept separate.
- `zrest` is a public native client.  It never receives or handles the resource
  owner's password.

The private username/password-to-JWT exchange and its refresh endpoint are
gone.  Authorization uses the authorization-code grant with PKCE `S256`, an
external user agent, issuer discovery, refresh-token rotation, and revocation.

## Standards profile

The implemented profile uses:

- [RFC 6749](https://www.rfc-editor.org/rfc/rfc6749.html) for OAuth roles,
  authorization codes, token requests, refresh tokens, scopes, and errors.
- [RFC 7636](https://www.rfc-editor.org/rfc/rfc7636.html) for mandatory PKCE
  `S256` on authorization-code transactions.
- [RFC 8414](https://www.rfc-editor.org/rfc/rfc8414.html) for authorization
  server metadata and path-aware discovery.
- [RFC 9207](https://www.rfc-editor.org/rfc/rfc9207.html) for issuer identity in
  authorization responses.
- [RFC 6750](https://www.rfc-editor.org/rfc/rfc6750.html) for bearer-token use
  and `WWW-Authenticate` failures.
- [RFC 7009](https://www.rfc-editor.org/rfc/rfc7009.html) for token revocation.
- [RFC 8252](https://www.rfc-editor.org/rfc/rfc8252.html) for an external user
  agent and ephemeral loopback redirect URI.
- [RFC 9700](https://www.rfc-editor.org/rfc/rfc9700.html) for the OAuth 2.0
  security best current practice.

This is OAuth 2, not OpenID Connect.  It has no ID token, `openid` scope,
UserInfo endpoint, federation, dynamic client registration, PAR, DPoP, device
grant, implicit grant, or password grant.

## Application and endpoint model

The demonstration's primary protected service application has two
independently registered public clients.  A second application/client pair is
registered solely to exercise issuer and application isolation:

```text
ping service application / issuer / audience
  +-- zrest native client       (client_id=zrest-native)
  +-- Go native client          (client_id=zrest-go)

other isolation fixture / issuer
  +-- isolation client          (client_id=zrest-other)
```

`client_id` identifies a caller, not the protected service.  The application ID
in the issuer path selects the authorization domain, whose configuration
determines the access-token audience.  The profile does not use an OAuth
`resource` parameter as the application selector.

For a configured HTTPS authority, the URI shapes are:

```text
application ID:      ping
issuer:              https://localhost:PORT/oauth2/ping
metadata:            https://localhost:PORT/.well-known/
                       oauth-authorization-server/oauth2/ping
authorize:           https://localhost:PORT/oauth2/ping/v1/authorize
token:               https://localhost:PORT/oauth2/ping/v1/token
revoke:              https://localhost:PORT/oauth2/ping/v1/revoke
keys:                https://localhost:PORT/oauth2/ping/v1/keys
resource:            https://localhost:PORT/api/ping
```

The client is configured with the exact issuer and resource URL.  It derives
the RFC 8414 metadata URL from the complete issuer, requires exact issuer
equality, and consumes the advertised absolute endpoints.  It does not append
endpoint names itself.

Application-prefixed paths are represented by `ZfStruct` fields mapped with
`ZfURI::URI::PathIndex<N>`.  Parsing and generation use the same typed form;
there is no parallel manual component splitting, percent decoding, escaping,
or concatenation.  Comparisons put the existing `ZuSpan` on the left so the
framework's equivalent `char`/`uint8_t` span comparison is used directly.

## `Zrest` dispatch design

`Zrest::MReqParser` now has this form:

```cpp
template <typename Catalog_, unsigned Skip_ = 0>
struct MReqParser;
```

The named catalog contains the request list, merged request/response header
catalogs, and out-of-line method/path and response-status matchers. `Skip_` is
a compile-time count of complete leading path components ignored by that
catalog matcher. It is fixed for every request in that `MReqParser` type.

- `Skip_ == 0` is the unchanged, zero-overhead default.  The complete path is
  matched against the fixed request paths exactly as before.
- With `Skip_ != 0`, dispatch makes a local span, validates and skips exactly
  that many leading components, and matches the remaining suffix.
- Dispatch never mutates the caller's `Zhttp::Target`.
- The selected request receives a target whose path is the same projected span
  used for matching and exact/query validation. It never silently receives the
  original full path after its catalog matched a suffix.
- The request universe, method sets, suffixes, headers, and `ZuUnion`
  alternatives remain compile-time fixed.

`zrestd` uses `MReqRouter` to hash the `oauth2`, `.well-known`, and `api`
roots.  The `oauth2` child loads the application component once through a
typed `ZfURI` path field and passes the remaining `/v1/...` suffix to its exact
request catalog.  The RFC 8414 child loads its fixed suffix and trailing
application component directly; the resource child receives `/ping`.  It does
not linearly probe parsers or rescan a complete application path.

No `Zhttp::Parser` contract was changed.  `Zrest` also uses `ZfURI::scanBody`
for form bodies so body form decoding is distinct from URI-query decoding.

HTTP/3 represents an absent request or response body as streamed framing with
no DATA.  `Zrest` therefore accepts unknown-length framing for
`BodyPolicy::None` and `BodyPolicy::Zero`; their `body()` still rejects any
actual DATA.  JSON, URI-form, and raw retained bodies remain fixed-length and
bounded.  The example completes its terminal lifecycle when the terminal
response transmission completes; it does not wait for connection teardown or
the HTTP/3 drain period.

## Source layout

The C++ example is deliberately limited to:

- `zrest/example/zrest.hh`: the shared wire contract, typed paths, bounded wire
  values, OAuth structures, and metadata.
- `zrest/example/zrest.cc`: all client configuration, adapters, transport
  state, callback handling, OAuth workflow, request scheduling, and lifecycle.
- `zrest/example/zrestd.cc`: all server configuration, adapters, application
  state, signing and verification, endpoint operations, and lifecycle.

There are no client-only or server-only implementation headers and no shared
example JWT library.  OAuth access and refresh tokens are opaque to clients;
ES256 issuance, JWKS publication, and resource-side verification are owned by
the server implementation.

## Authority and origin handling

The example keeps authority handling within its existing parser wrapper:

- An absolute-form request target reuses the authority already parsed by
  `Zhttp`; a `Host` field is ignored for target selection.
- An origin-form request retains at most one compile-time-dispatched `host`
  field and parses it once with `Zhttp::parseAuthority`.
- Issuer and audience construction use normalized parsed authority data, not a
  comparison of raw authority strings.
- The scheme comes from the actual link transport.  If an absolute-form target
  also supplied a scheme, a scheme inconsistent with that transport is
  rejected.  The server has no trusted-gateway mode and does not derive a
  secure public origin from forwarding headers.
- Negative conformance that would reject an otherwise unambiguous, usable
  message is not added merely to police protocol form.  Ambiguous or unusable
  authority input still fails admission.

This follows the project's Postel-law posture while avoiding repeated parsing,
repeated validation, and parsing data that `Zhttp` has already made available.

## OAuth lifecycle

The authorization server provides:

- RFC 8414 metadata with the exact issuer and absolute endpoint URLs.
- Distinct `zrest-native` and `zrest-go` public-client registrations beneath
  the same `ping` application.
- An authorization-server-owned browser login and secure session cookie.
- Exact native loopback redirect validation, state and issuer binding, and
  single-use authorization codes.
- PKCE verifier validation and code binding to issuer, client, redirect URI,
  subject, scope, and expiry.
- ES256 access tokens containing issuer, audience, subject, client, scope,
  issued-at, expiry, and token identity, with public keys exposed as JWKS.
- Opaque refresh tokens stored by digest, rotated on use, with family replay
  detection and family revocation.
- Idempotent token revocation and immediate resource rejection of revoked
  access tokens.

Sessions, codes, refresh tokens, refresh families, and access-token state are
bounded.  Each map has an ordered expiry index.  Request processing removes
expired entries from the minimum of those indexes before using or admitting
state; terminal sessions and codes are removed immediately.  Expiry therefore
does not scan containers, run in the background, or depend on a garbage-
collection timer.

The `/api/ping` resource operation verifies signature, exact issuer, exact
audience, validity, and `ping` scope.  Authentication failures return a bearer
401 response; an authenticated token without the required scope returns 403.

The state and signing key are intentionally in memory.  Restart invalidates
browser sessions, codes, refresh families, and keys.  This remains an
educational authorization server, not a deployable identity service.

## Native-client workflow

The C++ and Go clients:

1. Derive and fetch metadata from the configured exact issuer.
2. Validate issuer equality, bounded metadata, endpoint schemes/origins,
   authorization-code and refresh support, and PKCE `S256`.
3. Start an ephemeral loopback callback, generate state and a fresh PKCE pair,
   and invoke a separate external user-agent process.
4. Validate callback state and issuer, redeem the code, and retain opaque token
   values without logging them.
5. Run the concurrent or paced bearer-protected ping workload.
6. Refresh after expiry when required and replay eligible work at most once.
7. Revoke the current refresh token during orderly completion and clear secret
   material on terminal paths.

The automated user-agent fixture is a distinct process and sends the
demonstration password only to the authorization server's browser interaction.
It does not recreate a password grant inside the OAuth client.

## Verification baseline

The completed work is covered by:

- `ZfURI` tests for body scanning and typed URI behavior.
- `Zrest` tests for zero-skip dispatch, multi-component skipping, malformed and
  incomplete prefixes, hierarchical root hashing, projected child-target
  delivery, exact fixed-path behavior, and zero-body HTTP/3 framing.
- C++ example tests for OAuth field mapping, typed paths, wire structures, and
  the RFC 7636 PKCE test vector.
- A twelve-case C++/Go interoperability matrix covering both client/server
  directions, concurrency, fractional pacing, expiry-triggered refresh,
  negative OAuth and bearer cases, revocation, and orderly shutdown.
- Forced HTTP/2 and HTTP/3 cases in that automated matrix, without
  transport-specific process termination.
- Raw HTTP/1.1 authority cases, two active application registrations with
  cross-application grant/token rejection, and a limit-one expiry case proving
  deterministic reclamation of sessions, codes, refresh tokens, families, and
  access-token state.

The retained build is the existing clang debug configuration.  This work does
not require reconfiguration or changes to the `Zhttp` parser interface.

## Non-goals

- Compatibility aliases for the removed private protocol or old source layout.
- A production authorization server, persistent identity store, consent
  service, credential manager, or general OAuth SDK.
- OIDC, federation, MFA/passkeys, dynamic client registration, client
  credentials, device authorization, implicit flow, password grant, arbitrary
  grant extensions, or multiple resource audiences per issuer.
- Durable browser sessions, codes, refresh families, or signing keys across
  restart.
- Using `client_id`, scope, DNS hostname, or a client-supplied `resource` value
  in place of the application-scoped issuer path.
