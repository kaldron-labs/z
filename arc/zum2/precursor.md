# Application-scoped OAuth precursor for `zumping` and `zumpingd`

## Outcome

Update `zumd`, `libZum`, `zumpingd`, and `zumping` to demonstrate an
Okta-aligned N:1 client-to-service authorization model before `ztchub` uses it:

- Each protected service application has an application-scoped authorization
  server identified by an issuer URI containing the stable application ID.
- Mobile, browser, desktop, CLI, device, and other clients of that service have
  independent OAuth client registrations and `client_id` values under the same
  application.
- `client_id` identifies only the requesting client.  It never identifies the
  protected service application.
- The application-scoped issuer URI selects the service authorization domain.
  Access tokens issued there carry the protected API's configured resource
  identifier in `aud`.
- `zumd` owns authorization, token, revocation, passkey, signing-key, and
  discovery endpoints.  Resource servers do not proxy them.

This is a breaking cleanup.  Do not preserve the existing `zumpingd` OAuth
façade or add compatibility forwarding.

Extend `zrest` with hierarchical request routing.  A top-level catalog-backed
hash lookup selects a child parser from the first path component; the selected
child alone parses or projects the rest of the target.  Exact request paths
within a child continue to use catalog-backed hash dispatch, while deliberately
non-exact paths retain the compile-time prefix matcher.  `zumd` uses this
facility to route `oauth2`, `.well-known`, health, bootstrap, enrollment, and
management path families without sequentially probing independent parsers.

The root router gives the selected child the already root-stripped target view.
The `oauth2` child therefore parses only the typed `/{appID}` component, then
projects the unconsumed suffix into its exact-path request hash and injects the
resolved application context into the selected request.  This is equivalent to
a total skip depth of two without scanning `oauth2` twice.  The `.well-known`
child handles RFC 8414's fixed suffix and trailing issuer path explicitly.
Neither child rescans, copies, reconstructs, or linearly compares the complete
path.

## Terminology and cardinality

Use these terms consistently:

| Concept | Meaning | Cardinality |
|---|---|---|
| Protected service application | Zum's catalog entity for the protected product/API, its policy, roles, scopes, and resource identifier; this is local Zum terminology, not an OAuth client | 1 |
| OAuth client | A particular mobile, browser, desktop, CLI, device, or other caller identified by `client_id` | N per protected service application |
| Authorization server | The issuer/security domain that authenticates principals and grants access to a protected service | 1 per protected service application in this chosen profile, although OAuth does not require that cardinality |
| Resource server/API | The intended recipient and validator of an access token, such as `zumpingd`; multiple process instances may implement the same logical API | 1 logical API in the initial profile |
| Access-token audience | The stable resource identifier carried in an access token's `aud` claim and accepted by that resource server/API | 1 per protected service application in the initial profile |

For one ping protected service application:

```text
ping protected service application
  issuer:                https://auth.kaldron.io/oauth2/APP_ID
  access-token audience: https://zumpingd.kaldron.io
  +-- ping-mobile-client   (client_id=M)
  +-- ping-browser-client  (client_id=B)
  +-- ping-desktop-client  (client_id=D)
  +-- ping-cli-client      (client_id=C; `zumping`)
```

The client registrations share the application authorization domain and the
same access-token audience because they all call the same protected API.  They
remain separate because redirect URIs, origins, grant policy, logout behavior,
and credential handling differ by platform.  A new audience is warranted when
a token is intended for a different resource server/API, not merely because a
different kind of client requested it.

Do not generalize `aud` without naming the token type.  In this plan,
“audience” means the **access-token audience**: RFC 9068 defines it as the
intended resource server/API.  If OIDC ID tokens are issued, their `aud` has a
different meaning: it identifies the relying OAuth client and therefore would
normally contain the requesting mobile, browser, desktop, or CLI `client_id`.
An ID token must never be accepted by `zumpingd` as an access token.

## URI model

Use one public authorization host and put the application ID in the issuer path,
following the shape of an Okta custom authorization server:

