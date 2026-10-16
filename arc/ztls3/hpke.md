# Reusable `ZtlsHPKE` base-mode precursor

Implement the HPKE work needed by [`age.md`](age.md) as a reusable `Ztls`
capability, not inside the age header or stanza parser. The first supported
combination is [RFC 9180](https://datatracker.ietf.org/doc/html/rfc9180)
base mode with the [MLKEM768-X25519 hybrid KEM](https://filippo.io/hpke-pq)
(`kem_id = 0x647a`), HKDF-SHA256 (`kdf_id = 0x0001`), and
ChaCha20-Poly1305 (`aead_id = 0x0003`). Reuse the implemented raw KEM in
[`kem2.md`](kem2.md), itself dependent on [`x25519.md`](x25519.md).
This plan does not implement age stanzas, other KEMs or suites, PSK/auth
modes, exporter APIs, or a general-purpose HPKE framework.

## Boundary and API

- Keep HPKE in its own dependent header/source, **not** in
  `ZtlsKEM.hh`. `ZtlsHPKE.hh` includes `ZtlsKEM.hh` for the concrete
  hybrid key types and sizes; `ZtlsKEM.hh` must not include HPKE.
  KEM-only users therefore do not acquire HPKE's key-schedule/AEAD
  interface, and the dependency graph remains one-way.
- Add `ztls/src/ZtlsHPKE.hh` and `.cc`. Expose a narrow base-mode
  `sealBase` / `openBase` API in `Ztls`, taking the concrete
  `Ztls::PK::PK_MLKEM768_X25519` or `SK_MLKEM768_X25519`, borrowed
  `info` and AAD spans, borrowed plaintext/ciphertext, and caller-owned
  mutable output spans. The sender also receives `Ztls::Random &` in the
  existing KEM convention. Return `ZuUnion<void, ZeException>` from
  `sealBase` and `ZuUnion<size_t, ZeException>` from `openBase`, where
  the size is the authenticated plaintext length within its caller-owned
  output capacity. No age label, file-key length, Bech32, base64, or
  stanza type belongs in this API.
- `sealBase` writes the KEM's `HybridCiphertextSize` encapsulated key and
  the AEAD ciphertext plus its 16-byte tag directly to caller storage.
  `openBase` consumes those bytes and writes plaintext directly to caller
  storage. Use `HybridSecretSize` for the raw KEM shared secret. Rely on
  the KEM's exact encapsulated-key extent check; check HPKE output
  capacity, authentication, and crypto-provider results at this boundary.
  Clear any plaintext written before an error; do not expose it as valid
  until authentication succeeds.
  Borrowed inputs and secret state are not retained across calls.
- One-shot SealBase/OpenBase is sufficient for the first consumer. Do not
  add a persistent, polymorphic, multi-message HPKE context merely to
  model RFC 9180's general context API. The `info`/AAD parameters and
  separation from age make these operations reusable by other clients.

## Implementation decision

1. Inspect the installed zpicotls `ptls_hpke_setup_base_s/r` API and its
   `ptls_hpke_kem_t` / `ptls_key_exchange_algorithm_t` ownership contract.
   First test whether the existing raw hybrid KEM can be adapted without
   changing the RFC 9180 wire result or adding avoidable malloc-owned
   public-key/secret copies. Do not substitute zpicotls's TLS hybrid key
   exchange or its X25519-only HPKE KEM for MLKEM768-X25519. If a sound
   adapter fits, keep it inside `ZtlsHPKE` and reuse zpicotls setup.
2. If the adapter does not fit, implement only the base-mode key schedule
   in `ZtlsHPKE`, using the existing backend-independent zpicotls HKDF and
   AEAD operations and a narrow `Ztls::Backend` algorithm selector where
   the concrete zpicotls algorithm is OpenSSL-specific. Do not call
   `EVP_*`, `OSSL_*`, or `ptls_openssl_*` from `ZtlsHPKE`. Do not add a
   second crypto library or hand-code SHA-256, ChaCha20, or Poly1305.
3. Feed `PK_MLKEM768_X25519::encapsulate` / `SK_MLKEM768_X25519::decapsulate`
   directly into HPKE's base-mode key schedule. Their 32-byte output is
   already the hybrid KEM shared secret: do **not** apply RFC 9180
   DHKEM's ExtractAndExpand to it or repeat SHAKE256, X25519 agreement,
   or the SHA3-256 hybrid combiner. Use RFC 9180's `HPKE-v1` labeled
   extract/expand, base-mode byte `0x00`, empty PSK/PSK ID, and suite ID
   `"HPKE" || I2OSP(0x647a, 2) || I2OSP(0x0001, 2) ||
   I2OSP(0x0003, 2)`. Derive the 32-byte AEAD key and 12-byte base
   nonce; the one-shot operation uses sequence number zero. If the
   schedule is implemented here rather than delegated to zpicotls, do
   not derive or store an unused exporter secret.
4. Keep the `info` and AAD arguments independent. `info` enters the HPKE
   key schedule; AAD enters only AEAD Seal/Open. Use the same ordered
   bytes and suite IDs on sender and recipient. Write directly into caller
   outputs and use fixed-size contract-bound stack arrays for keys,
   nonces, and KEM secret. For any variable-length labeled-HKDF input
   that zpicotls requires contiguous, use tagged stack-backed scratch
   with heap fallback; do not build or retain a whole-message copy.
   Clear secret scratch on all exits with `ZuGuard` / `ZuClear`. Preserve
   in-place AEAD operation where the selected API supports it.

## Verification and integration

- Add `ztls/test/ZtlsHPKETest.cc` to the TAP test target. Check
  independent [hybrid HPKE vectors](https://github.com/FiloSottile/hpke/blob/main/testdata/hpke-pq.json)
  for recipient opening, ciphertext authentication, and plaintext; the
  vector implicitly checks the key schedule without a public key/nonce
  getter. If KEM randomness cannot be injected through the existing
  API, do not add a production deterministic-encapsulation API solely
  for tests. Exercise randomized sender round trips against the
  vector-validated recipient path; age CLI interop later verifies the
  age-specific sender composition. Include nonempty, distinct
  `info` and AAD; round trips with empty and binary plaintext; wrong
  `info`/AAD; truncated encapsulated key, ciphertext, or tag; and
  output-too-small/failure-clearing behavior.
- Add the public header and source to `ztls/src/Makefile.am`. Keep
  `ZtlsHPKE` independently testable without age parsing or a `.age` file.
  Follow `GUIDELINES.md` for direct includes, hard tabs, concise names,
  concrete heap identification only where allocation is necessary,
  contract-bound stack scratch, minimal copies, and no redundant
  validation. Reuse the current build configuration and artifacts;
  batch source changes, rebuild `ztls/src` before `ztls/test`, defer
  dependents, and use Valgrind sparingly as a final leak/corruption gate.

Completion requires a reusable base-mode seal/open boundary that
interoperates with independent MLKEM768-X25519 HPKE data. It does not by
itself claim age file-format interoperability.
