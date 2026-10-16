# `ZtlsKEM` implementation plan

Implement ML-KEM-768 as two concrete key types in the existing `Ztls::PK`
namespace. Install `ztls/src/ZtlsKEM.hh`; it includes `ZtlsPK.hh` and uses
`AnyKey` → `AnyPK` and the current `Backend::PKey` owner. A private key derives
from its public-key type, as in `ZtlsPK.hh`. Do not introduce a `KEM` namespace,
an `AnySK`, another `EVP_PKEY` owner, or virtual capability methods. These are
FIPS 203 ML-KEM keys, not age X-Wing identities or message-encryption keys.

## Public contract

Define the following in `ZtlsKEM.hh`, following the nearby `PK_ED25519_` and
`SK_ED25519_` layout, `Type_<Heap = ZuVoid>` / concrete `ZmHeap` aliases,
`ZuDerive`, `Secret` values, protected public-key default constructor, and
`using PK`, `using PK_`, `using PK_::key` in the private type:

```cpp
namespace Ztls::PK {

// FIPS 203 ML-KEM-768 fixed extents, in bytes.
enum {
  MLKEM768PublicSize = 1184,
  MLKEM768SeedSize = 64,
  MLKEM768CiphertextSize = 1088,
  MLKEM768SecretSize = 32
};

template <typename Heap = ZuVoid>
struct PK_MLKEM768_ : public Heap, public AnyPK {
  enum { Secret = 0 };
protected:
  PK_MLKEM768_() { }
public:
  PK_MLKEM768_(ZuBSpan pubKey);
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const;
  ZuUnion<void, ZeException> exportPK(ZuSpan<uint8_t> pubKey) const;
  ZuUnion<void, ZeException> encapsulate(
    Random &rng, ZuSpan<uint8_t> ciphertext,
    ZuSpan<uint8_t> secret) const;
};
using PK_MLKEM768_Heap = ZmHeap<"Ztls.PK_MLKEM768", PK_MLKEM768_<>>;
ZuDerive(PK_MLKEM768, (PK_MLKEM768_<PK_MLKEM768_Heap>));

template <typename Heap = ZuVoid>
struct SK_MLKEM768_ : public PK_MLKEM768_<Heap> {
  enum { Secret = 1 };
  using PK = PK_MLKEM768;
  using PK_ = PK_MLKEM768_<Heap>;
  using PK_::key;

  SK_MLKEM768_(Random &rng);
  SK_MLKEM768_(ZuBSpan key);
  template <typename S>
  ZuUnion<void, ZeException> save(S &s) const;
  template <typename S>
  ZuUnion<void, ZeException> savePK(S &s) const;
  ZuUnion<ZmRef<PK>, ZeException> mkPK() const;
  ZuUnion<void, ZeException> decapsulate(
    ZuBSpan ciphertext, ZuSpan<uint8_t> secret) const;
};
using SK_MLKEM768_Heap = ZmHeap<"Ztls.SK_MLKEM768", SK_MLKEM768_<>>;
ZuDerive(SK_MLKEM768, (SK_MLKEM768_<SK_MLKEM768_Heap>));

} // Ztls::PK
```

- `PK_MLKEM768_(ZuBSpan)` imports exactly the FIPS 203 `ek` public bytes.
  `SK_MLKEM768_(ZuBSpan)` accepts the 64-byte `(d, z)` key-generation seed
  or the 2400-byte expanded decapsulation key, distinguished by length;
  both are needed for standard PKCS#8 loading. These constructors throw
  `ZeEXCEPT(Error, "ZtlsKEM", ...)` on import failure, matching `ZtlsPK.hh`.
  The generated-key constructor takes `Random &` to match the existing PK
  constructors; OpenSSL supplies randomness internally, so mark the argument
  unused as the RSA/EC/Ed25519 implementations do. The encapsulation method
  follows the same `Random &` convention and similarly delegates randomness
  to OpenSSL. Do not synthesize a second random source or test-mode entropy
  parameter in the production API.
- `save`/`savePK` emit DER through `ZfASN1`, as in `ZtlsPK.hh`;
  `exportPK` writes raw `ek` bytes into the caller's destination. Keep these
  distinct: a raw KEM public key is not an SPKI structure. `mkPK`
  exports into `ZmScratch(uint8_t, MLKEM768PublicSize)` and constructs a
  separately owned public-only key; do not duplicate a private `EVP_PKEY` into
  the public object. Return `ZuUnion<ZmRef<PK>, ZeException>` and catch the
  public constructor's `ZeException`, as other `mkPK` methods do.
- Encapsulation writes ciphertext and shared secret directly into two caller
  spans. Decapsulation reads the ciphertext and writes directly into the
  caller's secret span. Use exactly the named extents at the public boundary;
  preserve OpenSSL's returned output lengths and require the FIPS 203 lengths
  on successful operations. The result is `ZuUnion<void, ZeException>`; no
  callback or result container is needed because both outputs have
  caller-owned storage and the operation is synchronous. Borrowed spans are
  valid only for the call. Do not log, format, or retain secret bytes.