```text
authorization base: https://auth.kaldron.io
application ID:     APP_ID
issuer:             https://auth.kaldron.io/oauth2/APP_ID
authorize:          https://auth.kaldron.io/oauth2/APP_ID/v1/authorize
token:              https://auth.kaldron.io/oauth2/APP_ID/v1/token
revoke:             https://auth.kaldron.io/oauth2/APP_ID/v1/revoke
keys:               https://auth.kaldron.io/oauth2/APP_ID/v1/keys
access-token audience:
                    https://zumpingd.kaldron.io
```

The application ID is the 1-side selector.  Every client belonging to that
application uses the same issuer and endpoints but sends its own `client_id`:

```text
GET https://auth.kaldron.io/oauth2/APP_ID/v1/authorize
  ?response_type=code
  &client_id=PING_CLI_CLIENT_ID
  &scope=openid%20ping
  &redirect_uri=http%3A%2F%2F127.0.0.1%3APORT%2Fcallback
  &state=...
  &code_challenge=...
  &code_challenge_method=S256
```

At admission, `zumd` loads `APP_ID` from the full target path first, then looks
up `client_id` within that application.  A client registered under another
application is rejected even if its redirect URI and requested scope happen to
match.  The authorization code, refresh-token family, consent, policy result,
and access token remain bound to both application and client.

The client does not construct endpoint paths.  It is configured with the exact
application issuer and consumes absolute endpoints from discovery metadata.
For an RFC 8414 issuer containing a path, the OAuth metadata URL is:

```text
https://auth.kaldron.io/.well-known/oauth-authorization-server/oauth2/APP_ID
```

The corresponding OIDC discovery URL may follow the Okta form:

```text
https://auth.kaldron.io/oauth2/APP_ID/.well-known/openid-configuration
```

DNS only locates the authorization service.  The URI path selects the
application-scoped issuer.  A subdomain-per-application deployment is possible
but is not the profile implemented here.

Represent the variable URI components as `ZfStruct` fields and map them to path
components with `ZfURI`'s `URI::PathIndex<N>` field property.  In particular,
the application ID in issuer, OAuth endpoint, and discovery paths must be
loaded and saved through this typed mapping.  Do not implement `{appID}` by
manually splitting, slicing, percent-decoding, escaping, or concatenating URI
path strings.  Fixed components such as `oauth2`, `v1`, and `authorize`
remain literals around the mapped application-ID field.

The initial profile does not use RFC 8707 `resource` as the application
selector.  The application issuer selects the authorization domain, its server
configuration determines the access-token `aud`, and scopes select permissions
within that protected API.

References:

