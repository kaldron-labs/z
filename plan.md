# Authenticated ping/pong interoperability plan

## Scope and current-state findings

- Upgrade the two maintained implementations and their build/test support:
  `zrest/example/zrest.cc`, `zrest/example/zrestd.cc`,
  `zrest/interop/go/client.go`, and `zrest/interop/go/server.go`.
- The checked-in Go code currently has `POST /api/auth` and
  `GET /api/protected`, but issues one 24-hour JWT and has no refresh flow.
  Therefore the first implementation step is to establish the access/refresh
  contract described below on the Go side, then implement the same contract in
  the C++ example.
- Keep this as example/fixture functionality, with end-to-end coverage in
  `zrest/itest`. Do not put application-specific credentials, JWT policy, or
  ping/pong types into the generic `Zrest` library.
- Extend the generic `Zrest` framing API with `BodyPolicy::Zero`: unlike
  `BodyPolicy::None`, it emits no body callback or bytes but declares
  `Content-Length: 0`. Implement it symmetrically in request/response builders
  and parsers, add focused library tests, and migrate every dependent that
  manually declares an empty content length to the policy so no dependent
  retains special `content-length: 0` handling.

## Prescribed source layout

- `zrest/interop/go/server.go`: Go auth, refresh, and ping server.
- `zrest/interop/go/client.go`: sequential Go auth/refresh/ping client.
- `zrest/interop/go/go.mod` and `go.sum`: pinned Go dependencies; delete the
  mutating `make init` workflow.
- `zrest/example/zrestproto.hh` and `zrestproto.cc`: C++ wire objects, Zf
  metadata, endpoint paths, duration parsing, and shared defaults; keep the
  declarations/metadata in the header and non-template implementation in the
  source.
- `zrest/example/zrestjwt.hh` and `zrestjwt.cc`: C++ HS256 issue/validate code.
  Build these with the protocol source as an example-local convenience library
  linked by both programs;
  do not make the JWT implementation header-only or export it from `zrest/src`.
- `zrest/example/zrest.cc`: C++ client state machine.
- `zrest/example/zrestd.cc`: C++ server dispatch and response construction.
- Keep the obsolete `zrest/example/zrclient.cc` and `zrserver.cc` absent, and
  ensure no build or distribution list refers to either historical WIP file.
- `zrest/test/ZrestExampleTest.cc`: isolated TAP tests for the C++ example
  protocol, duration, and JWT utilities.
- `zrest/itest/zrestmatrix.cc`: TAP process driver for the four end-to-end
  pairings and negative cases.
- `zrest/itest/zrestprobe.go`: stdlib-only HTTPS probe used by the matrix for
  authentication failures and wrong-token-type requests that neither example
  client exposes.
- `zrest/itest/Makefile.am`: builds the TAP driver and Go probe fixture and
  exposes `make test`.
- `zrest/test/README.md` and `zrest/itest/README.md`: concise entry-point,
  prerequisite, and direct-run documentation for the new unit and integration
  tests.
- Give each new C++ file the repository modelines, copyright/license header,
  idempotent include guard where applicable, and only its ordered direct
  dependencies. Use hard tabs with the local two-column logical indentation;
  keep all example declarations at top level, use file-scope `static` for
  implementation-only functions, and do not use anonymous namespaces,
  concepts, `requires`, scoped enums, or STL substitutes for existing Z types.
- While restructuring the two example sources, correct their nearby primitive
  and alias deviations: express integral/boolean compile-time constants that
  fit in `int` as enums, use plain `unsigned` for option counts/timeouts that do
  not require a fixed wire/storage width, and use `ZuDerive` for complex
  `ZmList`/`ZmPQueue`/`ZmPQTx` aliases. Preserve explicit `uint64_t` only for
  transport/logical IDs and epoch/deadline arithmetic that genuinely requires
  64 bits.

## Canonical wire contract

Define one contract before changing either implementation, and use it without
compatibility aliases:

| Operation | Request | Successful response | Authentication |
| --- | --- | --- | --- |
| Initial auth | `POST /api/auth`, JSON `{"username":"test","password":"test123"}` | `200`, JSON `{"access_token":"...","refresh_token":"...","expires_in":N}` | None |
| Refresh | `POST /api/refresh`, JSON `{"refresh_token":"..."}` | `200`, the same token-pair JSON shape | Refresh token in the body |
| Ping | `GET /?ping=true` | `200`, JSON `{"pong":true}` | `Authorization: Bearer <access-token>` |

Contract details:

- The username/password shown in the table are defaults. Server `--user` and
  `--pass` define the accepted pair; clients send their corresponding options.
- Send `Content-Type: application/json` for every JSON body and response.
- Use exact, case-sensitive JSON field names shown above. `expires_in` is the
  access-token lifetime in whole seconds, measured from issuance.
- JSON and URI producers emit the canonical field names and shapes in the
  table. C++ uses the normal `ZfJSON`/`ZfURI` transformations directly; Go uses
  `encoding/json` and `net/url`. Empty or missing credentials and refresh
  tokens fail authentication with 401. Clients validate the application values
  required for progress: non-empty tokens, positive `expires_in`, and
  `pong == true`.
- The canonical ping target is `/?ping=true`, generated and consumed through
  each implementation's URI-query facility. Auth and refresh carry JSON bodies;
  ping has no body.
- Sign JWTs with HS256 and the same configurable test secret in both servers.
  Include `sub` (username), `iat`, `nbf`, `exp`, `jti`, and a `token_type` claim
  whose value is either `access` or `refresh`. Generate a fresh 128-bit random
  `jti` for every token (`crypto/rand` in Go, `Ztls::Random` in C++) so a refresh
  always returns newly distinguishable tokens even within the same second.
  Producers encode it on the wire as exactly 32 lowercase hexadecimal
  characters.
- Access tokens are deliberately short-lived; refresh tokens live longer.
  Both Go and C++ servers expose the same `--access-token-lifetime` and
  `--refresh-token-lifetime` duration options. Their common duration grammar is
  an unsigned decimal integer followed by exactly one `s`, `m`, or `h` suffix;
  reject overflow, fractions, compound durations, missing suffixes, and zero.
  Use practical defaults for manual operation, accept `1s` for fast refresh
  tests, require refresh lifetime > access lifetime, and return the selected
  access lifetime through `expires_in`.
- A server must accept a ping only when the header has the exact Bearer scheme,
  the signature and registered time claims are valid, and `token_type` is
  `access`. A refresh endpoint must likewise reject anything except a valid
  `refresh` token.