- Declare no `verify` on the public type and no `sign` or `saveCSR` on the
  private type; an ML-KEM key cannot sign a CSR.
  Inheritance intentionally gives `SK_MLKEM768` public-key operations, as it
  does for existing PK types. Do not add KEM methods to `AnyKey`, `AnyPK`, RSA,
  EC, or Ed25519. Do not add a public seed-export API or an age wrapper in
  this milestone.

## ASN.1 key load/save

1. Extend `ZtlsPK_Data.hh` with `OIDs::MLKEM768` for RFC 9935
   `id-alg-ml-kem-768` (`2.16.840.1.101.3.4.4.2`), a public SPKI
   `ZfStruct` with that OID and a BIT STRING containing the unwrapped 1184
   `ek` bytes, and PKCS#8 private-key `ZfStruct` representations. The
   `AlgorithmIdentifier` parameters must be absent. The PKCS#8
   `privateKey` OCTET STRING contains a DER-encoded RFC 9935
   `ML-KEM-768-PrivateKey` CHOICE: `[0]` implicit 64-byte seed, 2400-byte
   OCTET STRING expanded key, or SEQUENCE of both. Use `ZfASN1` metadata
   and its tag/length primitives; do not hand-code DER lengths, OIDs, or
   nested wrapping. At the CHOICE boundary, dispatch once on the actual tag
   if `ZfASN1` cannot select the branch itself, then decode the chosen
   branch with its `ZfStruct` definition. Respect the encoded lengths, but
   do not re-validate key mathematics already checked by OpenSSL.
2. `PK_MLKEM768_::save(S &)` exports `ek` into bounded scratch, builds the
   SPKI data view, and calls `ZfASN1::save(s, data)` directly.
   `SK_MLKEM768_::save(S &)` emits PKCS#8 with the RFC-recommended compact
   seed form when OpenSSL retained the 64-byte seed, falling back to the
   2400-byte expanded-key form when it did not. The backend export reports
   which form and length it returned; the wrapper chooses the corresponding
   `ZfASN1` view. Neither path invents a seed or silently fails to save a
   generated key. `savePK(S &)` delegates to the public `save` method;
   `PK::savePEM`, `PK::saveB64`, and `PK::saveFile` then work unchanged.
   Use `ZmScratch` for fixed format-sized DER/key intermediates with heap
   fallback; do not use a new owning container or zero-fill before overwrite.
   Check that both DER and PEM encodings fit the existing `DERBufSize` and
   `BufSize` contracts; change those limits only if measured encoding length
   requires it. Keep private material out of logs and cleanse private
   scratch when its lifetime ends.
3. Extend `Load_::keyType` in `ZtlsPK.cc` and the `LoadPK`/`LoadSK`
   PKCS#8/SPKI dispatches in `ZtlsPK.hh` for the ML-KEM-768 OID. Reuse
   `Load::loadB64`, `loadPEM`, and `loadFile`; do not create parallel KEM
   file/PEM readers. Since `ZtlsKEM.hh` includes `ZtlsPK.hh`, avoid a header
   cycle: declare two narrow ML-KEM factory functions returning
   `ZuUnion<ZmRef<AnyKey>, ZeException>` in `ZtlsPK.hh`, define them in a
   `ZtlsKEM.cc` that includes `ZtlsKEM.hh`, and call them from the existing
   dispatches. Public factory input is `ek`; private factory input is the
   selected seed or expanded key. For the `both` CHOICE, prefer the seed as
   OpenSSL does; do not make a redundant consistency pass over the expanded
   key. Unknown algorithms retain the existing loader behavior.

COSE/CBOR is completely out of scope.

## Backend implementation

1. In `ZtlsBackend.hh`, declare only the matching functions with the current
   backend naming and span-passing style:

   ```cpp
   ZtlsAPI bool pkey_mlkem768_generate(PKey *);
   ZtlsAPI bool pkey_mlkem768_import_seed(PKey *, ZuBSpan seed);
   ZtlsAPI bool pkey_mlkem768_import_private(PKey *, ZuBSpan prvKey);
   ZtlsAPI bool pkey_mlkem768_import_public(PKey *, ZuBSpan pubKey);
   // Returns 64 seed bytes when available, otherwise 2400 expanded-key bytes.
   ZtlsAPI bool pkey_mlkem768_export_private(
     const PKey *, ZuSpan<uint8_t> prvKey, size_t *length);
   ZtlsAPI bool pkey_mlkem768_export_public(
     const PKey *, ZuSpan<uint8_t> pubKey);
   ZtlsAPI bool pkey_mlkem768_encapsulate(
     const PKey *, ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret);
   ZtlsAPI bool pkey_mlkem768_decapsulate(
     const PKey *, ZuBSpan ciphertext, ZuSpan<uint8_t> secret);
   ```

   Keep the public `ZeException` translation in `ZtlsKEM.hh`, like the
   neighboring PK wrappers; backend functions return `bool`. The only
   duplicated boundary checks should be those needed to keep direct backend
   callers safe.
