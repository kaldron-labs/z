# `zum` application IAM plan

## Goal

`Zum` encapsulates user and client authentication and authorization for one
host application or service. It remains an embedded OAuth authorization server:
the host can mount `/authorize`, `/token`, authorization-server metadata, and
JWKS with little glue. It supports two ways to authenticate a user during
`/authorize`:

1. a built-in WebAuthn passkey; or
2. an optional upstream OIDC provider such as Okta.

Both paths resolve a Zum user and roles, pass through the same scope-to-role
authorization, and issue Zum authorization codes and tokens. OIDC delegates
authentication to the provider; it does not make Zum an application catalog,
directory synchronizer, or general federation broker.

This plan retains all ten tables currently exposed by `ZumDB.hh`. Simplification
targets duplicated control flow, misplaced HTTP handling, speculative features,
and friction in the host API. This follows the project requirement that code
must align with its concrete goal and dead or historical code must be deleted
([GUIDELINES: Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

## Deliberate limits

Implement only the existing restricted OAuth profile plus upstream OIDC login:

- local authorization code with mandatory PKCE `S256`;
- local `client_credentials` for workload clients;
- local rotating refresh tokens;
- built-in passkey or upstream OIDC authentication at `/authorize`;
- asymmetric RFC 9068 access tokens, OAuth metadata, and JWKS; and
- typed administration needed to populate the ten tables.

Do not add local browser sessions, local groups, service-account records, an
application table, claim tables, generic claim expressions, SAML, SCIM,
introspection, token exchange, mTLS, a service mesh, dynamic client registration,
OIDC consent storage, or an HTTP management API. None is required to satisfy the
goal. A future concrete consumer can justify a separate change. This limit is
required by [GUIDELINES: General principles](GUIDELINES.md#general-principles)
and [GUIDELINES: Scope and cleanup](GUIDELINES.md#scope-and-cleanup).

Zum does not need a local browser session for SSO. On every local `/authorize`,
it redirects to the configured upstream provider. The provider's browser session
provides SSO and commonly returns without another prompt. Built-in authentication
performs a passkey assertion. This deletes the proposed Session table/kind,
cookie lifecycle, CSRF logout endpoint, and session garbage collection.

## Authorization model

Keep the existing model:

```text
built-in passkey -> User.roleIDs ---------------------------\
                                                            -> principal roles
upstream OIDC -> configured role source and role mapping ---/

principal roles -> Role.actions ----------------------------\
                                                            -> effective actions
requested scopes -> Scope.roleIDs -> Role.actions ----------/

effective actions = principal actions intersect scope actions
```

For `client_credentials`, `Client.roleIDs` supplies the principal roles and the
same scope-to-role calculation applies. A scope never assigns a durable role.
It limits the authority delegated to one token. Validate that the client may
request every scope and that all selected scopes have one audience before role
expansion. Code redemption and refresh also intersect current authority with
the authority ceiling stored in `Grant`; subsequent role expansion must not
enlarge an existing refresh family.

Use the existing `effectiveActions()` semantics: expand both role sets to action
bitmaps, then intersect. Do not add a separate Permission record or maintain a
second scope-to-action mapping. Resource ownership and business predicates stay
in the host resource handler. This keeps the hot check to bitmap operations and
follows [GUIDELINES: Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

Upstream OIDC has a separate scope vocabulary. `openid profile email groups`
asks the provider to release identity claims; those strings are never looked up
in the local `Scope` table. Name the fields `oidcScopes` and `scopes` in distinct
request/configuration types so the two cannot be confused.

OIDC authorization has exactly two configured modes:

- `LocalRoles`: after `(issuer, sub)` resolves a Zum user, use `User.roleIDs`;
- `MappedRoles`: read one configured claim and map its values to local `RoleID`s.

There is no implicit union mode. In mapped mode, missing, malformed, duplicate,
or unknown values add no role. Multiple known values form a union. The resulting
roles are request-local and are not written into `User.roleIDs`. This supports
both SSO-only applications with local authorization and MISP-style external
role mapping without creating another persistence model.

The issuer configuration also selects exactly one interactive authentication
method, `Passkey` or `OIDC`. Do not add a login-method chooser or run both for
one request. In OIDC mode, the passkey enrollment routes are not mounted. A
later consumer can justify account-level method selection.

## Persistence

Retain these tables and responsibilities:

| Table | Retained responsibility | Governing guideline |
| --- | --- | --- |
| `Issuer` | Local issuer identity, stable action-ID high-water mark, authorization version, and audit-ID high-water mark. | [Persistence](GUIDELINES.md#persistence); no process-local counters for durable IDs. |
| `User` | Human identity, passkey handle, local roles, lifecycle, OIDC subject binding, and authorization version. | [Persistence](GUIDELINES.md#persistence); [disorderly structs](GUIDELINES.md#storage-and-capacity). |
| `Cred` | Independently indexed passkey public material, counter, backup state, label, lifecycle, and user version. | [Persistence](GUIDELINES.md#persistence); avoid an oversized/hot `User` row. |
| `Action` | Persistent action name and stable, never-reused bitmap position. | [Algorithmic efficiency](GUIDELINES.md#algorithm-and-control-flow); bitmap authorization avoids string work. |
| `Role` | Persistent role name and action bitmap. | [Persistence](GUIDELINES.md#persistence); shared mutable relational state belongs in Zdb. |
| `Scope` | Audience-bound local OAuth name and referenced roles. | [Scope and cleanup](GUIDELINES.md#scope-and-cleanup); it is retained because both auth paths use it. |
| `Client` | Local OAuth client, type, credentials, redirects, grants, audiences, scopes, roles, and lifecycle. | [Persistence](GUIDELINES.md#persistence); registrations are durable security state. |
| `Grant` | Passkey ceremonies/capabilities, authorization codes, refresh families, token digests, reuse evidence, and authority ceilings. | [Persistence](GUIDELINES.md#persistence); sagas protect multi-row activation. |
| `SignKey` | Public JWK, protected signer reference, activation, and retirement metadata. | [Persistence](GUIDELINES.md#persistence); rotation must survive restart. |
| `Audit` | Persistent security events with bounded retention. | [Storage and capacity](GUIDELINES.md#storage-and-capacity); indexed bounded deletion, never scans. |

Do not add tables for the upstream provider. Support one optional upstream OIDC
provider per Zum issuer. Its endpoints, client ID, client authentication, scopes,
role mode, claim name, and role map are host configuration. Add `oidcSub` to
`User`, unique within the configured provider. The provider issuer is fixed by
configuration and validated on every login. A second provider would require an
explicit binding table and is outside this change.

Never bind by email. An administrator or an authenticated local user explicitly
sets `oidcSub`; optional just-in-time creation is a host policy and creates a new
user only. This is necessary because OIDC `sub` is the stable provider identity.

Keep one `Grant` table, but stop treating its current 31-field C++ struct as an
ideal layout. Organize immutable common fields first and kind-specific fields
into clearly separated payload groups. Retain the flat record and existing
`ZfbStruct` representation; a discriminated union adds conversion and audit
surface without changing the table. Reorder the C++ storage by alignment and
lifecycle and document fields valid for each `GrantKind`; metadata remains in
schema order. Do not add a private serialization layer. This applies
[GUIDELINES: disorderly structs](GUIDELINES.md#storage-and-capacity) and
[GUIDELINES: Metadata](GUIDELINES.md#metadata).

Use Zdb sagas only where an invariant spans rows, such as initial user plus
credential activation and code-to-refresh-family activation. One-row state
transitions use conditional Zdb commits. This follows
[GUIDELINES: Persistence](GUIDELINES.md#persistence). Expired grants and audits
are removed through their expiry/time indexes in bounded batches initiated by
request activity or explicit maintenance. Never scan a table or run timer-based
GC ([GUIDELINES: Storage and capacity](GUIDELINES.md#storage-and-capacity)).

## Upstream OIDC login

Add `ZumOIDC.hh` and `ZumOIDC.cc`. The adapter performs authorization code with
state, nonce, and PKCE `S256`. It uses configured authorization, token, and JWKS
endpoints; optional discovery runs during initialization, validates the exact
configured issuer, then freezes the endpoint configuration.

The adapter must:

1. create bounded pending state tied to the existing local authorization grant
   and browser binding;
2. redirect to the provider with configured `oidcScopes`;
3. validate state on callback and exchange the code asynchronously;
4. validate ID-token signature, allowed algorithm, `iss`, `aud`, `azp` where
   required, nonce, `iat`, and `exp`;
5. resolve `oidcSub`, check the Zum user state, and select local or mapped roles;
6. run the common local scope/role authorization; and
7. finish the existing Zum authorization grant and redirect with a local code.

Do not accept provider access tokens as Zum tokens and do not copy their expiry,
scopes, or audience into a local token. UserInfo is unnecessary when all required
claims are in the verified ID token; omit it initially. Do not implement refresh
of provider tokens because Zum re-enters upstream authorization for each local
authorization request.

Hold the upstream PKCE verifier only in a bounded pending object and remove it
on callback, failure, cancellation, or timeout. Restart invalidates pending OIDC
login and the local authorization request fails closed. Each object owns one
`ZmScheduler::Timer`; shutdown uses cancel, same-shard drain continuation, then
destruction ([GUIDELINES: Timers](GUIDELINES.md#timers)). The timer is that
request's timeout and removes it directly; do not add an expiry index or a
second cleanup mechanism.

The HTTP adapter creates a random, short-lived transaction cookie, stores only
its digest in the bound grant/pending object, and checks it on passkey completion
and OIDC callback. Set `Secure`, `HttpOnly`, and `SameSite=Lax`; clear it at the
terminal response. This is browser-request binding, not a login session. Zum
owns its creation and validation so hosts cannot accidentally use a constant
such as the current `zumd` example binding.

Cache the provider's current bounded JWKS in one `ZmHash` keyed by `kid`, using
a named `ZmHeapID`. Replace the set after a successful refresh; do not add LRU,
per-key expiry, or a generic HTTP cache. Refresh asynchronously on an unknown
`kid` and coalesce concurrent refreshes; cap work per scheduler turn.
This follows [GUIDELINES: Containers and intrusion](GUIDELINES.md#containers-and-intrusion),
[GUIDELINES: Heap allocation](GUIDELINES.md#heap-allocation), and
[GUIDELINES: Liveness](GUIDELINES.md#liveness).

## Library-owned HTTP integration

Zum must own every security-sensitive step currently implemented by `zumd`.
In particular, move its private `FinishReq`, `id`/`flow` parsing, WebAuthn input
selection, ceremony dispatch, continuation into `authorizeFinish()`, OAuth error
selection, and response formatting into `ZumHTTP`/`ZumPasskey`. Derive ceremony
purpose from server-side `Grant`; the browser must not select enrollment versus
authorization by supplying `flow`.

Retain the existing composable Zrest request types and complete the missing
`PasskeyFinishReq`. Add one small CRTP adapter, `Zum::HTTP<App>`, which implements
the standard handlers by forwarding to a non-template `Zum::Server`. The host
derives from it, supplies rendering, enrollment/admission policy, signer access,
clock/configuration, and app resource routes. It does not parse authentication
messages or reproduce callback chains.

Keep nondependent state and implementation in `Server` and `.cc` files; request
templates contain only Zrest type adaptation. This directly applies
[GUIDELINES: Static polymorphism](GUIDELINES.md#static-polymorphism-and-constraints)
and the prohibition on nondependent declarations nested in templates under
[GUIDELINES: Code structure](GUIDELINES.md#code-structure).

The retained routes are:

| Route | Owner | Governing guideline |
| --- | --- | --- |
| `GET /authorize` | Zum validates the local OAuth request and starts passkey or OIDC authentication. | [Framework fit](GUIDELINES.md#framework-fit): Zrest, ZfURI, and common auth continuation. |
| `POST /token` | Zum handles code, refresh, and client-credentials grants. | [Data movement](GUIDELINES.md#data-movement-and-initialization): parse mutable form data in place and clear secrets. |
| `GET /.well-known/oauth-authorization-server` | Zum advertises exactly the implemented OAuth profile. | [Scope and cleanup](GUIDELINES.md#scope-and-cleanup): omit unsupported features. |
| `GET /jwks` | Zum publishes active and still-valid retiring keys. | [Liveness](GUIDELINES.md#liveness): bounded indexed key traversal. |
| `POST /revoke` | Zum revokes a refresh family using RFC 7009 response semantics. | [Persistence](GUIDELINES.md#persistence): conditional indexed mutation. |
| `POST /passkey/begin` | Zum starts an allowed enrollment/add/recovery ceremony. | [Framework fit](GUIDELINES.md#framework-fit): common ZfJSON/WebAuthn parsing. |
| `POST /passkey/finish` | Zum parses and completes the server-bound ceremony. | [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow): switch on stored purpose and flatten errors. |
| `GET /oidc/callback` | Zum completes configured upstream authentication. | [I/O sharding](GUIDELINES.md#io-sharding): asynchronous exchange and owner-shard handoff. |

Do not add `/logout`, `/userinfo`, local OIDC-provider metadata, or introspection
in this change. There is no local browser session to log out. Optional upstream
discovery is only an initialization client operation. `/revoke` ends refresh
authority; already issued self-contained access tokens remain valid until their
short expiry. Advertise only routes, grants, client-authentication methods, and
algorithms actually implemented.

## Names and file layout

All new C++ names are camel-case and at most 28 bytes. Source and unit-test
basenames are CamelCase with the `Zum` prefix; example program files remain
lowercase. FlatBuffers files retain lowercase underscore names. These rules come
from [GUIDELINES: Naming](GUIDELINES.md#naming) and
[GUIDELINES: Header layout](GUIDELINES.md#header-layout).

| Purpose | Name |
| --- | --- |
| Runtime owner | `Server` in `ZumServer.hh/.cc` |
| CRTP REST adapter | `HTTP<App>` in `ZumHTTP.hh` |
| OIDC adapter | `OIDC` in `ZumOIDC.hh/.cc` |
| OIDC configuration | `OIDCConfig` |
| Pending OIDC request | `OIDCReq` |
| Validated OIDC claims | `OIDCClaims` |
| Authentication enum | `AuthMethod` with `Passkey`, `OIDC` |
| Role-source enum | `OIDCRoles` with `Local`, `Mapped` |
| Role-map entry | `RoleMap` |
| Common user authority load | `loadUserAuth` |
| Client authority load | `loadClientAuth` |
| Grant authority reload | `loadGrantAuth` |
| OIDC start/finish | `oidcBegin`, `oidcFinish` |

Use `m_` only for private class members; structs remain all-public with data
first. Use overloaded getters/setters and trailing underscores only for internal
or owning-shard variants. Internal enums use `namespace E { enum { ... }; }`;
use `ZtEnum` only for persisted or externally named values needing lookup. Do
not use `enum class`, concepts, `requires`, anonymous namespaces, or STL
containers ([GUIDELINES: C++ language](GUIDELINES.md#use-of-c-language),
[GUIDELINES: Class and struct shape](GUIDELINES.md#class-and-struct-shape), and
[GUIDELINES: Trailing underscores](GUIDELINES.md#trailing-underscores)).

## Performance, ownership, and audit gate

Every implementation review must check every audit flag in `GUIDELINES.md`.
The following records how this design addresses them; an exception requires a
measured or protocol-specific justification in the code.

| Guideline audit | Required implementation |
| --- | --- |
| Nondependent template declarations | Keep them in `Server`, `OIDC`, or file scope; `HTTP<App>` contains only dependent adaptation. |
| Invariant loop calls and repeated indexing | Cache invariant lengths/values and each indexed element within loops. |
| Hard-coded capacities | Centralize named, configurable limits in `Limits`; comments cite an RFC maximum or measured workload/footprint choice. |
| Table/container GC scans | Use Grant/Audit indexes and pending-request expiry indexes; delete terminal objects immediately. |
| Intrusive objects stored by value | Store `ZuRef`/`ZmRef` as appropriate; never place a ref-counted object value in a hash. |
| Struct padding/cache contention | Order by size and lifecycle; separate shard-exclusive groups and align genuinely contended groups to `Zm::CacheLineSize`. |
| Fixed arrays/tables and array storage | Use `ZuArray` only for protocol-fixed crypto widths, `ZtBuiltin` for measured common small sets, `ZtScratch` for hot scratch, and `ZtArray` for cold variable data. |
| Buffer/node allocation | Use `ZiIOBuf`, intrusive nodes, `ZmHeap`/`ZmVHeap`, and named heap IDs. |
| Hot-path heap scratch | Parse in place and use `ZtScratch`; retain heap state only across asynchronous callbacks. |
| Split allocations | Co-locate pending request, refcount, timer, and hash node where practical. |
| Logger captures | Copy only stable values into `ZiLOG`; never capture pointers or references. |
| Temporary copies/contiguous conversions | Move owned strings/buffers; operate on spans and write JSON/JWT directly into final storage. |
| Default initialization before overwrite | Construct result records in final storage; avoid zeroing arrays that are immediately filled. |
| Repeated blocks | One authority resolver and one HTTP completion/error path serve passkey, OIDC, and client credentials. |
| Async `run`/`invoke` semantics | All dependent work occurs in continuations; no read-after-post assumptions. |
| Polling/blocking and timed tests | Runtime is continuation-based; tests use `ZmBlock`/`ZmSemaphore` only from permitted controlling threads and never sleep/poll. |
| Iterator lifetime/count snapshots | End traversal before mutation/post/callback; count visited records during traversal; avoid needless aliases and redundant iterator null checks. |
| Complexity, switches, and nesting | Use `switch` for grant/ceremony/OIDC states, early exits for errors, and bounded helpers for repeated state transitions. |
| Hashing | Use Z span/type hashes and composite hash conventions; no hand-written FNV. |
| Framework duplication | Use ZfURI, ZfJSON, ZfbStruct, Ztls, Zrest/Zhttp, ZiIOBuf, ZmScheduler, and Z containers. |
| Direct C I/O and varargs | Use ZiFile/ZiLog and `ZuBox`/`ZuFmt`/stream output; no `FILE`, `syslog`, or `printf` paths. |
| Separate validity booleans | Prefer sentinel/tagged state. Retain `Grant.oauthStatePresent`: `ZtString` and the FlatBuffers string transform collapse null and empty, so this tag is required to preserve OAuth's absent/empty distinction. Parser `seen` bitsets remain necessary for duplicate detection. |
| Cast friction and enum mapping | Use native Z span/string conversions and enum values directly; no redundant casts or mapping switches. |
| Goal/dead compatibility code | Delete superseded endpoint code and schemas after dependents move; add no shims or forwarders. |
| Short-lived refs to owners | Pending/HTTP/I/O objects use raw back-pointers; owners stop ingress and drain callbacks before destruction. |
| Needless atomics/locks/copies | Keep authority and provider-cache state on an owner shard; do not add locks to permit foreign-shard access or atomics for approximate diagnostics. |
| Long scheduler turns | Bound key/row processing per turn and post continuations for remaining work. |

The table above applies [GUIDELINES: Audit flags](GUIDELINES.md#audit-flags),
[GUIDELINES: Framework capabilities](GUIDELINES.md#leveraging-key-z-framework-capabilities),
and [GUIDELINES: I/O sharding](GUIDELINES.md#io-sharding).

Issuer-owned IAM and grant state stays on its designated Zdb shard. Public calls
are thin dispatchers to trailing-underscore shard functions. HTTP Rx state stays
on Rx; response Tx state stays on Tx. Cross-shard posts capture only fixed-size
metadata or handles; variable input remains in destination-owned/pool-backed
buffers. No lock makes non-owner access acceptable. Teardown stops ingress,
drains Rx, drains Tx, then releases owners as required by
[GUIDELINES: I/O function organization](GUIDELINES.md#io-function-organization),
[GUIDELINES: Cross-shard handoffs](GUIDELINES.md#cross-shard-handoffs), and
[GUIDELINES: Teardown](GUIDELINES.md#teardown).

## Delivery

1. Add focused tests for the current local OAuth/passkey behavior and authority
   ceiling before structural edits.
2. Introduce `Server` and complete the library-owned HTTP routes. Move passkey
   parsing, purpose dispatch, and completion out of `zumd`; delete the duplicate
   `FinishReq`, flow selector, error formatting, and callback chain. Add the
   transaction cookie and `/revoke` handler here.
3. Factor `loadUserAuth`, `loadClientAuth`, and `loadGrantAuth` around one bitmap
   resolver while retaining `Scope.roleIDs` and all ten tables.
4. Add `oidcSub`, `OIDCConfig`, the OIDC adapter, bounded pending state, and
   provider-key cache. Feed verified OIDC roles into `loadUserAuth` without
   changing local `User.roleIDs`.
5. Reorder `Grant` storage and document fields by kind. Preserve OAuth state
   with its required presence tag; update its FlatBuffers schema through
   `ZfbStruct`, saga payloads, and all consumers together.
6. Reduce `zumd` to configuration, seed data, page rendering, signing, transport,
   and one example resource. Delete all example-only protocol implementations.
7. Delete unused APIs, structs, schemas, and generated build entries exposed by
   the refactor. Propagate breaking changes; do not retain compatibility layers.
8. Update `zum.md` and `zum_model.mermaid` to describe the implemented model.

Before changing persistent schemas, settle outstanding sagas and decide whether
the development data requires an offline migration. Never delete user data as a
cleanup shortcut.

## Tests and acceptance

Unit tests in `zum/test` use `ZuTestUtil` and TAP. Full HTTP, restart, persistence,
and multi-component flows belong in `zum/itest`; independent OIDC/JWT provider
fixtures belong in `zum/interop` ([GUIDELINES: Module layout](GUIDELINES.md#module-layout)
and [GUIDELINES: Tests](GUIDELINES.md#tests)).

Required coverage:

- passkey enrollment/assertion and account recovery;
- local passkey -> authorization code -> token;
- upstream OIDC -> local authorization code -> token;
- OIDC state, nonce, PKCE, issuer, audience, algorithm, expiry, and `sub` checks;
- local-role and mapped-role modes, malformed claims, unknown roles, and explicit
  account linking;
- identical scope-to-role intersection for built-in users, OIDC users, and
  `client_credentials` clients;
- disallowed scopes, mixed/wrong audiences, disabled records, and overlapping
  role actions;
- code replay, refresh reuse/narrowing, ceiling preservation, and key rotation;
- RFC 7009 unknown-token behavior, refresh-family revocation, and the documented
  access-token expiry window;
- bounded cleanup, unknown-`kid` refresh coalescing, cancellation, restart, and
  exactly-once completion; and
- mounting all Zum routes without host passkey parsing or OAuth error handling.

Preserve the existing clang debug build configuration. Do not run `z.config`,
`configure`, `autoreconf`, `make clean`, or otherwise change the configured build
type. Batch related implementation changes instead of rebuilding after each
delivery step. If intermediate compiler feedback is materially useful, rebuild
only the changed source layer with `make -C zum/src -j8`; never build a dependent
directory against source that has not first been rebuilt.

Defer rebuilding `test`, `util`, `itest`, `example`, `bench`, `interop`, and all
other dependent modules until implementation is complete. Then run one top-level
`make -j8` to refresh the dependency hierarchy and build the dependent default
targets, followed by the relevant `make test` invocations and `git diff --check`.
Repeat a build only after fixing a build or test failure.
Perform ASan/LSan and valgrind checks only when supported by the existing build;
do not reconfigure this tree to obtain another build type. Use `libtool exec` for
source-tree debuggers. This cadence applies the source-before-dependent ordering
while deliberately constraining reconfiguration and rebuild frequency
([GUIDELINES: Build configuration](GUIDELINES.md#build-configuration),
[GUIDELINES: Acceptance](GUIDELINES.md#acceptance), and
[GUIDELINES: Debugging](GUIDELINES.md#debugging)).

Completion means a host can mount Zum's OAuth and passkey endpoints, select
built-in or upstream OIDC authentication, and enforce the same local scope/role
model without reproducing authentication protocol code in the host.