- Require exactly one Authorization field whose value is exactly `Bearer`, one
  ASCII space, and the non-empty JWT, with no leading/trailing whitespace or
  second credential. Multiple Authorization fields are malformed and return
  401.
- Return `401` with no body for invalid credentials, missing/malformed Bearer
  headers, invalid/expired/wrong-type tokens, or an invalid refresh token.
  Parsing/routing failures use the native HTTP/Zrest error behavior; the clients
  require only the documented success and 401 responses and never depend on an
  implementation-specific error string.
- Request bodies are bounded by the common 1 MiB `ReqBodyMax`. Oversize and
  transport/framing failures are outside the application status contract and
  may return the stack's protocol error or close/reset the request.
- Successful response bodies are bounded by the common 1 MiB `RespBodyMax` on
  both clients. A response exceeding it is a terminal peer/protocol failure;
  never allocate or read an unbounded diagnostic/error body.
- A successful refresh returns a newly generated pair. The examples need not keep a
  server-side revocation store, so the old refresh token remains valid until
  its `exp`; a refresh token must never authorize ping and an access token must
  never authorize refresh.
- Preserve the existing ping and pong payloads and the root ping URI so the
  existing C++/C++ behavior becomes the authenticated subset of the new API.
- The client origin input (Go `--url`; the C++ client's existing positional
  `[URL]`) denotes only the origin: require `http` or `https`, a host and optional
  port, no userinfo/query/fragment, and an empty or `/` path. Resolve all three
  fixed endpoint paths against that origin rather than concatenating arbitrary
  user-provided paths.

## 1. Add ping/pong and refresh behavior to the Go programs

### Go server

- Define `Credentials`, `RefreshRequest`, `TokenResponse`, `Ping`, `Pong`, and
  `Claims` with the exact JSON tags from the canonical contract. Delete
  `Response`, `handleProtected`, and `/api/protected`. Make `expires_in` an
  `int64`; keep `jti` as the JWT library's string claim but encode the 16 random
  bytes with lowercase `encoding/hex`.
- Parse these flags in `main`: `--addr` (default `:8443`), `--cert` (default
  `server.crt`), `--key` (default `server.key`), `--jwt-secret` (default
  `your_secret_key`), `--user` (default `test`), `--pass` (default `test123`),
  `--access-token-lifetime` (default `5m`),
  `--refresh-token-lifetime` (default `24h`), `--requests` (default `0`, meaning
  serve until signalled), and `--verbose`. Implement a `flag.Value` for the
  common duration grammar and fail startup unless refresh is longer than
  access and user, password, and JWT secret are all non-empty.
- Implement `issueToken(subject, tokenType string, lifetime time.Duration,
  now time.Time) (string, error)` and `issueTokenPair(subject string)
  (TokenResponse, error)`. Set all JWT times from one `now :=
  time.Now().UTC()`, encode numeric dates at whole-second precision, generate
  `jti` with `crypto/rand`, set JWT header `typ` to `JWT`, and set `expires_in`
  to the configured access lifetime divided by `time.Second`. Enforce the same
  `JWTPartMax`/`JWTMax` bounds on generated tokens as on parsed tokens. Reject
  any duration/time addition that cannot be represented by the JWT numeric-date
  or Go `time.Time` operations.
- Implement `parseToken(raw, requiredType string) (*Claims, error)` with
  `jwt.ParseWithClaims`, `jwt.WithValidMethods([]string{"HS256"})`, required
  expiration, strict base64url decoding, and the configured HMAC key. Before
  parsing, require exactly three non-empty unpadded segments of at most 4 KiB
  each. Require header `alg == HS256` and `typ == JWT`. Reject an empty subject, missing
  `iat`/`nbf`/`exp`/`jti`, a `jti` other than exactly 32 lowercase hexadecimal
  characters, wrong `token_type`, invalid signature, non-positive registered
  times, `iat >= exp`, `nbf >= exp`, `iat` or `nbf` in the future, or an expired
  token. Do not trust the token's algorithm when selecting the key.
- Register exactly `POST /api/auth`, `POST /api/refresh`, and `GET /`. Let the
  router handle path/method mismatches. Decode
  `r.URL.RawQuery` with `url.ParseQuery` into `Ping` and require `ping == true`;
  do not hand-parse `RequestURI`.
- Decode auth/refresh bodies directly with `encoding/json`, bounded by
  `http.MaxBytesReader` using the same 1 MiB `ReqBodyMax` as `zrestd`.
  `handleAuth` compares the resulting binary values against
  the configured demo credentials, and returns `TokenResponse`.
  `handleRefresh` validates the body token as `refresh` and issues a new pair
  for its subject. `handlePing` accepts only an exact two-part
  `Authorization: Bearer <JWT>` header (checking `Header.Values`, not only
  `Header.Get`), validates type `access`, and returns `Pong{Pong: true}`.
  Map token-generation/random failures to 500;
  they are server failures, not authentication failures. After writing that
  500, trigger graceful shutdown and return a nonzero process status.
- Centralize JSON output in `writeJSON`: marshal once into a bounded byte slice,
  set `Content-Type` and exact `Content-Length` before status, then write the
  complete fixed body. Check/log marshal and short-write failures without ever
  logging a token or Authorization value. This guarantees the fixed framing
  required by the C++ REST response parsers instead of relying on Go's
  automatic chunking threshold.
- Count only successfully written pongs. When a nonzero `--requests` count is
  reached, signal `main` through a bounded channel and use `Server.Shutdown` so
  the final response is flushed before exit. Print final auth/refresh/pong
  counters without secrets; auth failures do not increment successful auth.
  Under `--verbose`, emit stable `event=auth`, `event=refresh`, and `event=pong`
  lines only after successful operations so the matrix can verify ordering.
  Handle SIGINT/SIGTERM through the same graceful shutdown path.
  Load the key pair, create the TCP listener explicitly, emit the stable
  non-secret `event=listening` line only after bind succeeds, wrap it with
  `tls.NewListener`, and call `Server.Serve`; this gives supervision a portable
  readiness event rather than a timed probe.
  Use `atomic.Uint64` for handler counters and `sync.Once` when closing/signaling
  the shutdown channel; `net/http` handlers may execute concurrently.
  Treat `http.ErrServerClosed` as successful shutdown and report every other
  listener/server error as fatal.

### Go client

- Delete `ProtectedResponse`, the single `Token` field, and `GetProtectedData`.
  Define the same three auth structs plus `Ping` and `Pong`; `Client` stores
  `AccessToken`, `RefreshToken`, and `AccessExpiresAt` in addition to its URL and
  HTTP client.