- [Okta authorization servers](https://developer.okta.com/docs/concepts/auth-servers/)
  describes custom authorization-server issuer paths, separate signing keys,
  scopes, claims, policies, and resource audiences.
- [Okta authorization code with PKCE](https://developer.okta.com/docs/guides/implement-grant-type/authcodepkce/main/)
  identifies the requesting app integration with `client_id` beneath a selected
  issuer.
- [RFC 6749](https://www.rfc-editor.org/rfc/rfc6749.html) defines OAuth client,
  authorization-server, and resource-server roles.
- [RFC 8414](https://www.rfc-editor.org/rfc/rfc8414.html) defines issuer metadata
  and its path-aware well-known URL construction.
- [RFC 9068](https://www.rfc-editor.org/rfc/rfc9068.html) requires a JWT access
  token's `aud` to identify its intended resource server/API and carries
  `client_id` separately.
- [OpenID Connect Core](https://openid.net/specs/openid-connect-core-1_0.html)
  defines an ID token's `aud` differently: it contains the relying party's
  `client_id`.

## Current implementation to replace

- Application-scoped issuer and endpoint handling is partially implemented but
  its routing remains local to `zumd` rather than a general `Zrest` facility.
- `zumd` divides public and control requests between parsers that are probed in
  sequence.  Its application-scoped parser projects `/oauth2/{appID}` before
  exact suffix dispatch, while RFC 8414 metadata remains a separate non-exact
  control request.  This is application-specific routing rather than a reusable
  hierarchical `Zrest` facility.
- `zumping` has only `serviceURL`; it constructs `/authorize` and `/token` under
  the resource-server origin.
- `zumpingd` implements `/authorize`, `/token`, and `/revoke`, parses or retains
  public-client protocol data, and forwards it through `Zum::Service`.
- `Zum::Service` provides `authorize()`, `token()`, and `revoke()` façade APIs
  backed by privileged `/service/authorize`, `/service/token`, and
  `/service/revoke` operations.
- The ping fixture tests the façade instead of direct use of an
  application-scoped authorization server.

The supporting exact-path `Zrest` catalog dispatch is already present.  Build a
hierarchical router around it:

- Define a small named root catalog whose keys are literal first path
  components and whose values identify child parser types.  Instantiate its
  lookup in `.cc`, following the existing catalog `...Derive`/`...Impl`
  pattern, so root and request type lists do not proliferate through template
  parameters or debug information.
- Select the child parser with one hash lookup of the first component.  Do not
  implement root selection as a succession of `parser.operation()` calls,
  candidate path comparisons, or a `ZuMatcher` over exact roots.
- Retain the selected child as parser state so headers, body, completion,
  reset, and error handling dispatch directly to that same child without
  repeating route lookup.  Compose child header catalogs at compile time and
  use static dispatch; do not introduce virtual polymorphism.
- Return both the selected root ID and a non-owning target view beginning with
  the slash after that root.  The child receives this root-stripped view and
  may project it further for nested dispatch.  `MReqParser` must invoke the
  selected request with the same projected target it matched, not silently
  restore the original full path.  Query offsets must therefore remain
  relative to the request's declared `Path`.
- Keep `MReqParser<Catalog, Skip>` only for prefixes whose components have
  already been selected and require no decoding.  A numeric skip is not a
  substitute for typed extraction of dynamic components such as `{appID}`.
- Having already consumed the literal `oauth2` root for hash selection, load
  `/{appID}` once through a field mapped with `ZfURI::URI::PathIndex<0>` and
  retain the returned suffix span.  Reuse the same typed application-ID field
  definition in the full issuer mapping used for URI generation.  Pass the
  suffix directly to the child request catalog's method/path hash and inject
  the application into the one selected request.
- Route RFC 8414 separately because its well-known URL has the issuer path
  after `/.well-known/oauth-authorization-server`.  After the `.well-known`
  root hash selects that child, match the fixed metadata subpath and load the
  trailing `/oauth2/{appID}` as a typed issuer path without reconstructing it.
- Preserve `ZfURI::scanBody` for form bodies without treating them as URI
  queries.  Path routing must not allocate, copy input bytes, build an
  intermediate URI tree, or perform a second scan.

Keep the useful `Zum::Service` responsibilities: confidential service
authentication, application catalog publication, signing-key discovery/cache,
and access-token audience/action verification for resource requests.

## Implementation plan

### `Zrest` prerequisite: add hierarchical root routing

- Add a reusable root-router parser to `zrest`.  Its root catalog maps each
  literal first path component to one child parser type and exposes a
  catalog-backed hash lookup instantiated in `.cc` through paired
  `...Derive`/`...Impl` macros.
- Split the first path component as part of root lookup and retain a non-owning
  root-stripped target view into the original input.  Validate component
  boundaries so roots such as `oauth2x` cannot match `oauth2`.  Do not scan or
  skip that component again in the child.
- Permit each child to parse additional typed prefix fields and pass a
  projected target to its nested `MReqParser`.  Define the projection contract
  in `Zrest`: matching, exact-length checks, and query parsing all observe the
  same projected path.
- Make child selection and all later parser callbacks static tagged-union
  dispatch.  Merge child request-header catalogs at compile time, retain the
  active child index, and clear it on rejection/reset.
- Detect duplicate literal roots when constructing the root hash.  An unknown
  or malformed root fails without invoking any child parser.
- Add focused `Zrest` tests for root hash selection, component-boundary
  rejection, projected exact and query paths, child callback continuity,
  unknown roots, duplicate roots, and a typed dynamic prefix feeding a nested
  exact-path catalog.

The intended `zumd` composition is:

```text
oauth2     -> typed /{appID} projection -> MReqParser<OAuthCatalog>
.well-known
           -> MReqParser<WellKnownCatalog> over the root-stripped suffix
health     -> MReqParser<HealthCatalog> over the root-stripped suffix
bootstrap  -> MReqParser<BootstrapCatalog> over the root-stripped suffix
enroll     -> MReqParser<EnrollCatalog> over the root-stripped suffix
admin      -> MReqParser<AdminCatalog> over the root-stripped suffix
```

These projections are semantically the proposed per-root skip depths, but the
root component is consumed by the hash router itself.  Child request paths are
declared relative to their root, eliminating redundant `Skip=1`; the `oauth2`
child additionally consumes its typed application component rather than using
a blind `Skip=2`.

### 1. Define application-scoped issuer identity

- Derive each service application's issuer from the configured public
  authorization base URL and its stable application ID using the exact
  `/oauth2/{appID}` profile above.
- Define the application-prefixed issuer and endpoint path shapes as typed
  `ZfStruct` URI views.  Map `appID` with `URI::PathIndex<N>` and use `ZfURI`
  load/save for both parsing incoming paths and constructing canonical issuer,
  endpoint, and metadata URIs.
- Decode the mapped application ID exactly once before lookup.  Do not
  serialize the parsed path back into a temporary string or compare that string
  with the input; accepted path syntax belongs in `ZfURI`.
- Treat the full issuer URI as a security identifier, not a presentation alias.
  Store it in grants, authorization codes, refresh-token families, access-token
  claims, and audit context wherever the current global issuer is stored.
- Give each application authorization server its configured access-token
  audience (the protected API's stable resource identifier), scopes, claims,
  access policies, token lifetimes, and signing-key lifecycle.
  Do not infer any of these from a client record.
- Decide activation ownership explicitly: an inactive/suspended application
  issuer must not authorize, mint, refresh, or revoke tokens except where
  revocation semantics deliberately permit cleanup.

### 2. Add application-prefixed OAuth and discovery endpoints to `zumd`

- Keep the public authorization-server operation types and matched suffixes
  compile-time fixed: `/v1/authorize`, `/v1/token`, `/v1/revoke`, `/v1/keys`,
  and the discovery suffixes.
- Replace sequential public/control parser probing with the `Zrest` root
  router.  Associate `oauth2`, `.well-known`, `health`, `bootstrap`, `enroll`,
  and the management root with their respective child parsers; add other roots
  only where the actual request catalog requires them.
- In the `oauth2` child, accept the target after the root router has consumed
  `/oauth2`, load `/{appID}` once with the typed `ZfURI` mapping, resolve the
  application, project the returned suffix into the application request
  catalog lookup, and inject the resolved context into the selected request.
  Do not use a bare `MReqParser<Catalog, 2>` in place of extracting `{appID}`.
- In the `.well-known` child, handle the RFC 8414 fixed subpath and then load
  its trailing `/oauth2/{appID}` issuer path.  OIDC discovery remains under the
  `oauth2` child because its well-known portion follows the issuer path.
- Do not scan the prefix once to skip it and again to decode it.  Do not parse,
  serialize, and compare paths, and do not allocate or copy path strings during
  dispatch.
- Use the same mapped field definitions across authorize, token, revoke, keys,
  RFC 8414 metadata, and OIDC discovery so admission and generated metadata
  cannot disagree about application identity.  Do not add a second manual path
  parser or repeat decoding or canonical validation.
- Resolve the decoded application ID against the active application catalog on
  every admission, or through a coherently invalidated catalog cache.  Adding
  an active application makes its issuer paths usable without restarting
  `zumd`; suspending or removing it makes those paths fail closed without
  changing the compile-time request set.
- Make catalog publication/removal and operation-admission ordering explicit.
  Once removal or suspension commits, no new authorize, token, refresh,
  discovery, or key operation may be admitted for that issuer.  Define whether
  already admitted operations finish or are cancelled, and ensure cached
  application/key/policy state cannot resurrect the issuer.
- Serve RFC 8414 metadata at the well-known URL derived from the complete issuer
  URI and serve compatible OIDC discovery for clients requesting `openid`.
- Generate absolute endpoint URLs from validated public authorization-base
  configuration and the resolved application ID.  Do not use request `Host`
  headers to mint issuer identities.
- Return an exact issuer in metadata and in `iss`; advertise only grants,
  authentication methods, scopes, claims, and PKCE algorithms supported by that
  application authorization server.
- Keep global administrator/service-management endpoints distinct from
  application-scoped public OAuth endpoints.  Do not overload `client_id` or a
  public scope to select management operations.

### 3. Bind every client to its parent application

- Resolve `{appID}` from the typed issuer path, then resolve `client_id` using
  an application-scoped key or verify that the globally unique client record's
  parent application matches exactly.
- Reject cross-application client IDs before user authentication, consent, or
  token issuance.  Do not fall back to the client record's application when the
  issuer path disagrees.
- Validate redirect URIs, client type, client authentication method, allowed
  grants, scopes, user/group assignment, and application access policy within
  that parent application.
- Bind authorization code redemption to issuer/application, client ID, redirect
  URI, and PKCE verifier.  Bind refresh rotation and revocation to the same
  issuer/application and client registration.
- Mint access-token `aud` from the application authorization server's configured
  protected-API resource identifier.  `client_id`/`cid` identifies the caller
  separately and must never replace or determine the access-token audience at
  the resource server.

### 4. Update `libZum` service configuration

- Separate the authorization-management relationship from the application
  issuer used to validate end-user access tokens.  Require an exact protected
  application `issuerURL`, an exact core `managementIssuerURL` used for the
  catalog publisher's client-credentials grant, and an independent
  `managementURL` administrative resource origin.  The catalog publisher is a
  client of the core management authorization server, not of the protected
  application authorization server.
- Configure `zumpingd` with its application ID or exact application issuer and
  expected access-token audience.  Validate access tokens against exact `iss`,
  `aud`, expiry, signature, and actions.
- Discover the application-specific JWKS URI from issuer metadata and retain the
  existing bounded refresh/cache behavior.  Never accept a key solely because
  it is valid for another application issuer on the same `zumd` host.
- Keep confidential workload credentials out of normal configuration and use
  them only for service authentication and catalog publication.

### 5. Make `zumpingd` a pure resource server

- Remove `Authorize`, `Token`, and `Revoke` request types, parsers, forwarding,
  and request-union entries from `zum/example/zumpingd.cc`.
- Keep `/ping` and readiness.  `/ping` accepts bearer tokens and returns 401 for
  invalid issuer/access-token-audience/token, 403 for missing `ping` action,
  and 200 only for authorized tokens.
- Do not expose application OAuth endpoints on the `zumpingd` listener or
  redirect unauthenticated API calls to a guessed issuer.

### 6. Make `zumping` use the application issuer directly

- Add required `issuerURL` to `zumping.cf`; it is the exact
  `https://auth.kaldron.io/oauth2/{appID}` value.  Retain independent
  `serviceURL`, public `clientID`, scope, callback port, and timeouts.
- Fetch and validate RFC 8414/OIDC metadata for the configured issuer.  Require
  exact issuer equality, bounded/unique JSON, absolute HTTPS endpoint URLs, no
  fragments, authorization code and refresh support, and PKCE `S256`.  Permit
  HTTP only for an explicit loopback test profile.
- Open the discovered authorization endpoint in the system browser and send the
  client's `client_id`, redirect URI, scopes, state, and PKCE challenge.  Do not
  send the service application ID as `client_id`.
- Redeem the code and rotate/revoke refresh tokens at the discovered token and
  revocation endpoints.  Use separate bounded HTTP destinations when endpoint
  origins differ.
- Send the resulting access token only to `zumpingd` in the `Authorization`
  header.  Clear code, verifier, access-token, and refresh-token material on all
  success, failure, and shutdown paths.

### 7. Remove the private OAuth façade

- Remove `Zum::Service` public APIs and data types used only to proxy public
  authorization, token, and revocation transactions.
- Remove `/service/authorize`, `/service/token`, and `/service/revoke`
  endpoints, handlers, bridge structures, and tests from `zumd`.
- Propagate the breaking removal through dependents.  Do not retain deprecated
  methods, forwarding endpoints, or compatibility switches.

### 8. Update tests and documentation

- Add `Zrest` unit coverage for hierarchical root hashing and projected child
  targets independently of `zumd`.
- Add unit tests for application issuer construction, `ZfURI` load/save
  round-trips of every application-prefixed path, `URI::PathIndex<N>` field
  mapping, RFC 8414 path handling, metadata content, exact issuer validation,
  path/app mismatch, client/app mismatch, wrong access-token audience,
  cross-issuer keys, invalid schemes/fragments, duplicate fields, oversized
  metadata, and missing PKCE support.
- Cover missing and extra path components, invalid application IDs, and
  ambiguous routes.  Prove that parsing and generation use the same typed URI
  representation and resolve exactly one application.
- Register multiple client types under one ping application in integration
  tests.  Prove that they use the same application issuer and access-token
  audience but have distinct client IDs, redirect policies, grants, and
  revocable token families.
- Register a second service application on the same `zumd` host.  Prove that its
  issuer path, clients, policies, scopes, signing trust, codes, refresh tokens,
  and access tokens cannot cross into the ping application.
- Add and remove an application while `zumd` remains running.  Prove its typed
  issuer paths become available and unavailable with catalog activation, with
  no parser rebuild or process restart, and that suspension/removal invalidates
  all relevant application-resolution caches.
- Rewrite the real ping flow so passkey authorization, code redemption, refresh
  rotation, and revocation call `zumd` directly.  Assert `/authorize`, `/token`,
  and `/revoke` on `zumpingd` are not found.
- Exercise opaque application IDs and non-default endpoint paths to prove that
  clients consume discovery rather than construct URLs.
- Preserve restart, unavailable-resource, unavailable-`zumd`, role-removal,
  invalid-token, and secret-redaction coverage.
- Update `zum/example/README.md`, `zumping.cf`, and `zumpingd.cf` with the
  application/client cardinality and `auth.kaldron.io/oauth2/{appID}` examples.

## Delivery sequence

1. Add the `Zrest` hierarchical root router, projected-target contract, named
   root catalog macros, and focused tests.
2. Freeze the `ZfURI`/`ZfStruct` issuer-path mappings and application/client
   binding helpers with unit tests.
3. Replace `zumd` parser probing with the root router and add its
   application-scoped OAuth and discovery children.
4. Update token, refresh, revocation, key, and `libZum` issuer/access-token-
   audience handling.
5. Move `zumping` to issuer discovery and direct OAuth calls.
6. Remove OAuth proxy endpoints from `zumpingd` and convert the ping fixture.
7. Delete the private façade APIs and endpoints and rebuild all dependents.
8. Complete isolation, restart, failure, and documentation tests.

## Acceptance criteria

- `Zrest` selects a root child with one catalog-backed hash lookup, and `zumd`
  performs no sequential parser probing or linear candidate-path matching.
- The path target matched by a nested `MReqParser` is the same projected target
  its concrete request parses; query offsets and exact matching cannot observe
  different path views.
- One ping protected service application has one application-scoped issuer and
  one access-token audience with at least two independently registered clients.
- Every ping client uses the same issuer-prefixed authorization endpoint but its
  own `client_id`; `client_id` is never interpreted as the service ID.
- A second service application on the same authorization host has a distinct
  issuer path and access-token audience, and all cross-application attempts
  fail closed.
- Adding, suspending, reactivating, or removing an application changes runtime
  admission for its issuer paths without restarting `zumd` or changing the
  compile-time OAuth handler set.
- `zumping` discovers endpoints from its exact application issuer, completes
  PKCE directly with `zumd`, rotates/revokes its refresh token, and calls the
  independent `zumpingd` resource origin.
- `zumpingd` serves no authorization, token, or revocation façade and accepts
  only access tokens with its exact issuer, access-token audience, validity,
  and `ping` action.
- Endpoint placement is driven by validated metadata, not hard-coded DNS or path
  concatenation in the client.
- Every application-prefixed issuer, OAuth endpoint, and discovery path is
  parsed and generated through typed `ZfURI` mappings whose application field
  uses `URI::PathIndex<N>`; there is no parallel manual path codec.
- No secret, authorization code, verifier, access token, or refresh token is
  logged or sent to an unvalidated origin.
- Focused unit and integration tests pass and the example documentation explains
  the N:1 client-to-service model without overloading “app” or `client_id`.

## Non-goals

- Sharing a client ID across mobile, browser, desktop, and CLI clients.
- Using `client_id`, `resource`, DNS subdomains, or scopes as a substitute for
  the application-scoped issuer selector.
- Multiple access-token audiences/resource APIs within one application
  authorization server in the initial profile.
- Dynamic client registration, generalized federation, or arbitrary third-party
  authorization servers.
- Retaining the old façade protocol or making `zumpingd` an authorization
  server.
- Turning `zumping` into a general OAuth SDK or production credential manager.
