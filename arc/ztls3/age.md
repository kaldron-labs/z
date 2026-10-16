# `ZtlsAge` precursor implementation plan

Implement binary age v1 files as `Ztls_::Age`, with `using ZtlsAge =
Ztls_::Age` outside the namespace. This is the file-format precursor for
[`ZtlsVault`](goal.md), not a second credential store. Support **all four**
agreed recipient profiles, for encryption and decryption:

| Rank | Profile | Adoption judgment |
| --- | --- | --- |
| 1 | Classic native X25519 (`age1…`) | Longstanding standard key-pair workflow; likely the largest installed base. [age documentation](https://github.com/FiloSottile/age/blob/main/README.md) |
| 2 | scrypt passphrase (`age -p`) | Prominent built-in ad-hoc workflow. [age documentation](https://github.com/FiloSottile/age/blob/main/README.md) |
| 3 | SSH RSA and Ed25519 keys | Reuses existing SSH keys, but age recommends native keys when available. The relative adoption of ranks 2 and 3 is uncertain. [age manual](https://github.com/FiloSottile/age/blob/main/doc/age.1.ronn) |
| 4 | Hybrid post-quantum ML-KEM-768 + X25519 (`age1pq1…`) | Recommended for many new applications, but built-in support arrived in age v1.3.0; its installed-file base is likely smaller. [age manual](https://github.com/FiloSottile/age/blob/main/doc/age.1.ronn), [release notes](https://github.com/FiloSottile/age/releases) |

The ranking is an adoption judgment, **not** implementation priority or an
excuse to omit a profile. The wire reference is the versioned
[C2SP age specification](https://c2sp.org/age@v1.1.0), including its native
X25519, scrypt, hybrid, header, and payload sections. For SSH recipients,
follow the [age SSH implementation](https://github.com/FiloSottile/age/blob/main/agessh/agessh.go)
and age manual. For the hybrid, also cross-check the
[age hybrid implementation](https://github.com/FiloSottile/age/blob/main/pq.go)
and the [MLKEM768-X25519 HPKE construction](https://filippo.io/hpke-pq). The SSH
profile contains **both** `ssh-rsa` and `ssh-ed25519`; it is not a generic
synonym for native X25519.

## Scope and dependencies

- Implement native X25519, scrypt, SSH RSA/Ed25519, and
  `mlkem768x25519` recipient stanzas. Decode a header containing multiple
  supported recipients, selecting a matching identity; encryption may emit
  multiple compatible recipients wrapping one fresh file key. Do not reject
  an otherwise usable file merely because another stanza is unrecognized.
  Do not **emit** a file that mixes post-quantum and classical recipients:
  doing so forfeits post-quantum security. Do not reject such an otherwise
  interpretable file solely for that reason; C2SP says SHOULD NOT, not MUST
  NOT. In contrast, enforce scrypt's mandatory single-stanza rule on both
  read and write. No plugins, tagged hardware
  recipients, age CLI, or vault inner schema are in this precursor.
- `age.md` **depends on [`x25519.md`](x25519.md), [`kem.md`](kem.md),
  [`kem2.md`](kem2.md), and [`hpke.md`](hpke.md)**. All four precursors are
  implemented: raw X25519 derivation/agreement and RFC 8410 load/save,
  raw FIPS 203 ML-KEM, the 32-byte-seed MLKEM768-X25519 hybrid KEM,
  and reusable HPKE base-mode Seal/Open over that hybrid KEM. None
  implements age's recipient stanzas.
  Do not treat an ML-KEM PKCS#8 key, a raw 64-byte ML-KEM seed, or
  `PK_MLKEM768::encapsulate` alone as an `age1pq1` identity.
- Use `Ztls::HPKE::sealBase` / `openBase` from `ZtlsHPKE.hh` per
  [`hpke.md`](hpke.md). `ZtlsAge` supplies the age-specific `info`, empty
  AAD, stanza encoding, and recipient-mixing policy; it does not own an
  RFC 9180 key schedule or call the raw hybrid KEM for stanza wrapping.
- Add no Rust, FFI bridge, external age library, second crypto library, or
  in-process invocation of the age CLI. `ZtlsAge` may use the existing
  backend-independent zpicotls API directly alongside `Ztls` capabilities.
  It must not include OpenSSL headers or call `EVP_*`, `OSSL_*`, or other
  OpenSSL APIs directly. Keep OpenSSL-specific operations and zpicotls
  algorithm selectors behind the switchable `Ztls::Backend`; add only the
  narrow backend accessors or operations the inventory identifies. Do not
  create a broad generic crypto facade or reimplement cryptographic primitives.
- Conformance to age v1 is required because the output is named `.age`.
  Interoperability with an existing Codex vault *inner schema* is not
  required. The vault may choose the scrypt profile for its own files, but
  that does not narrow `ZtlsAge`'s four-profile contract.

### Capability inventory and unmet dependencies

| Required operation | Existing framework capability | Required work before `ZtlsAge` uses it |
| --- | --- | --- |
| Random bytes, SHA-256/SHA-512, HMAC-SHA-256, constant-time comparison, base64 | `Ztls::Random`, `Ztls::MD`, `Ztls::HMAC`, `Ztls::ctEqual`, `ZuBase64` | Reuse. Trim encoder padding and check canonical unpadded base64 at the age parsing boundary; `ZuBase64` decoding alone is permissive. |
| scrypt with age's salt, `logN`, `r = 8`, `p = 1`, bounded memory | OpenSSL is linked, but `Ztls::secretHash` fixes a different application-specific profile | Add a narrow parameterized `Ztls` scrypt operation. Do **not** call `EVP_PBE_scrypt` directly from `ZtlsAge` or repurpose `secretHash`. |
| HKDF-SHA-256 and ChaCha20-Poly1305 AEAD | zpicotls exposes `ptls_hkdf_extract/expand`; `Ztls::Backend::hash_algorithm(SHA256)` and `Ztls::Backend::chacha20poly1305()` select the algorithms; `Ztls::Pico::AeadCtx` owns a pooled direct-key/IV AEAD context. The HPKE test verifies in-place AEAD use through this path. | Reuse these capabilities for age's non-HPKE wrapping and payload. Pass age's specified key, IV/nonce, sequence, and AAD; verify the age-specific wire results. Do not use the TLS cipher-suite descriptor as an AEAD operation or add a redundant EVP implementation. |
| Native and ephemeral X25519 public derivation/agreement | `Ztls::PK::PK_X25519` and `SK_X25519` in `ZtlsPK.hh` accept raw 32-byte keys; `exportPK`/`exportSK` write caller-owned spans, and `SK_X25519::agree` writes a 32-byte shared secret. The backend rejects failed/all-zero agreement. RFC 8410 load/save is also implemented. | Reuse the raw APIs for native recipients and identity text; do not round-trip through RFC 8410 to obtain age key bytes. The SSH-Ed25519 conversion/tweak remains separate work. |
| Bech32 native and PQ recipient/identity text | `ZuBase32` is **not** Bech32; no Bech32 facility was found | Add a narrowly scoped Bech32 codec in the appropriate Z foundation layer, or explicitly keep the Age text-key encoder/decoder format-local. Do not substitute ordinary Base32. |
| SSH RSA OAEP-SHA256, SSH-Ed25519 conversion/tweak, SSH key encodings | `ZtlsPK` has RSA/Ed25519 signing and ASN.1 key load/save, but not these age operations or OpenSSH private-key parsing | Normalize RSA OAEP-SHA256 and required raw-key operations in `Ztls`. The Ed25519-to-X25519 public conversion/tweak needs a verified implementation, not merely an OpenSSL function rename. Implement the age SSH wrapping rules and key-format parsing without mistaking signatures for encryption. Support authorized-key public input and unencrypted OpenSSH private keys. Password-protected OpenSSH private keys are out of scope. |
| Hybrid MLKEM768-X25519 KEM | `Ztls::PK::PK_MLKEM768_X25519` and `SK_MLKEM768_X25519` in `ZtlsKEM.hh` implement the 32-byte-seed hybrid with normalized SHAKE256/SHA3-256, a 1216-byte raw public key, a 1120-byte encapsulated key, and a 32-byte shared secret. | Construct the concrete hybrid keys from `HybridPublicSize` raw public bytes or `HybridSeedSize` identity bytes, then pass them to `Ztls::HPKE`; do not rebuild the hybrid from standalone ML-KEM and X25519 or invoke raw `encapsulate`/`decapsulate` in the stanza path. |
| HPKE SealBase/OpenBase for the hybrid stanza | `ZtlsHPKE.hh` implements `Ztls::HPKE::sealBase` / `openBase` for the concrete hybrid keys, arbitrary `info`/AAD, caller-owned encapsulated-key and body spans, and a public `Ztls::HPKE::TagSize`. Independent vector, round-trip, and in-place tests pass. | Pass age's `info`, empty AAD, 16-byte file key, `HybridCiphertextSize` encapsulated-key span, and `16 + Ztls::HPKE::TagSize` body span to the public API. Do not duplicate KEM or HPKE internals in `ZtlsAge`. |

This inventory is a dependency gate, not permission to implement every
OpenSSL primitive anew. Recheck the actual `Ztls` and zpicotls APIs before
coding; if a capability has since been added, use it and delete the
corresponding planned extension. No OpenSSL-specific symbol belongs in
`ZtlsAge.cc`.

With `x25519.md`, `kem2.md`, and `hpke.md` implemented, the remaining
**`Ztls` backend gaps** are parameterized scrypt, RSA-OAEP-SHA256 with
age's label, and the SSH-Ed25519 conversion's required raw-key/curve
operations. HKDF and AEAD operations already have backend-independent
zpicotls APIs; the OpenSSL-specific ChaCha20-Poly1305 selector is already
normalized as `Ztls::Backend::chacha20poly1305()`. Raw X25519 import,
public derivation, agreement, hybrid SHAKE256/SHA3-256, and hybrid HPKE
are already implemented; do not implement them again. The age SSH
Ed25519 conversion still needs a verified construction; OpenSSL does not
supply the whole conversion as one API.
Bech32, authorized-key/OpenSSH parsing, and canonical base64 are format
work, not OpenSSL normalizations. Existing
`Ztls::Random`, `MD<SHA256/SHA512>`,
`HMAC<SHA256>`, and `ctEqual` need no duplicate wrappers.

## API and ownership

- Add installed `ztls/src/ZtlsAge.hh` and `ztls/src/ZtlsAge.cc`. Define one
  `ZtlsAPI` `Ztls_::Age` type and the external `ZtlsAge` alias. Keep
  wire helpers and profile dispatch private. Expose typed, borrowed
  recipient/identity inputs sufficient for all four profiles, including a
  caller-supplied span for multiple recipients and identities; do not force
  text key parsing or dynamic allocation on callers that already hold binary
  keys. Keep the scrypt passphrase form explicit. Do not overload a generic
  byte string with four ambiguous meanings.
- Provide synchronous encryption from borrowed plaintext to a caller-opened
  `ZiFile`, and decryption from a caller-opened `ZiFile` into a caller-owned
  mutable byte span, returning plaintext length or `ZeException`. The caller
  controls paths, permissions, publication, and identity/passphrase lifetime.
  An `Age` instance is owner-thread-confined and retains no secrets or
  operation state between calls. No scheduler, lock, cache, virtual
  interface, or heap-allocated operation object is needed.
- Stream encrypted bytes to `ZiFile`; read ciphertext into bounded scratch
  and decrypt directly into the caller's uninitialized destination. Do not
  expose plaintext as valid until header MAC, all chunk tags, and final-chunk
  marker have passed. Clear bytes written to the destination on failure;
  the caller clears successful plaintext after use. Do not choose paths or
  rename files inside `Age`.

## Wire implementation, in dependency order

1. Implement one common age v1 binary header/payload engine. Generate a
   fresh 16-byte file key and 16-byte payload nonce for every encryption.
   Use named specification-derived constants and small primitive arrays for
   fixed-size secrets. Use `ZuClear` for secret scratch on all exits. The
   common header contains one or more recipient stanzas, then an HMAC-SHA-256
   over exactly the C2SP prefix through `---`; derive its key with
   HKDF-SHA-256 and verify with `Ztls::ctEqual`. Emit canonical unpadded
   base64 and parse bounded lines/stanzas with Z spans. Unknown stanzas are
   skipped, while malformed input and a mixed scrypt header fail. Bound total
   header size and recipient count as explicit resource policy, not as
   arbitrary per-profile correctness checks.
2. Implement profile-specific wrapping/unwrapping behind that one stanza
   engine. For scrypt, use age's salt and specified KDF label, a fresh salt,
   a named measured encryption work factor, and a bounded accepted `logN`
   before KDF execution. For native X25519, use a fresh ephemeral scalar per
   stanza, the specified salt/info, HKDF-SHA-256, and zero-nonce
   ChaCha20-Poly1305 wrapping. Construct `SK_X25519` from
   `Ztls::Random &` for a fresh native ephemeral key,
   `exportPK` for its 32-byte public value, and `agree` with the raw
   recipient public bytes; do not allocate a `PK_X25519` merely to pass a
   peer key into agreement; propagate failed/all-zero agreement from the
   backend without adding a second point-validation layer. For SSH RSA
   and Ed25519, match the age
   reference's fingerprint, labels, RSA-OAEP or Ed25519-to-X25519
   conversion/tweak, and stanza encodings.
   For hybrid, construct `PK_MLKEM768_X25519` from exactly
   `HybridPublicSize` raw recipient bytes or `SK_MLKEM768_X25519` from
   exactly `HybridSeedSize` identity bytes. Do not call their raw
   `encapsulate`/`decapsulate` methods in Age: `Ztls::HPKE::sealBase`
   and `openBase` already do so and run the RFC 9180 base-mode schedule.
   Pass the concrete key, `info = "age-encryption.org/mlkem768x25519"`,
   empty AAD, and the 16-byte file key to those operations. Allocate
   caller-owned spans of `HybridCiphertextSize` for `enc` and
   `16 + Ztls::HPKE::TagSize` for the body; `openBase` returns the
   authenticated plaintext length, which must be the file-key length.
   Age encodes the 1120-byte `enc` as the stanza argument and the
   16-byte encrypted file key plus 16-byte AEAD tag as its 32-byte body.
   Verify exact key/stanza encodings against independent vectors; do not
   infer them from the ML-KEM PKCS#8 layout.
3. Derive the payload key with HKDF-SHA-256 from the file key and payload
   nonce. Process 64-KiB chunks with ChaCha20-Poly1305. The 12-byte nonce is
   an 11-byte big-endian counter plus a final-chunk byte. Empty plaintext
   emits a final empty chunk; an exact 64-KiB multiple marks its last full
   chunk final. Reject missing final marker, bad tag, counter overflow, and
   trailing bytes. Re-encryption always generates a fresh file key and nonce.
   Keep parsing liberal where bytes remain interpretable, but never accept a
   wire-format mismatch or failed authentication.

## Storage and framework discipline

- Use `ZiFile` for file-handle I/O. The vault owns same-directory temporary
  creation, sync, rename, and interprocess coordination. On failure, `Age`
  leaves the caller's output unpublished.
- Use one reusable chunk buffer (pooled `ZiIOBuf` where its API fits) and
  `ZtScratch` with tagged heap fallback for bounded header/stanza scratch.
  Do not allocate a second whole-file ciphertext or plaintext container.
  Operate in place where the zpicotls AEAD API permits it. Account for
  OpenSSL-owned contexts inside the `Ztls` backend, not in the age parser.
- Follow `GUIDELINES.md`: direct includes, concise names, no unnecessary
  casts, uninitialized destination storage, and no secret bytes in logs or
  error strings. Validate C2SP structure, authentication, and resource
  bounds, not application policy already guaranteed by types.

## Build and acceptance

- Add the Age header/source to `ztls/src/Makefile.am` and a focused
  `ztls/test/ZtlsAgeTest.cc` TAP binary. Reuse the implemented
  [`kem.md`](kem.md), [`x25519.md`](x25519.md), [`kem2.md`](kem2.md),
  and [`hpke.md`](hpke.md) precursors; complete the other remaining
  capabilities before claiming all four profiles. No new external crypto
  dependency is planned; if a required primitive cannot be
  provided by zpicotls or the switchable `Ztls::Backend`, report that blocker
  rather than importing another library or implementing crypto by hand.
  Keep the current build configuration and artifacts, batch source changes,
  rebuild `ztls/src` before `ztls/test`, and defer dependents to the end.
- The independent `ZtlsHPKE` vector, round-trip, and in-place tests
  already pass; keep that precursor as a gate while adding age stanza
  and file-format interoperability tests.
- Use published [age format vectors](https://age-encryption.org/testkit),
  [hybrid vectors](https://github.com/C2SP/CCTV), and age/SSH fixtures for
  every profile. Test both directions against the reference age CLI for
  X25519, scrypt, SSH RSA, SSH Ed25519, and hybrid recipients; include mixed
  compatible-recipient headers and unknown stanzas; verify writers avoid
  PQ/classical mixing, readers can interpret it, and scrypt mixing is
  rejected. Keep fixtures local so ordinary unit tests need no
  network or age CLI. Put live CLI interoperability tests in `interop` if
  added, per `GUIDELINES.md`.
- SSH private-key loading covers unencrypted OpenSSH and the existing
  `ZtlsPK`-supported private-key encodings. Password-protected OpenSSH
  private keys are explicitly out of scope; the scrypt age recipient profile
  remains fully in scope.
- Test empty and binary plaintext, 64-KiB boundaries and multi-chunk files,
  output-too-small, malformed/truncated headers and payloads, bad MAC/tag,
  missing final chunk, non-canonical base64/Bech32, wrong identity or
  passphrase, and excessive scrypt work factor. Verify failure clears
  produced plaintext and private scratch. Run the current Linux build and
  focused tests, then a sparse Valgrind leak/corruption gate. A Windows/MSYS2
  run is out of scope.

Completion requires interoperable read/write support for **all four**
profiles, not merely the vault's scrypt path. `ZtlsAge` may call
backend-independent zpicotls APIs directly, but no OpenSSL-specific call or
selector may escape the switchable `Ztls::Backend`.