- Parse `--url` (default `https://localhost:8443`), `--cert`, `--user`, `--pass`,
  `--requests` (default `1`), `--interval` (default `0s`), and `--timeout`
  (default `10s`). Use the common duration grammar for interval/timeout, with
  the explicit exception that `0s` is valid for interval. Reject non-positive
  request counts and timeout. When loading the supplied CA, require
  `AppendCertsFromPEM` to succeed; retain hostname verification and never offer
  an insecure test-only bypass.
- Add one internal `doJSON(method, path, requestBody, responseBody, bearer)`
  function. It marshals the optional body, constructs the request, sets JSON and
  Bearer headers, performs it, always closes the body, bounds diagnostic body
  reads, and returns the status separately from transport/JSON errors. For a
  successful response, enforce `RespBodyMax` before/while decoding directly
  into the response struct and reject a second JSON value.
- `Authenticate` posts credentials, requires `200`, and calls `installTokens`.
  `installTokens` rejects empty tokens, either token exceeding `JWTMax`, or
  `expires_in <= 0`, stores both tokens,
  and computes `AccessExpiresAt` from the local receipt time only after checking
  the addition for overflow.
- `Refresh` posts `RefreshRequest{RefreshToken: c.RefreshToken}`, requires
  `200`, and atomically replaces both stored tokens only after the complete
  response validates. It never falls back to username/password.
- `Ping` first refreshes when `time.Now()` is not before `AccessExpiresAt`, then
  constructs `Ping{Ping: true}`, encodes the canonical query, and sends the GET
  with `Bearer ` plus the access token. On `401`, call `Refresh` and replay once;
  a replayed `401` is terminal. Require `200` and `pong == true`.
- `main` authenticates once, then performs exactly `--requests` successful
  logical pings, sleeping `--interval` between them (not after the last). This
  sequential behavior is intentional; concurrent refresh coalescing is tested
  by the C++ client. Remove the raw request dump and never print tokens.

## 2. Implement the same protocol in `zrest/example`

### Shared example protocol/JWT support

- Keep the example protocol declarations at top level, matching the existing
  `zrest` and `zrestd` sources; do not introduce a `ZrestEx` namespace that
  suggests these example types belong to the `Zrest` library. In
  `zrestproto.hh`, define `CredString` and `TokenString` with separately named
  `ZtStringHeapID`s and an example-local allocation base backed by named
  `ZmVHeap<"zrest.Object">`. Expose size-aware `operator new` and matching
  sized/unsized `operator delete` on that base, following the existing
  `ClientVHeap_`/`AlertEvent` idiom. Then define `Credentials`, `RefreshRequest`,
  `TokenResponse`, `Ping`, and `Pong`. `Credentials` owns `CredString` fields;
  `RefreshRequest` and `TokenResponse` own `TokenString` fields. These are the
  application structs into which `ZfJSON`/`ZfURI` load directly; do not retain
  wire text or introduce an intermediate parsed-body representation. Use normal
  camelCase C++ field/accessor names
  (`accessToken`, `refreshToken`, `expiresIn`, and `tokenType`) and declare each
  snake-case wire name as a facet property in the `ZfStruct` metadata:

  ```cpp
  ZfStruct((RefreshRequest, JSON),
    (((refreshToken), (JSON::ID<"refresh_token">, Required)), (String)));

  ZfStruct((TokenResponse, JSON),
    (((accessToken), (JSON::ID<"access_token">, Required)), (String)),
    (((refreshToken), (JSON::ID<"refresh_token">, Required)), (String)),
    (((expiresIn), (JSON::ID<"expires_in">, Required)), (UInt64)));
  ```

  Here the metadata `String` is the Zf field type; the C++ members retain their
  declared `CredString` or `TokenString` storage. Use `URI::ID<...>` by the same
  mechanism whenever a URI field's C++ and wire names differ; no alias is
  needed for `ping`. Do not create snake-case C++ accessors or perform ad hoc
  name translation in parsers/builders. Use `uint64_t` for `expiresIn` and
  `bool` for ping/pong. Default `Ping::ping` and `Pong::pong` to false; builders
  explicitly construct the successful `true` values, so a missing query/body
  field cannot be mistaken for success.
  Mark every canonical wire field in `Credentials`, `RefreshRequest`,
  `TokenResponse`, `Ping`, `Pong`, `JWTHeader`, and `JWTClaims` with the generic
  `Required` property; requiredness belongs to the schema metadata, not a
  separately maintained parser table.
- Derive the five REST payload objects from the allocation base and `ZmObject`,
  because
  `Zrest::ReqBuilder`, `ReqParser`, `ResBuilder`, and `ResParser` retain them in
  `ZmRef` and allocate them with `new Object`. The variable-size heap base is
  intentional because client request envelopes derive from these payloads and
  have different concrete sizes; it provides named telemetry without an unsafe
  fixed-size base allocator. Keep `ReqBuilder`/`ResBuilder` queue nodes on their
  existing named fixed-size `ZmHeap` container paths. Define the plain binary
  value `JWTHeader` and `JWTClaims` structs
  privately in `zrestjwt.cc` and transform them directly with `ZfJSON` as well.
  Declare the signed-`int8_t` `TokenType` `ZtEnum` in `zrestjwt.hh`, because it
  is part of `jwtValidate`'s interface, and put its implementation map in
  `zrestjwt.cc`. Keep the signed-`int8_t` `alg` and `typ` enums private to that
  source. Give all three an `Invalid = -1` sentinel that is the binary default;
  define the valid `HS256`, `JWT`, `access`, and `refresh` values and apply
  `JSON::ID<"token_type">` to `tokenType`. Use
  `CredString` for `sub`, `uint128_t` for `jti`, and signed `int64_t` Unix
  seconds for `iat`/`nbf`/`exp`. Give `jti` the Zf JSON number-as-string format
  with lowercase hexadecimal and width 32, so the binary field remains numeric
  while the wire claim is the canonical string:

  ```cpp
  (((jti),
    (JSON::String<ZuFmt::Hex<false, ZuFmt::Right<32>>>, Required)), (UInt128))
  ```

  Reject a missing or zero `jti` and regenerate on the vanishingly unlikely
  random zero during issuance. Only after all checks pass, move the validated
  subject into the caller's `CredString` with `ZuMv`. Define fieldless
  `Unauthorized` and `InternalError` objects from the same allocation base and
  `ZmObject` solely as distinct callback-dispatch tags: `ReqBuilder::process`
  dispatches on response object type. Give these tags no Zf metadata. The
  corresponding response builders/parsers explicitly override `Body` with
  `Zrest::BodyPolicy::Zero`, which emits no body bytes and adds
  `Content-Length: 0`; never serialize the tags and do not define an `Empty`
  wire payload. Reserve `BodyPolicy::None` for messages intrinsically lacking
  body framing.
