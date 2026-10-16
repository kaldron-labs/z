# `ZtlsKEM` hybrid precursor for `ZtlsAge`

Extend [`kem.md`](kem.md) with the **MLKEM768-X25519 hybrid KEM** required by
[`age.md`](age.md), using the audited standalone X25519 implementation in
[`x25519.md`](x25519.md). This plan is the raw KEM boundary only. It does not
implement age headers, Bech32, SSH keys, a vault, or HPKE SealBase/OpenBase.
The byte layout and algorithms follow the
[MLKEM768-X25519 HPKE construction](https://filippo.io/hpke-pq), which
[age's hybrid recipient implementation](https://github.com/FiloSottile/age/blob/main/pq.go)
uses. The base [C2SP age v1 specification](https://c2sp.org/age@v1.1.0)
does not itself specify this post-quantum recipient profile.

## Dependency and public contract

- Use the implemented standalone FIPS 203 `PK_MLKEM768` and
  `SK_MLKEM768` in `ZtlsKEM.hh` and `PK_X25519` / `SK_X25519` in
  `ZtlsPK.hh`. Reuse their `Backend::PKey` operations without allocating
  nested heap key wrappers; an
  ML-KEM SPKI or PKCS#8 object is **not** an age hybrid key. The X25519
  backend entry points are `pkey_x25519_import_private`,
  `pkey_x25519_export_public`, and `pkey_x25519_agree`; the raw public
  API is `PK_X25519::exportPK`, and `SK_X25519::agree` takes raw peer
  bytes and writes to a caller-owned 32-byte span. The existing X25519
  backend checks exact extents and rejects failed or all-zero agreement;
  the hybrid must not add a second public-key validation layer. Do not
  call OpenSSL from the hybrid wrapper.
- Define concise concrete hybrid `PK_` and `SK_` types alongside the
  existing types in `Ztls::PK`, following `ZtlsKEM.hh` naming,
  `Type_<Heap = ZuVoid>`, concrete `ZmHeap` identity, `ZuDerive`, and
  `ZuUnion<..., ZeException>` error conventions. Do not introduce a
  `Ztls::KEM` namespace, a virtual KEM interface, or KEM methods on
  `AnyKey`. The private hybrid key does retain a second backend key owner
  for X25519, as specified below.
- The public hybrid constructor consumes **exactly 1216 raw bytes**:
  1184-byte ML-KEM encapsulation key followed by a 32-byte X25519 public
  key. The private constructor consumes the **32-byte hybrid identity
  seed**, not the 64-byte ML-KEM seed from `kem.md`. The caller may
  generate that seed using `Ztls::Random`. `exportPK` writes the raw
  1216-byte public key to caller storage. `encapsulate` writes the
  1120-byte encapsulated key and 32-byte shared secret directly to caller
  spans; `decapsulate` reads the encapsulated key and writes the secret
  directly to caller storage. Borrowed inputs are not retained.
- Keep Bech32 `age1pq1…` and `AGE-SECRET-KEY-PQ-1…` encoding in
  `ZtlsAge`; keep HPKE message encryption outside the raw KEM, using
  backend-independent zpicotls HPKE directly where compatible. Do not add
  ASN.1/PKCS#8 serialization for the hybrid solely because standalone
  ML-KEM has it. Do not add a public ML-KEM seed export API. The caller
  retains the 32-byte hybrid identity seed for identity-file encoding;
  the hybrid object retains only the expanded state needed for operations.

## KEM implementation

1. Use named format constants: identity seed 32, raw public key 1216,
   encapsulated key 1120, shared secret 32; derive the component extents
   from `MLKEM768PublicSize`, `MLKEM768CiphertextSize`,
   `MLKEM768SecretSize`, and `Ztls::X25519KeySize` from
   `ZtlsBackend.hh`. Do not add hard-coded lookup tables or variable-size
   containers for these fixed values.
2. Expand the 32-byte hybrid seed with SHAKE256 to **96 bytes**. The first
   64 are the FIPS 203 ML-KEM key-generation seed and the final 32 are the
   X25519 seed. Import the first part with
   `Backend::pkey_mlkem768_import_seed` and export its public half through
   `Backend::pkey_mlkem768_export_public`; import the last part with
   `Backend::pkey_x25519_import_private` and obtain its public half with
   `Backend::pkey_x25519_export_public`. Keep SHAKE256 behind a narrow
   `Ztls` backend operation, not a direct `EVP_*` call in the KEM type.
   This expansion is the age/HPKE hybrid identity rule, not
   `SK_MLKEM768(ZuBSpan)` semantics. Clear the 96-byte expansion after
   both backend keys have imported their seeds, including on failure.
3. Encapsulate the ML-KEM public half through the backend operation used by
   `PK_MLKEM768`; create a fresh stack-local `SK_X25519` with `Random &`,
   derive its public key with `exportPK`, use `agree` against the
   recipient's raw X25519 public bytes, and write
   `ct_PQ || ct_X25519` directly to the caller's 1120-byte
   destination. Compute the 32-byte hybrid secret as
   `SHA3-256(ss_PQ || ss_X25519 || ct_X25519 || pk_X25519 || Label)`,
   with `Label` exactly the six bytes `5c 2e 2f 2f 5e 5c`. Use a
   normalized `Ztls` SHA3-256 operation, preferably incremental, so no
   concatenation allocation or copy is needed. Do not substitute
   SHA-256, HKDF, or a TLS hybrid combiner.
4. Decapsulate by splitting the 1120-byte input into the 1088-byte ML-KEM
   ciphertext and 32-byte ephemeral X25519 public key. Use the
   receiver's retained X25519 public key for the combiner, obtain both shared
   secrets, and use the **same** ordered SHA3-256 expression. Keep the
   imported ML-KEM and X25519 backend keys for repeated decapsulation;
   call `Backend::pkey_x25519_agree` with the retained X25519 key and
   the ephemeral public-key slice. Do not repeat SHAKE expansion or key
   import per call. Treat failure of
   X25519 agreement (including disallowed all-zero output) as failure;
   do not revalidate ML-KEM mathematics already handled by its provider.
   Clear intermediate seeds and shared secrets on every exit with
   `ZuGuard` / `ZuClear`; the X25519 backend already clears the secret
   output if an attempted derivation fails. Do not duplicate its
   low-order-point check.
5. The public hybrid key needs one ML-KEM `Backend::PKey` and inline raw
   X25519 public bytes. The private hybrid key also needs one retained
   X25519 `Backend::PKey`: the implemented agreement API requires a key
   owner, and retaining it avoids per-decapsulation import and allocation.
   Manage both through the existing `pkey_new`/`pkey_free` convention;
   do not add a nested heap-allocated `PK_MLKEM768`/`SK_MLKEM768` or
   `SK_X25519` wrapper, or extra reference-counted objects. A fresh
   ephemeral `SK_X25519` on the stack is appropriate for encapsulation.
   Use caller spans for output and fixed-size protocol-bound primitive
   arrays for the 96-byte expansion and 64-byte combined shared-secret
   scratch. Those extents do not warrant a dynamic container or heap
   fallback. Do not zero-initialize storage immediately overwritten by
   crypto output.

## Verification and boundary

- Add focused TAP tests in `ztls/test/ZtlsHybridTest.cc` for deterministic
  public-key derivation from the 32-byte seed, encapsulate/decapsulate agreement,
  independent encapsulations, exact output sizes, wrong-length inputs,
  and small-order X25519 failure. Inspect the `ZuGuard` paths for private
  scratch clearing rather than attempting to read expired stack storage
  in a test. Use the independent [HPKE-PQ KEM 0x647a vectors](https://github.com/FiloSottile/hpke/blob/main/testdata/hpke-pq.json)
  to check seed-derived public key and decapsulation of the vector's
  encapsulated key against its shared secret; a self-round-trip alone is
  insufficient.
- The `kem2.md` gate is reached when raw 32-byte identity and 1216-byte
  public key interoperate with the specified hybrid KEM, and no direct
  OpenSSL call appears in the public KEM types. It does **not** claim age
  interoperability until zpicotls HPKE (or the equivalent composition) and `ZtlsAge`
  stanza handling are complete.
- Follow `GUIDELINES.md` for direct includes, hard tabs, concrete heap
  identity, fixed-size contract-bound primitive arrays, minimal copying,
  and no redundant validation. Use tagged heap-fallback scratch only if
  a larger, non-contract-bound temporary is actually needed.
  Reuse the current configured build artifacts; batch changes, rebuild
  `ztls/src` before `ztls/test`, and defer dependents. Run the focused
  KEM tests and a final sparse Valgrind gate; other build configurations
  remain out of scope unless separately requested.
