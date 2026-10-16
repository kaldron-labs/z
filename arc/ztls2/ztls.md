# Ztls in-memory enrollment credential precursor plan

## Outcome

Extend `Ztls` just enough for an ephemeral enrolling client to:

- generate a DER PKCS#10 CSR from any existing `Ztls::PK` secret key;
- retain that same key with an enrollment-issued DER client certificate chain;
- add enrollment-issued DER CA certificates to the normal server-trust store;
  and
- initialize `Ztls::Client` for mTLS without writing or re-encoding the private
  key.

This is a precursor to `plan.md`. It changes `ztls` only. HTTP enrollment,
JSON/base64, bearer tokens, telemetry, hub policy, application bounds, and
environment variables remain outside this plan.

## Audit conclusions and simplifications

The earlier design contained avoidable abstractions and data movement. Delete
them from the intended implementation:

- Do not serialize a live `Ztls::PK` private key to PKCS#8 merely to put it in
  `ClientCreds`, then parse it back into the same backend key type. Retain the
  existing intrusive key reference instead. This removes a secret copy, a
  parse, failure paths, and the proposed `pkey_load_der()` and `cleanse()` APIs
  ([Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
  [Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies),
  [Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).
- Do not add `SupplementalCA`. It provided no invariant beyond an owning
  certificate array. Store supplemental CA certificates directly in
  `ClientParams` as `Certs` and validate them while building the trust store
  ([Framework fit](GUIDELINES.md#framework-fit),
  [Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).
- Do not expose an allocated backend `CSR` object with `new/free/size/write`
  functions. Add one synchronous backend operation that emits DER to the
  caller's sink; no intermediate repository-owned CSR object survives the call
  ([Heap allocation](GUIDELINES.md#heap-allocation),
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)).
- Do not model PKCS#10 a second time in `ZfASN1`. CSR construction, signature
  algorithm identifiers, public-key encoding, and signing are one atomic
  cryptographic backend operation. OpenSSL already implements it for RSA, EC,
  and Ed25519. Keep that direct use inside `ZtlsOpenSSL.cc`, as is already done
  for X.509 certificate verification and TLS signing
  ([Framework fit](GUIDELINES.md#framework-fit),
  [I/O and system integration](GUIDELINES.md#io-and-system-integration)).
- Do not preserve new APIs solely as compatibility shims if their shape changes
  during implementation. The existing file-path client configuration remains
  because it is independently useful and already supported, not as an adapter
  for the new in-memory path
  ([General principles](GUIDELINES.md#general-principles),
  [Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

## Minimal API

### Certificate storage

Add heap-identified Z array vocabulary in a small `ZtlsCreds.hh` header:

```cpp
namespace Ztls {

ZuDerive(Cert,
  (ZtArray<uint8_t, ZtArrayHeapID<"Ztls.Cert">>));
ZuDerive(Certs,
  (ZtArray<Cert, ZtArrayHeapID<"Ztls.Certs">>));

} // Ztls
```

`Certs` owns a collection of DER certificates. `ClientCreds` interprets its
collection as leaf first; supplemental CA order is immaterial. The enrollment
parser decodes directly into `Cert` elements and moves the collection into TLS
configuration; there is no intermediate span-owning wrapper and no DER byte
copy. These are cold-path, variable-sized arrays, so `ZtArray` with explicit
heap IDs is
appropriate; do not use fixed arrays, STL containers, or unexplained built-in
capacities ([Storage and capacity](GUIDELINES.md#storage-and-capacity),
[Heap allocation](GUIDELINES.md#heap-allocation),
[Storage and lifetime](GUIDELINES.md#storage-and-lifetime)).

### `ClientCreds`

`ClientCreds` owns one existing secret-key reference and the issued client
chain:

```cpp
namespace Ztls {

class ClientCreds {
public:
  ClientCreds();
  ClientCreds(ZmRef<PK::AnyPK> key, Certs certs);
  ~ClientCreds();

  ClientCreds(const ClientCreds &) = delete;
  ClientCreds &operator =(const ClientCreds &) = delete;
  ClientCreds(ClientCreds &&);
  ClientCreds &operator =(ClientCreds &&);

  explicit operator bool() const;
  const Certs &certs() const;

private:
  template <typename> friend class Client;

  Backend::PKey *pkey_() const;
  void clearCerts_();

  Certs			m_certs;
  ZmRef<PK::AnyPK>	m_key;
};

} // Ztls
```

The constructor requires the caller to move a `ZmRef` to an existing
`SK_RSA`, `SK_EC`, or `SK_ED25519`; `Client::init()` rejects a public-only key.
The null key is the object's null sentinel. Do not add a separate validity
boolean, expose the backend key, expose mutable certificate storage, or provide
a PKCS#8 accessor. Define operations requiring the complete `PK::AnyPK` type
out of line so `ZtlsCreds.hh` can forward-declare it and avoid pulling the
template-heavy `ZtlsPK.hh` into every `Ztls.hh` dependent
([Framework fit](GUIDELINES.md#framework-fit),
[Assertions and type mechanics](GUIDELINES.md#assertions-and-type-mechanics),
[Module layout](GUIDELINES.md#module-layout)).

`ClientCreds` is a class because its data is private and its invariants are
enforced by its interface. Members use `m_`, are ordered largest first where
practical, and names remain below 28 bytes
([Class and struct shape](GUIDELINES.md#class-and-struct-shape),
[Length and abbreviations](GUIDELINES.md#length-and-abbreviations),
[Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes)).

### CSR generation

Add the same sink API to every existing secret-key template:

```cpp
template <typename Heap>
struct SK_RSA_ {
  template <typename S>
  ZuUnion<void, ZeException> saveCSR(S &out) const;
};

template <typename Heap>
struct SK_EC_ {
  template <typename S>
  ZuUnion<void, ZeException> saveCSR(S &out) const;
};

template <typename Heap>
struct SK_ED25519_ {
  template <typename S>
  ZuUnion<void, ZeException> saveCSR(S &out) const;
};
```

Each is a one-line wrapper around one file-scope common implementation; do not
duplicate CSR construction in three templates or nest non-key-dependent
declarations within them. RSA uses SHA-256 with PKCS#1 v1.5, EC uses
ECDSA/SHA-256, and Ed25519 uses Ed25519. The request has an empty X.509 subject
and no requested extensions. It returns DER through the established Ztls key
sink/error convention; PEM and base64 remain caller concerns
([Code structure](GUIDELINES.md#code-structure),
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
[Type and API friction](GUIDELINES.md#type-and-api-friction)).

Add only this backend callback operation:

```cpp
namespace Ztls::Backend {

using CSRFn = bool (*)(void *, ZuBSpan);

bool pkey_csr(const PKey *key, void *ctx, CSRFn fn);

} // Ztls::Backend
```

The callback is synchronous; its span is valid only for the call. The
`saveCSR()` wrapper passes a stack context referring to the output sink, and
the backend reports sink or crypto failure as `false`. OpenSSL temporary
objects and its DER buffer are released before return. Do not store the
callback, capture a long-lived owner, or allocate a Z callback wrapper for this
cold synchronous boundary
([Callbacks](GUIDELINES.md#callbacks),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

The backend name retains the existing C-style `pkey_*` vocabulary. Public C++
members remain camel case (`saveCSR`, `caCerts`); no new credential API accepts
`const char *`
([Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes),
[Strings](GUIDELINES.md#strings),
[Type and API friction](GUIDELINES.md#type-and-api-friction)).

### `ClientParams`

Replace `using ClientParams = HubParams` with a class derived publicly from
`HubParams`:

```cpp
class ClientParams : public HubParams {
public:
  using HubParams::HubParams;

  HubParams &hub();
  const HubParams &hub() const;
  template <typename L> ClientParams &&hub(L &&);

  ClientParams &&creds(ClientCreds value);
  ClientParams &&caCerts(Certs value);

  const ClientCreds &creds() const;
  const Certs &caCerts() const;

private:
  ClientCreds	m_creds;
  Certs		m_caCerts;
};
```

Both credential setters take ownership and use the same overloaded
getter/setter name. `hub()` exposes the base parameters, while its lambda
overload configures them and returns the concrete `ClientParams`, following
the `ZiMxParams::scheduler()`/`ZmSchedParams` pattern. This preserves inline
configuration without forwarding every `HubParams` setter or decaying the
expression to `HubParams`. It also avoids the near-identical forwarding block
currently present in `ServerParams`
([Accessors](GUIDELINES.md#accessors),
[Code structure](GUIDELINES.md#code-structure),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

`ClientParams` is move-only as a consequence of `ClientCreds`; pass it to the
existing `Client::init(ClientParams)` by move. Do not copy keys or certificate
collections through the existing Rx initialization continuation
([Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
[Cross-Shard Handoffs](GUIDELINES.md#cross-shard-handoffs)).

### Backend certificate operations

Retain the existing path functions for the existing path branch. Replace the
raw pointer/length DER store function and add only the operations needed by the
in-memory branch:

```cpp
namespace Ztls::Backend {

bool load_certificates_der(
  ptls_context_t *ctx, ZuSpan<const ZuBSpan> certs, const PKey *key);

bool cert_store_add_der(CertStore *store, ZuBSpan cert);
bool cert_store_add_ca(CertStore *store, ZuBSpan cert);

PKey *pkey_load_pem(const char *path);                  // existing
bool load_certificates(ptls_context_t *, const char *); // existing

} // Ztls::Backend
```

`load_certificates_der()` validates every DER value with full input
consumption, requires a private key, and matches the leaf public key to it. Use
an OpenSSL `X509_STORE_CTX` with partial-chain verification and the last
supplied certificate as the enrollment-authorized validation anchor. This
checks time, signatures, Basic Constraints/path length, key usage, and client
authentication EKU without incorrectly requiring the normally omitted root or
mixing the client chain into platform server trust. Allocate and validate a
complete candidate picotls list before installing it; free the candidate on
every failure.

`cert_store_add_der()` is the span replacement for the current internal
pointer/length function and continues to load platform roots, including the
Windows store. `cert_store_add_ca()` additionally requires a currently valid
CA certificate and is used only for enrollment-supplied trust. Duplicate
certificates remain idempotent. Keep every `X509`, `BIO`, `EVP_PKEY`, and
picotls allocation inside `ZtlsOpenSSL.cc`, with one cleanup exit or equivalent
RAII and no OpenSSL type in a public Ztls header
([I/O and system integration](GUIDELINES.md#io-and-system-integration),
[General principles](GUIDELINES.md#general-principles),
[Type and API friction](GUIDELINES.md#type-and-api-friction)).

## Phase 1: CSR support

1. Implement `Backend::pkey_csr()` in `ZtlsOpenSSL.cc` using `X509_REQ`. Set
   version zero, an empty subject, and the key's public key; select the signature
   operation with a `switch` on backend key type. Reject unsupported or
   public-only keys. DER-encode once, invoke the synchronous callback once,
   then clean every OpenSSL allocation.
2. Add one file-scope common `template <typename S>` `saveCSR_` bridge outside
   the key classes in `ZtlsPK.hh`; each secret-key member delegates to it. Only
   the sink adapter is templated; keep all nondependent CSR construction in
   `Backend::pkey_csr()`. Do not alter the existing generic digest `sign()`
   contract merely to support CSR generation.
3. Extend `ZtlsPKTest` for RSA, P-256 EC, and Ed25519. Parse each CSR with
   OpenSSL, require full DER consumption, compare its public key to the source
   key, verify its signature, and assert empty subject/no extensions.

Interim acceptance: all existing key types produce interoperable PKCS#10 DER,
and malformed/public-only/backend failures return `ZeException` without leaks.

Applicable guidance: [Code structure](GUIDELINES.md#code-structure),
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
[Framework fit](GUIDELINES.md#framework-fit), and
[Tests](GUIDELINES.md#tests).

## Phase 2: In-memory client credentials

1. Add `ZtlsCreds.hh`/`.cc` with the header skeleton, direct includes, include
   guard, modelines, license, exported definitions, and no circular/tail
   includes. Register both files in `ztls/src/Makefile.am`.
2. Add `ClientParams` without inherited setter forwarders. Extend
   `Client::init()` validation to accept exactly one credential source: neither
   for server-authenticated TLS, both existing `certPath` and `keyPath`, or
   non-null `ClientCreds`. Reject incomplete and mixed forms before network
   startup.
3. Change the `Client::init()` initialization continuation to accept its moved
   `ClientParams` mutably. Move `params.m_creds` into one `ClientCreds m_creds`
   member before installing it. Create a `ZtScratch` array of `ZuBSpan` views
   over its `Certs`; this cold-path temporary contains no DER copy. Construct
   `m_sign` from `m_creds.pkey_()`, then call
   `load_certificates_der()`. This order leaves no installed certificate list
   if signer construction fails. After the picotls context owns its
   certificate bytes, call
   `m_creds.clearCerts_()` so the duplicate input collection does not survive
   initialization.
4. Preserve existing path-key ownership. Use null `m_creds` as the ownership
   sentinel: the destructor frees `m_sign` first, then calls
   `pkey_free(m_key)` only for the path-owned key; otherwise the subsequently
   destroyed `m_creds` releases the in-memory key. Unwind `m_sign`, `m_key`, and
   `m_creds` in that order on failed initialization. Add no boolean, lock,
   atomic, exposed mutable accessor, or runtime polymorphic credential
   strategy.

Interim acceptance: in-memory mTLS succeeds with each supported key type and a
matching chain, while empty, malformed, public-only, mismatched, reversed,
broken, expired/not-yet-valid, wrong-key-usage/EKU, constraint-violating, and
non-CA-intermediate inputs fail
before connection establishment.

Applicable guidance: [Header layout](GUIDELINES.md#header-layout),
[Module layout](GUIDELINES.md#module-layout),
[Storage and lifetime](GUIDELINES.md#storage-and-lifetime),
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
and [Class and struct shape](GUIDELINES.md#class-and-struct-shape).

## Phase 3: Additive server trust

1. Split `Hub::loadCA()` internally into store creation, base-store loading,
   supplemental additions, and verifier installation. If `caPath` is null,
   load the platform store exactly as today; otherwise load the explicit path.
   Then add every `ClientParams::caCerts()` item with
   `Backend::cert_store_add_ca()` before constructing `m_verify`.
2. Cache collection length before loops and each indexed element before
   repeated use. Abort initialization immediately on the first invalid CA;
   ordinary `Hub` teardown releases the partially constructed store. Do not
   add rollback state, a second store, background validation, or a trust-store
   cache.
3. Preserve SNI, exact hostname verification, validity checks, session-ticket
   behavior, and existing TLS Rx/Tx ownership. Add no verification-disable
   option and no application-specific early-data policy.

Interim acceptance: a server chains to either the base store or a supplemental
CA successfully; unrelated, malformed, expired, or non-CA supplemental
certificates and wrong hostnames fail. Supplemental roots do not replace base
trust.

Applicable guidance: [Code structure](GUIDELINES.md#code-structure),
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
[I/O sharding](GUIDELINES.md#io-sharding), and
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup).

## Audit checklist for implementation review

- **Code structure:** common CSR work is outside key templates; key-type
  dispatch is one `switch`; invariant lengths/elements are cached; control flow
  uses early failure and one cleanup path
  ([Code structure](GUIDELINES.md#code-structure),
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)).
- **Storage/capacity:** certificate counts and bytes are caller-bounded;
  `ZtArray` carries explicit heap IDs; span scratch uses `ZtScratch`; there are
  no fixed certificate arrays, lookup tables, retained zombies, or periodic GC
  ([Storage and capacity](GUIDELINES.md#storage-and-capacity),
  [Liveness](GUIDELINES.md#liveness)).
- **Allocation:** repository-owned variable storage uses Z heaps. Native
  OpenSSL allocations exist only inside the backend, are cold-path, and are
  released in the same call or normal TLS teardown
  ([Heap allocation](GUIDELINES.md#heap-allocation),
  [Storage and lifetime](GUIDELINES.md#storage-and-lifetime)).
- **Movement/initialization:** the key reference and `Certs` move through params
  into the client; input certificates are released after their single copy into
  picotls-owned handshake storage; no
  private-key serialization, temporary contiguous DER copy, default-before-
  overwrite array fill, or secret-bearing log capture is introduced
  ([Data movement and initialization](GUIDELINES.md#data-movement-and-initialization)).
- **Framework/API fit:** public data uses `ZmRef`, `ZtArray`, `ZtScratch`,
  `ZuBSpan`, `ZuUnion`, and `ZeException`; OpenSSL remains behind
  `ZtlsBackend.hh`; no STL container, `FILE`, printf, raw credential C string,
  unnecessary cast, or hand-rolled ASN.1 parser is added
  ([Framework fit](GUIDELINES.md#framework-fit),
  [Type and API friction](GUIDELINES.md#type-and-api-friction)).
- **Concurrency/lifetime:** CSR generation is synchronous and thread-confined;
  credential/trust installation occurs during existing Rx-owned initialization
  and is stable afterward; no new lock or atomic is added; signing is destroyed
  before its retained/path-owned key; no callback outlives its stack context
  ([Concurrency and sharding](GUIDELINES.md#concurrency-and-sharding),
  [I/O sharding](GUIDELINES.md#io-sharding),
  [Lifetime](GUIDELINES.md#lifetime)).
- **Scope:** no enrollment transport, retry, token, JSON, fleet policy,
  persistence, new key type, generic trust manager, or compatibility wrapper is
  implemented in `ztls`
  ([Scope and cleanup](GUIDELINES.md#scope-and-cleanup),
  [Persistence](GUIDELINES.md#persistence)).
- **Style/naming:** new public files are `ZtlsCreds.{hh,cc}`; public types are
  `Cert`, `Certs`, `ClientCreds`, and `ClientParams`; public members are
  `saveCSR`, `creds`, `certs`, and `caCerts`; backend functions follow the
  established C-style names. All names are under 28 bytes, class data uses
  `m_`, no newly added struct has private data, and source uses hard tabs with
  the local two-column logical indent
  ([Naming](GUIDELINES.md#naming),
  [Indentation](GUIDELINES.md#indentation),
  [Class and struct shape](GUIDELINES.md#class-and-struct-shape)).
- **Tests:** use `ZuTestUtil`, TAP, deterministic fixtures/synchronization, and
  no sleep/poll completion. Exercise error cleanup under valgrind; a sanitizer
  build is not required
  ([Tests](GUIDELINES.md#tests),
  [Acceptance](GUIDELINES.md#acceptance)).

## Verification

Reuse the existing clang debug build configuration. Do not run `autoreconf`,
`configure`, `z.config`, `make clean`, or otherwise reconfigure/change the
current build type during this implementation. Do not rebuild after every file
or phase: complete the implementation first, then build the changed layer and
its tests once. Defer rebuilding all dependent modules until the focused Ztls
tests pass.

1. Implement all three phases without intermediate builds unless a compiler
   diagnostic is specifically needed to resolve an uncertain API boundary.
2. Build the changed library once with `make -C ztls/src -j8`, then build its
   tests once with `make -C ztls/test -j8`. Because this Makefile hierarchy does
   not rebuild lower layers automatically, always build `ztls/src` before
   `ztls/test`.
3. Run the focused CSR/credential/handshake binaries, then
   `make -C ztls/test test`. Repair failures and rebuild only the changed target
   and directly affected test binary while iterating.
4. After focused Ztls tests pass, rebuild direct dependents once with
   `make -C zhttp/src -j8` followed by `make -C zhttp/test -j8`, and run the
   focused Zhttp tests affected by `ClientParams`.
5. At the end of implementation, run top-level `make -j8` once and then
   `make test` once to catch dependency, exported-header, and regression issues.
6. Run focused failure paths under valgrind with `libtool exec`; do not run
   `.libs` binaries directly. No sanitizer build is required.
7. Build and run the focused tests under MinGW/Windows as a separate platform
   verification step. No new platform branch may appear outside
   `ZtlsOpenSSL.cc` or the existing platform trust loader.

This order follows [Build configuration](GUIDELINES.md#build-configuration):
build the changed library before its tests, avoid mixed/stale objects, and
perform the broad dependent rebuild only after focused implementation work is
stable.

## Completion criteria

- `saveCSR()` works for RSA, P-256 EC, and Ed25519 and emits interoperable DER
  proving possession of the exact retained key.
- `ClientCreds` retains that key directly; no private-key DER/PEM serialization
  or temporary credential file occurs.
- In-memory client chains are validated, installed transactionally, and used
  successfully for mTLS.
- Supplemental DER CAs extend rather than replace the selected base trust
  store.
- Existing path-based TLS clients and servers continue to pass their tests.
- The implementation passes the audit checklist and builds warning-free with
  current gcc/clang on Linux and MinGW/Windows.

## Open questions and ambiguities

None currently identified.