- Use the existing `Zrest::ReqParser`/`ResParser` body paths and
  `ZfJSON`/`ZfURI` handlers directly for REST payloads. Do not add a parallel
  loader, parsed-body representation, presence mask, or validity state to the
  application objects. The `Required` metadata documents the schema and is
  available to reflection; application acceptance checks only the values it
  consumes.
- Fix formatted integral loading at its Zf root instead of compensating in the
  JWT code. `ZfFieldScanInt` must pass the field's declared `Fmt` to the normal
  integer scanner, so `JSON::String<Fmt>` and URI number fields load with the
  same format they use when saving; retain the explicit legacy `Hex` property
  branch. Audit the independent `ZfCf` and `ZfCLI` integer loaders and likewise
  preserve their declared formats. Add focused JSON, URI, configuration, and
  CLI round-trip/load regressions using non-decimal hexadecimal values.
- Define named integral bounds in `zrestproto.hh` with an unscoped enum:
  `JWTPartMax = 4U<<10` (a deliberate defensive ceiling far above the roughly
  200-byte example claims), `JWTScratchSize = 512` for the expected hot-path
  initial scratch capacity,
  `JWTMax = (JWTPartMax * 3) + 2`, `ReqBodyMax = 1U<<20`, and
  `RespBodyMax = 1U<<20`, matching the existing retained-body policies. Apply
  hard limits before allocation and after generation; do not use the hard
  ceilings as stack capacities. Retain the Zhttp default 1 KiB `HdrBufSize`,
  whose `ZtScratch` storage has heap fallback; do not inflate every hot-path
  header scratch buffer to the exceptional security ceiling. Reject an
  Authorization value longer than `Bearer ` plus `JWTMax` in the keyed header
  callback.
- Put the defaults (`test`, `test123`, `your_secret_key`, `5m`, `24h`) and path
  types (`/api/auth`, `/api/refresh`, `/`) in `zrestproto.hh` so client and
  server cannot drift. Declare
  `bool parseDuration(ZuCSpan, bool allowZero, uint64_t &seconds)` in the header
  and define it in `zrestproto.cc`; leave `seconds` unchanged on failure. It
  implements the common grammar, and callers compare access and refresh
  lifetimes only after both parse successfully.
- Give `zrestjwt.hh` these two top-level functions with explicit prefixes:

  ```cpp
  bool jwtIssuePair(
    Ztls::Random &, ZuCSpan secret, ZuCSpan subject, int64_t now,
    uint64_t accessSecs, uint64_t refreshSecs, TokenResponse &);
  bool jwtValidate(
    ZuCSpan secret, ZuCSpan token, TokenType::T requiredType,
    int64_t now, CredString &subject);
  ```

  Pass `now` explicitly so tests contain no sleeps and server issuance uses one
  timestamp per pair. Build both tokens in temporaries and assign the output
  `TokenResponse` only after the complete pair succeeds; assign the validated
  subject only after every JWT check succeeds. Move subject and completed
  `TokenString` storage with `ZuMv`; do not copy through intermediate
  contiguous strings.
  Reject `now + accessSecs` or `now + refreshSecs` on signed `int64_t` overflow
  before constructing either claims object.
- In `zrestjwt.cc`, define the canonical unpadded base64url encoding of the
  invariant `{"alg":"HS256","typ":"JWT"}` header as a named compile-time
  `ZuString`; do not serialize and encode the same header for every token.
  Serialize only `JWTClaims` with `ZfJSON` and encode the payload with
  `ZuBase64URL` directly into each candidate `TokenString`, after the header
  constant and first separator. Sign that existing contiguous prefix with
  `Ztls::HMAC<Ztls::SHA256>`, then append the separator and encoded signature;
  size/reserve the final token once and write encoded output into uninitialized
  destination storage where the container API permits; do not build and re-copy
  a separate signing string. Fill a mutable byte span over each `uint128_t`
  `jti` directly with the caller-owned `Ztls::Random`; do not allocate and copy
  through a separate 16-byte array. Zf emits it as the canonical 32-character
  lowercase hex string. Fail issuance if any generated segment or the complete
  token exceeds the shared bounds.
- Validation must require exactly three non-empty segments and canonical
  unpadded base64url, cap each encoded segment and decoded JSON object at 4 KiB
  before allocating, and require the decoded signature length to equal
  `Ztls::HMAC<>::Size`. Use named `ZtScratch` storage with an initial
  `JWTScratchSize` capacity; let it extend through its named heap as needed up to
  the already-checked `JWTPartMax` bound. Parse both binary objects directly
  with `ZfJSON`, require
  `alg == HS256` and `typ == JWT`, recompute the MAC into
  `ZuBArray<Ztls::HMAC<>::Size>` (a protocol-defined fixed
  capacity), and compare it with the `Ztls_memcmp` backend abstraction rather
  than depending directly on OpenSSL or adding a hand-rolled comparator. Require
  non-empty `sub`, nonzero `jti`, `token_type == requiredType`, positive
  `iat`/`nbf`/`exp`, `iat <= now`, `nbf <= now`, `iat < exp`, `nbf < exp`, and
  `now < exp`. The `JSON::String` field format performs the direct string-to-
  `uint128_t` conversion; do not retain or separately parse the source claim.
  Reject integer overflow and trailing JSON.
- Build `zrestproto.cc` and `zrestjwt.cc` into `libzrestexample.la` in
  `zrest/example/Makefile.am`; list `zrestproto.hh` and `zrestjwt.hh` in
  `noinst_HEADERS`, and link both `zrest` and `zrestd` against the convenience
  library before the existing Z libraries.
- Test the example utilities in `zrest/test/ZrestExampleTest.cc` with
  `ZuTestUtil`: first cover the accepted duration suffixes, zero policy,
  overflow, fractions, compound/missing suffixes, and Zf JSON/URI field-name
  mappings. Then validate a fixed Go-generated HS256 vector, plus C++
  issue/validate round trip, unique
  `jti`, signature corruption, extra/missing segments, non-canonical or
  malformed base64, malformed/trailing JSON, wrong algorithm/type, access used
  as refresh, refresh used as access, expired, future `nbf`/`iat`, and boundary
  `now == exp`.

### C++ server (`zrestd`)