2. In `ZtlsOpenSSL.cc`, use the provider name `"ML-KEM-768"`. Generate with
   `EVP_PKEY` keygen. For seed import, set
   `OSSL_PKEY_PARAM_ML_KEM_SEED` on the keygen context before keygen; it is
   not an expanded private key or an Ed25519-style raw private key. Import
   expanded private `dk` through `EVP_PKEY_fromdata` with
   `OSSL_PKEY_PARAM_PRIV_KEY`, and public `ek` through
   `EVP_PKEY_fromdata` with `OSSL_PKEY_PARAM_PUB_KEY`. Export them with
   `EVP_PKEY_get_octet_string_param` for the corresponding parameter. On
   private export, first try `OSSL_PKEY_PARAM_ML_KEM_SEED`, then fall back
   to `OSSL_PKEY_PARAM_PRIV_KEY`; report the actual length, so the ASN.1
   wrapper uses the right CHOICE tag. Use
   `EVP_PKEY_CTX_new_from_pkey`, `EVP_PKEY_encapsulate_init` /
   `EVP_PKEY_encapsulate`, and `EVP_PKEY_decapsulate_init` /
   `EVP_PKEY_decapsulate`. Pass the caller's output pointers directly to
   OpenSSL, with initialized `size_t` capacities; check returned lengths.
   Do not implement ML-KEM, SHA-3, or a KEM combiner.
3. Follow `replace_pkey_`: build a candidate key, free it on failure, and
   replace the existing key only after success. Free each `EVP_PKEY_CTX` on
   every path. Do not introduce a container, extra key copy, heap scratch,
   or a custom OpenSSL error object. A syntactically altered, correctly sized
   ciphertext may decapsulate successfully under ML-KEM implicit rejection;
   do not mistake that for successful authentication of an application
   message. Invalid ciphertext length, unavailable provider, or OpenSSL
   failure is an operation error. Treat any written secret as unusable on
   failure.
4. Require OpenSSL 3.5 or later in configuration, checking the actual link
   dependency on both Linux and Windows/MSYS2 before landing the API. The
   existing `zpicotls-openssl` pkg-config check alone does not establish an
   OpenSSL minimum. Fail configuration clearly if the provider/API is absent;
   do not silently ship stub methods or substitute another KEM. Keep current
   build flags and configuration otherwise unchanged.

## Build and verification

- Add `ZtlsKEM.hh` to `ztls/src/Makefile.am`'s installed headers,
  `ZtlsKEM.cc` to the library sources, and a
  focused `ztls/test/ZtlsKEMTest.cc` TAP binary to `ztls/test/Makefile.am`.
  Implement the header with direct includes and the standard modelines,
  copyright, include guard, and `ZtlsLib.hh` preamble from `GUIDELINES.md`.
  Put non-template OpenSSL work in `ZtlsOpenSSL.cc` and the loader factories
  in `ZtlsKEM.cc`; keep generic `save(S &)` methods in the header.
- Test generated and seed-imported pairs; public export/import and `mkPK`;
  public SPKI and private PKCS#8 DER/PEM save→load round trips; `LoadPK` and
  `LoadSK` dispatch; seed-only, expanded-only, and both private-key input;
  seed-preferred and expanded-key-fallback output;
  `savePK`; OpenSSL CLI reading the emitted keys and `ZtlsPK` loading keys
  emitted by OpenSSL. Also test encapsulate/decapsulate equality, independent
  encapsulations, and exact public, seed, expanded-key, ciphertext, and
  output-span sizes. Reject wrong-length public bytes; leave same-length
  byte-content checks to OpenSSL. For a modified valid-length ciphertext,
  assert only that it does not recover the
  sender's secret, not that decapsulation must fail. Cross-check against a
  FIPS 203/OpenSSL vector or independent implementation if a reliable vector
  exists; keep deterministic entropy confined to test code.
- Batch source changes, rebuild incrementally with the current artifacts:
  `ztls/src` before `ztls/test`, then dependent targets at the end. Do not
  reconfigure to different compilers, platforms, or debug/release flags as a
  substitute for fixing the current build. Run the focused TAP test on Linux
  and Windows/MSYS2. Use Valgrind sparingly, as a final Linux leak/corruption
  gate or to diagnose observed corruption.
- Audit every changed line against `GUIDELINES.md`'s repair procedure and
  flags: direct dependencies; concrete `ZmHeap` type identity; no extra
  dispatch/container/allocation; direct KEM output without copies;
  `ZmScratch` for bounded key/DER intermediates; no pre-zeroing before
  overwrite; contract-based size checks, not redundant key-byte
  revalidation; and only the `ZtlsPK` changes required for loader dispatch.

Completion means the public and private types expose the specified operations,
the same encapsulation's ciphertext decapsulates to its sender's shared
secret, and the feature builds and tests with a verified OpenSSL provider on
both supported platforms.

References: [FIPS 203](https://csrc.nist.gov/pubs/fips/203/final),
[OpenSSL ML-KEM key management](https://docs.openssl.org/3.6/man7/EVP_PKEY-ML-KEM/),
[OpenSSL encapsulation](https://docs.openssl.org/3.6/man3/EVP_PKEY_encapsulate/),
[RFC 9935 key encoding](https://www.rfc-editor.org/rfc/rfc9935.html).