- Extend `Options` with `ZuCSpan` fields `user`, `pass`, `jwtSecret`,
  `accessTokenLifetime`, and `refreshTokenLifetime`, a `verbose` boolean, and the
  corresponding long CLI names.
  Parse the lifetimes once in `loadOptions` into whole-second numeric fields;
  defaults and non-empty credential/secret validation must match the Go server.
  Add every new option and its default/grammar to `usage()`.
- Give `App` a `Ztls::Random` member, initialize it once before listening, and
  fail startup if the cryptographic backend or random source cannot initialize.
  Pass it explicitly to `jwtIssuePair`; do not introduce hidden global or
  singleton JWT state. Keep it Rx-owned and call it only from the server's Rx
  request-completion path.
- Define `AuthParser` (`POST /api/auth`, JSON `Credentials`), `RefreshParser`
  (`POST /api/refresh`, JSON `RefreshRequest`), and `PingParser` (`GET /`, URI
  query `Ping`).
  `PingParser::Headers` is `ZhttpHeaders("authorization")`; its keyed
  header callback rejects an over-limit value before copying an accepted value
  into parser-owned `TokenString` storage. Reject duplicate Authorization
  values as malformed authentication state. Otherwise retain the normal
  `Zrest::ReqParser` operation/body handling: `ZfURI` loads `Ping`, and
  `ZfJSON` loads `Credentials`/`RefreshRequest` directly.
- Define separate response types per request so the `Zrest` response union has
  no duplicate alternatives: a JSON 200 and bodyless 401 for each endpoint,
  plus request-specific bodyless 500 builders for auth/refresh issuance failure.
  Use `TokenResponse` for auth/refresh 200, `Pong` for ping 200,
  `Unauthorized` for 401, and `InternalError` for 500. In every 401/500 response
  builder, override `Body` with `Zrest::BodyPolicy::Zero`; that policy
  suppresses body bytes and adds zero-length framing. Declare only those legal
  responses in each parser's
  `Responses` list.
- In `App::respond`, dispatch on the active parser alternative. Auth compares
  the parsed credentials and calls `jwtIssuePair`; refresh validates type
  `refresh` and issues a pair for the returned `sub`; ping parses exactly
  `Bearer <token>`, validates type `access`, requires `Ping::ping`, and emits
  `Pong{true}`. A JWT issuance failure emits the request-specific 500,
  increments `errors`, and signals shutdown so the response is drained and the
  server exits nonzero; client-controlled auth failures produce 401 without
  doing so.
- Keep request objects and Authorization/token strings on the Rx thread through
  validation. Construct the destination-owned `ResBuilder` and response object
  before `link->send`; capture no parser references in asynchronous work and add
  no locks. Preserve raw parser-to-`App` back-pointers and the declaration/
  teardown order in which the server drains before `App` is destroyed.
- Increment `processed` and evaluate `--requests` immediately after enqueueing
  the authenticated pong with `link->send`. Auth, refresh, and 401
  exchanges do not affect that count. Let the existing `server.stop()` drain
  the accepted final response before teardown. When the configured pong count
  is reached, emit the stable final `event=summary auth=N refresh=N pong=N`
  record from that same Rx callback before signalling main; do not read the
  counters from main merely to format the summary. Reserve `errors` for
  parser/build/transport/internal failures, not expected authentication
  rejections in negative tests.
- Track successful auth and refresh counts separately and include them with the
  pong count in the final non-secret summary consumed by `zrestmatrix`.
  Keep auth/refresh/pong counters as plain Rx-owned `unsigned` values; the
  configured server has one Rx shard, and completion/shutdown is communicated
  to main through the existing semaphore rather than cross-shard counter reads.
  Keep transport/error state on its actual owning shard or communicate a fixed
  failure result explicitly; do not make ownership ambiguous with locks or
  blanket atomics.
  Under `--verbose`, emit the same stable successful-operation event lines as
  the Go server. Increment/log auth and refresh only after their successful 200
  response has been enqueued, matching pong accounting. Every `ZiLOG` lambda
  captures only the required scalar/string values by copy, never pointers or
  references. Emit `event=listening` from the existing `listening` callback
  after the requested transport is active, matching the Go readiness contract.
- Keep the existing transport configuration. The common Go matrix starts
  `zrestd` with `--https --http2=disable --cert=... --key=...`; retain the
  current HTTP/2 and HTTP/3 modes for C++-only manual use. Preserve the explicit
  `.retainedBodyMax(ReqBodyMax)` server configuration.
- Correct the local class-shape deviation while expanding the server: change
  `App` from a struct with mixed interface/implementation visibility to a
  class, keep its data private, `m_`-prefixed, and ordered to avoid padding, and
  expose only the callbacks/types required by `Zhttp::Server`. Keep `Options`,
  wire objects, parsers, and builders as all-public structs with data members
  before functions.

### C++ client (`zrest`)

- Extend `Options` with `user`, `pass`, and a `double interval`; use `test`,
  `test123`, and `0` defaults. Expose the interval as both `-i N` and
  `--interval=N`, where `N` is a finite, non-negative number of seconds and may
  be fractional (`0`, `1`, and `0.01` are valid; suffixes are not). Declare it
  in the `Options` metadata as
  `(((interval), (CLI::Opt<'i'>, CLI::Long<"interval">)), (Float, 0))`.
  Reject NaN/infinity, overflow, and a positive value that rounds to zero at
  `ZuTime` resolution, then convert it once to `ZuTime` during option loading.
  This client-only pacing option does not use the server token-lifetime duration
  grammar. Retain URL/CA/protocol/concurrency controls and use the URL's single
  origin for all endpoints. Document all three options, units, fractional
  syntax, and scheduling semantics in `usage()`.
- Change `Client::workload` to receive and retain the borrowed `ZiMultiplex *`
  alongside `Options`; it remains valid until after `app.stop()`. Use it only to
  arm/cancel the interval timer through the inherited `ZmScheduler` API, and
  clear it during `final` before the multiplex is stopped.
- Define `TokenState` by deriving from `RefreshRequest` and adding only a
  prebuilt Bearer value and access deadline. Its inherited field owns the
  refresh `TokenString`; the Bearer `TokenString` incorporates the access token.
  This preserves one intrusive object/allocation and never embeds a
  `ZmObject`-derived payload as a value. Do not retain duplicate raw
  access/refresh strings once these two request-ready forms are built. The client
  holds `ZmRef<const TokenState>` and replaces the whole reference after auth or
  refresh; it never mutates token strings in place while requests may refer to
  them.
- Define three intrusive request objects carrying a raw `Client *`. Derive
  `AuthReq` from `Credentials` so its payload and callbacks share one allocation,
  with `AuthBuilder::bodyObject()` returning the `Credentials` base; derive
  `PingReq` from `Ping`, with `PingBuilder::queryObject()` returning that base,
  and add its captured `TokenState`, logical ping ID, and one-bit `replayed`
  flag. `RefreshReq` captures `TokenState` and its
  builder overrides `bodyObject()` to return that state's inherited
  `RefreshRequest` base. Do not store any `ZmObject`-derived payload by value.
  Their `process`/`failed`
  callbacks call explicit Tx-thread
  methods on `Client`; they contain no policy themselves. Because response
  completion may run off the Tx shard, each callback first snapshots only the
  fixed metadata or owned token/pong data it needs, then invokes
  `client->txRun(0, ...)` and calls the trailing-underscore state transition
  there. Never capture a
  borrowed response parser/object or link pointer across that post.
- For token responses, consume the Rx-owned `TokenResponse` while still in its
  callback, construct the complete destination-owned `TokenState` there by
  moving/copying each variable-size value exactly once, and post only its
  `ZmRef` handle to Tx. The Tx continuation swaps that handle without accessing
  the response object. For pong, capture only its boolean and logical ID by
  value. A ping-401 callback captures logical ID, replay bit, and its existing
  `TokenState` handle for generation comparison. Never capture an Rx-owned
  payload and dereference it on Tx.
- Give every queued HTTP request a monotonically increasing transport ID used by
  `ReqBuilder_::key()`. Keep it separate from `PingReq::logicalID`; auth,
  refresh, a rejected ping attempt, and its replay must all have distinct
  transport IDs while the replay retains the same logical ID.
- Remove the current `Client::archive -> produce_` workload trigger.
  `Pool::archive_` only retires the transport request; the Tx-thread auth,
  refresh, pong, 401, timer, and failure transitions are the sole producers of
  subsequent logical work.
- Define `AuthBuilder` and `RefreshBuilder` as JSON POSTs to the fixed paths,
  and `PingBuilder` as a GET with `QueryPolicy::URI`, letting `ZfURI` render its
  typed `Ping{true}` object as `/?ping=true`.
  `PingBuilder::Headers` declares runtime `authorization` and emits the
  Bearer value from that `PingReq`'s captured `TokenState`, never from mutable
  client state. `RefreshBuilder` likewise emits the refresh body from its
  captured state. Define distinct response parser types per request and status,
  mirroring the server response lists and avoiding duplicate union alternatives:
  auth and refresh accept typed 200/401/500 alternatives; ping accepts 200/401.
  Non-200 alternatives use `Unauthorized` or `InternalError`; each response
  parser explicitly overrides `Body` with `Zrest::BodyPolicy::Zero` and drives
  the request-specific terminal/retry policy.
- Let the standard response parsers load directly into `TokenResponse` or
  `Pong`. Before installing token state, require non-empty access/refresh tokens
  and positive `expiresIn`; require `Pong::pong` before completing a logical
  ping. Preserve the explicit `.retainedBodyMax(RespBodyMax)` client
  configuration. A transport/parse failure reaches the request's failure
  callback and never installs partial/default state.
- Add an unscoped `ClientState` enum
  (`Idle`, `Authenticating`, `Ready`, `Refreshing`, `Complete`, `Failed`)
  and keep all mutable auth/workload state on the pool Tx thread: current `TokenState`
  reference, generated/completed ping counts, queued logical pings,
  the actual Tx scheduler slot captured from `ZmSelf()->sid()`, the next-ping
  `ZuTime` sentinel, and the interval timer.
  `ClientState::Refreshing` itself is the refresh-in-flight sentinel; do not add
  a parallel boolean. Store pending `{logicalID, replayed}`
  values in an explicitly named Z container/heap rather than an STL container
  or unexplained fixed array. Public `workload` remains a thin `txRun`
  dispatcher and calls a trailing-underscore Tx implementation that asserts its
  shard with `ZiAssert`.
- On startup enqueue only `AuthBuilder`. Its 200 callback validates both tokens
  as non-empty and no longer than `JWTMax`, requires positive `expires_in`,
  builds the Bearer string once, sets the deadline
  from receipt time after a checked addition, enters `Ready`, and queues the
  ping window up to `min(requests, concurrency)`. With interval zero it dispatches
  that window immediately; with a positive interval it arms the first tick and
  leaves the window queued. Auth 401, malformed response, or transport failure
  enters `Failed` and seals the pool.
- Define a prominently documented `WorkBatch = 64` enum bound for client Tx
  work. Define a logical scheduled wave as exactly
  `min(concurrency, requests - generated)` new pings. Accumulate completed HTTP
  capacity until that whole next wave fits; do not turn early individual
  completions into partial scheduled waves. A window refill assigns the wave's
  logical IDs and places fixed-size `{logicalID, replayed}` items in the pending
  queue, but creates or dispatches at most `WorkBatch` items in one scheduler
  turn. If more work in the same wave remains, post a Tx continuation and
  continue that wave without another interval; never loop over arbitrary
  `--requests` or `--jobs` in one I/O turn. Retain the wave's `TokenState`
  snapshot and remaining count across those continuations so every request in
  the wave uses one token generation. Emit the wave event and set the next
  eligible dispatch time only after the whole wave has been issued. Before
  starting a wave, check the access deadline once: send the wave when valid,
  otherwise start one refresh and leave all pending items queued. This makes
  proactive expiry exercise the same refresh-coalescing path as multiple 401
  responses.
- `dispatchPending_` compares `Zm::now().sec()` with the current token state's
  deadline once before starting a logical wave. If expired, it leaves the
  pending queue intact and calls `startRefresh_`; otherwise it snapshots that
  token state for all bounded continuations that issue the wave. A refresh
  request also captures the current `TokenState`. Exactly one may be active.
  Refresh 200 constructs and swaps in a complete new `TokenState`, enters
  `Ready`, and resumes eligible pending pings through the same scheduled,
  bounded path; refresh 401/failure is terminal and must not retry credentials.
- On pong 200 require `pong == true`, complete that logical ID exactly once,
  and produce the next ping. On ping 401, requeue that same logical ID with
  `replayed = true` as pending metadata (not the stale `PingReq`) and
  compare the request's captured `TokenState` with the client's current state.
  If they are identical, start/coalesce refresh; if they differ, a refresh has
  already replaced that rejected generation, so dispatch the pending replay on
  the current state without another refresh. A replay is another transport
  attempt for the same logical ping: dispatch it as soon as the replacement
  token is available, without consuming another interval tick or emitting
  another ping-wave event. Dispatch constructs the replay request with that
  current state. A 401 when `replayed` is already true is terminal.
  Auth/refresh attempts and the rejected HTTP attempt never increment logical
  generated/completed counts.
- Centralize terminal failure in idempotent `fail_`: enter `Failed`, increment
  one logical failure result, cancel the interval timer, and prevent further
  dispatch immediately. Discard unsent pending work in `WorkBatch`-bounded Tx
  continuations and release any partial-wave snapshot; do not destruct an
  arbitrarily large queue in one I/O turn. Seal the pool immediately to prevent
  new transport submission, but let `idle()` acknowledge terminal failure only
  after bounded cleanup is complete, the timer is cancelled, and every active
  request has retired. Later callbacks from already-active transport requests
  only retire/archive their request and cannot mutate counts or restart
  refresh/work generation.
- Implement `-i`/`--interval` with one `ZmScheduler::Timer` owned by `Client`
  and armed by calling `m_mx->add(&m_timer, Zm::now() + m_interval,
  ZmScheduler::Update, ..., m_txSID)`. Set `m_txSID` from `ZmSelf()->sid()` in
  `workload_`, which is already executing on the pool's actual Tx shard; do not
  assume that it is always the multiplex's default `txThread()`. The callback
  therefore runs on the same Tx thread as the client state machine and calls
  the Tx-only trailing-underscore transition directly; do not repost or use a
  separate pacing thread. After a tick dispatches a wave, the client schedules
  itself again with another `ZmScheduler::add` only when more logical work
  remains.
  Store the next eligible dispatch time as the Tx-owned `ZuTime` sentinel and
  base it on the time the preceding whole wave finished dispatching, so
  scheduler or response delay can lengthen, but never shorten, the requested
  gap. If a tick finds insufficient HTTP capacity for the complete next wave,
  leave that deadline due; the completion that makes the full wave fit
  dispatches it immediately and advances the deadline. Otherwise completions
  only ensure the timer is armed for the existing deadline and never defer it.
  Cancel/drain the timer before
  `stop`/`final`; never sleep a scheduler or I/O thread. With interval zero,
  retain the current window-filling behavior. With a nonzero interval, the Tx
  thread sends at most one concurrency-sized ping wave per timer tick, including
  delaying the first wave by one interval. Completions accumulate capacity for
  the next tick and never move an already-armed deadline. Thus the nominal send duration is
  `ceil(requests / jobs) * interval`: `-j2 -n20 -i1` takes 10 seconds to issue
  all waves, while `-j2 -n20 -i0.01` takes 0.1 seconds. The interval applies
  between successive waves on this client Tx thread; do not independently
  sleep or pace each request in a wave.
- Seal only after all configured logical pings have received pong and no refresh
  or timer remains, then enter `Complete`. Report success from logical
  completed/failed counts rather than the base HTTP attempt count, because auth,
  refresh, and replay add transport attempts. Never include credentials, JWTs,
  or Authorization in verbose/error logs.
  Under `--verbose`, emit `event=pong id=N` only when logical ping `N` completes
  and `event=ping-wave tick=N count=M time_ns=T` when a scheduled wave is fully
  issued, where `T` is the dispatch-time `Zm::now()` value in nanoseconds.
  Include the same scheduler-clock timestamp on the client `event=auth` record,
  plus non-secret refresh events and a final logical-count summary.
- Make `Client::idle()` post the process semaphore only in `Complete` or
  fully cleaned-up `Failed`; transient HTTP idleness while authentication,
  refresh, bounded failure cleanup, or an interval timer is pending must not end
  the process.
- Add a main-thread `quiesce()` entry point implementing the required timer
  teardown sequence: post to Tx to prevent rearming and cancel the timer, post
  a second continuation on that same Tx shard to drain any late callback, then
  acknowledge the main thread with `ZmBlock`. Only the main
  thread waits; no scheduler/I/O thread blocks. Invoke it before every
  `app.stop()` path, including SIGINT and timeout, so the timer can never outlive
  the HTTP client or borrowed multiplex pointer.
- Correct the corresponding local deviations in `zrest.cc`: change `Client`
  and `Pool` from structs with private data to classes. Keep their private data
  `m_`-prefixed at the bottom and group/order the Tx-owned members to avoid
  padding. Keep request objects, builders, parsers, `TokenState`, and pending
  queue nodes as all-public structs with their data before functions.
- Preserve raw `Client *` back-pointers in the short-lived request objects; do
  not add owner reference-count churn or cycles. `Client`/`Pool` teardown must
  cancel the timer, drain/archive every active request, clear the pending queue,
  release the token snapshot, and assert the dependent inventory is empty
  before owner destruction.

## 3. Build an automated interoperability matrix in `zrest/itest`

- Extend `zrest/test/Makefile.am` with `ZrestExampleTest`, the direct
  `zrest/example` include path, and `zrest/example/zrestproto.cc` plus
  `zrest/example/zrestjwt.cc` as sources of that isolated test binary; do not
  link `test` against a later-directory
  convenience library. Keep its basename CamelCase as required for unit tests
  and include it in the existing TAP `test` target. Distribute the test README.
- Add `zrest/itest/Makefile.am` using the include/link ordering from the example
  Makefile plus the direct `zrest/example`, `zhttp/test`, and `zquic/test`
  include paths required by its sources. Build the lowercase integration driver
  `zrestmatrix` and `zrestprobe.go` fixture, and run `zrestmatrix` with
  `prove -j1`. Distribute the integration README and probe source. Add
  `zrest/itest/Makefile` to `AC_CONFIG_FILES`.
- Set `zrest/Makefile.am` to the guideline traversal order
  `SUBDIRS = src test itest example interop`. The recursive `all` completes all
  directories before the module `test` target invokes `make -C test test` and
  `make -C itest test`, so the example and peer executables exist before the
  matrix runs without making `itest` link against a later directory. Make the
  module target explicitly `test: all`; do not add an `interop` test target.
- Update `zrest/interop/go/Makefile` so normal `all` builds `client` and `server`
  from the checked-in `go.mod`/`go.sum`, add a `clean` target for only those two
  generated binaries, and remove `init`. The parent
  `zrest/interop/Makefile.am` remains build/distribution wiring for those peer
  binaries, not a test runner: add `all-local` invoking the Go `all` target,
  `clean-local` invoking its clean target, and distribute the Go sources,
  certificate config, Makefile, `go.mod`, and `go.sum`.
- Detect the Go tool in `configure.ac` and expose it through the generated
  makefiles and an Automake conditional instead of assuming an unconfigured
  external executable. Guard `interop`'s `all-local`/`clean-local` Go recipes
  with that conditional. If Go is absent, normal C++ builds still succeed and
  the TAP matrix reports a clear skip; the full four-way acceptance run
  requires Go and the checked-in module metadata/dependencies. Make targets may
  perform normal pinned Go module resolution through the configured
  cache/proxy, but must never run `go get`, `go mod init`, or `go mod tidy`, or
  otherwise mutate `go.mod`/`go.sum`.
- Base `zrestmatrix.cc` process supervision on the existing `zhttp/itest`
  drivers: create a private temporary directory, generate one localhost
  certificate/key, reserve an isolated loopback port, capture stdout/stderr per
  child, impose a named 15-second case deadline, and terminate and reap every child
  on all exits/signals. Wait in the test's main thread for the server's
  `event=listening` record on its captured output pipe; do not poll TCP or send
  a readiness request. Reuse the Zhttp/Zquic
  test utilities and Z containers rather than adding STL/process abstractions;
  guard any unavoidable platform-specific process operation with `_WIN32` and
  provide equivalent cleanup semantics.
- Exercise the four required pairings over HTTPS/HTTP/1.1:

  1. C++ `zrest` client -> Go server.
  2. Go client -> C++ `zrestd` server.
  3. C++ `zrest` client -> C++ `zrestd` server.
  4. Go client -> Go server.

- Pass `--http3=disable --http2=disable --ca=<cert>` to the C++ client,
  `--https --http2=disable --cert=<cert> --key=<key>` to `zrestd`, and the
  corresponding `--cert`/`--key` or `--cert` CA options to the Go programs.
  Bind `zrestd` with `--addr=127.0.0.1 --port=<reserved-port>` and the Go server
  with `--addr=127.0.0.1:<reserved-port>`. Use the same `localhost` certificate
  and `https://localhost:<reserved-port>` URL in all four cases; do not disable
  certificate or hostname verification.
- Run each pairing with server `--requests=2`,
  `--refresh-token-lifetime=1h`, `--jwt-secret=matrix-secret`, and `--verbose`.
  For Go-client pairings use `--access-token-lifetime=1s` on the server and
  `--requests=2 --interval=2s` on the client; the Go client sends its first ping
  immediately. For C++-client pairings use `--access-token-lifetime=3s` on the
  server and `--requests=2 --interval=2 --verbose` on the client; `zrest` sends
  at the 2- and 4-second ticks, crossing expiry only for the second wave.
  Require exit status zero and the server event sequence
  `auth, pong, refresh, pong`; require final server counters
  `auth=1 refresh=1 pong=2`. This verifies refresh ordering without inspecting
  JWT values.
- Run an additional C++ concurrency case against each server with
  `--requests=8 --jobs=4 --links=4 --link-max=1 --interval=2`: four
  pings on the first tick, then four logical pings crossing the expiry boundary
  on the second. Start the server with a three-second access-token lifetime,
  the same refresh lifetime/secret/verbose flags, and `--requests=8`.
  Require counters `auth=1 refresh=1 pong=8`, eight unique client pong IDs, and
  zero client/server exit status.
- Run a focused C++ fractional-pacing case against one server with a long-lived
  access token and `--requests=20 --jobs=2 --links=2
  --link-max=1 -i 0.01 --verbose`. Require ten ordered
  `event=ping-wave` records with `count=2`, 20 unique completed pong IDs, and
  successful client/server exit. Start that server with `--requests=20`, the
  shared matrix secret, a five-minute access-token lifetime, a one-hour refresh-
  token lifetime, and verbose events. Compare the `time_ns` values emitted by
  `zrest` itself and require the tenth wave not to appear earlier than 90 ms
  after its successful auth event; retain the ordinary case deadline as the
  upper liveness bound. This checks the nominal 0.1-second schedule while
  allowing normal timer jitter and avoids treating output-pipe delivery time as
  dispatch time. Process status and exact counts determine completion—the
  elapsed-time assertion tests pacing only.
- Have `zrestprobe` authenticate normally and then exercise, against each
  server, missing/duplicate Authorization, raw token without `Bearer`, malformed
  Bearer whitespace, a one-byte-mutated signature, refresh token used for ping,
  access token used for refresh, bad/empty credentials, empty refresh token,
  and an access token after its one-second expiry. Assert 401 for these
  authentication failures; do not turn the interoperability suite into a
  separate parser/router conformance specification.
- When probing `zrestd`, independently verify both returned JWT signatures and
  claims in `zrestprobe` with the configured shared secret before using them.
  This supplies the Go-validation-of-C++-issuance direction; the fixed Go token
  in `ZrestExampleTest` supplies C++ validation of Go issuance.
- Capture client/server logs per case and print them only on failure. Apply a
  hard per-case timeout so a lifecycle bug cannot hang `make test`. The timed
  waits exercise token-expiry and interval semantics; they are not used to
  infer asynchronous completion, which is determined by child status/events
  and guarded by the deadline.

## 4. Build and acceptance sequence

1. Reconfigure through `./z.config -c` with the existing prefix/options after
   adding `zrest/itest/Makefile.am`, then build the full tree with `make -j8`;
   do not build the example against a stale `zrest/src` library.
2. Run `go test client.go`, `go test server.go`, and `make` from
   `zrest/interop/go`; the two files are intentionally separate `main` programs
   in one directory and therefore are not compiled together as one package.
3. Run `make -C zrest/test -j8` before `make -C zrest/test test`, then run
   `make -C zrest/itest -j8` before `make -C zrest/itest test`, so both the
   isolated utility tests and integration fixtures are built before execution.
4. Run top-level `make test` and verify clean shutdown: no orphaned server
   processes, incomplete requests, leaked response bodies, or logged
   credentials/tokens.
5. Repeat acceptance with the repository-supported clang ASan/LSan
   configuration. Reconfigure through `z.config`, then run a top-level
   `make clean`, `make -j8`, and the test suite so build types are never mixed;
   require a warning-free build and no sanitizer findings.
6. Run the C++ TAP binaries under valgrind with `libtool exec` (never invoke a
   `.libs` binary directly), including leak checking. Preserve the matrix's
   deterministic child cleanup and report any child failure/output through TAP.

Acceptance requires all four clients/servers to use the exact same wire
contract, every ping to reject absent/invalid access authentication, every
client to obtain credentials before its first ping and refresh without losing
or double-counting logical pings, both servers to honor a configured one-second
access-token lifetime, and all four forced-expiry matrix cases to finish
successfully. It also requires both concurrent-expiry cases to perform exactly
one refresh, the fractional C++ pacing case to issue exactly ten two-ping waves
on its Tx-thread scheduler, both negative suites to return the prescribed
statuses, and the Go probe/C++ JWT vector tests to validate issuance in both
directions.
