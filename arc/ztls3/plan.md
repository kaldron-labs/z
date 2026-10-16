# ZtlsVault implementation plan

This plan is authoritative for implementation decisions; [goal.md](goal.md)
records the application and module contracts and must agree with this plan.
`VaultStore` selects the backend; `VaultVariant` independently selects `Direct` or
`Indirect`. The defaults are `Direct` for every selected backend except Windows
`KeyRing`, where `Indirect` is mandatory. In particular, test `Indirect` with
`Ephemeral`, `File`, and `Module` without relying on an OS keyring.

## 0. Establish the implementation boundary

- Read `GUIDELINES.md` and any directory-specific guidance before editing.
  Preserve unrelated work. Use the current configured build and existing
  objects; batch edits, rebuild `ztls/src` before dependent tests, and defer
  top-level dependent rebuilding until integration.
- Check the actual `Ztls`/`Zi`/`Zdbus` APIs before coding. Reuse `ZiFile`,
  `ZiModule`, `Ztls::MD<SHA256>`, `Ztls::Random`, `ZtlsAge`, `ZtScratch`,
  `ZmBlock`, `ZdbusClient`, and `ZfDBUS`; do not create lookalike wrappers
  or modify those modules merely to make this implementation convenient.
  The one required normalization gap is `ZiFile::mkdir` on Windows, which
  currently ignores its mode argument: implement owner-only directory
  permissions there for the vault's `0700` request. Keep filesystem calls
  out of vault code on both platforms.
- Treat [`x25519.md`](x25519.md), [`kem2.md`](kem2.md),
  [`hpke.md`](hpke.md), and [`age.md`](age.md) as implemented precursors,
  not vault work to repeat. Their current source and focused tests are the
  integration baseline described below. `Indirect` uses the real age v1
  **scrypt recipient** through `ZtlsAge`; it does not implement another age
  header, KDF, HPKE schedule, or cipher. Keep Rust, FFI, external age
  libraries, and a second crypto library out of scope. The precursor's
  Linux build, local vectors, reference-age interoperability, and Valgrind
  gate have passed in the current configuration. A Windows/MSYS2 precursor
  run is out of scope; do not infer Windows verification from the Linux run.
- A vault session is short-lived: open, perform the needed loads/saves, and
  close; applications retain required credentials in their own memory, not
  by leaving the vault open. Each selected backend independently ensures
  the integrity of its own `load`/`save` calls. Hold one program-specific
  `<home>/vault/vault.pid` through `ZiPIDFile` before accessing either
  `<home>/vault/secrets.json` or `<home>/vault/secrets.age`; create
  `<home>/vault` on demand through `ZiFile` with mode `0700` (equivalent
  owner-only permissions on Windows) before acquiring the PID file.
  Native-only `Direct` must not create it. Acquire the guard during `open()` for an
  explicit `File` store or any `Indirect` variant, or lazily before `Auto`'s
  `File` fallback in a native-only `Direct` session. Hold it until `close()`;
  fail the acquisition on `Running` or `Error`, and release it on any later
  start failure. Do not acquire it twice for `Indirect + File`. Native-only
  `Direct` needs no PID file. Test contention, release, and crash recovery
  with two processes.

### Implemented precursor boundaries to reuse

- [`x25519.md`](x25519.md): `ZtlsPK.hh` provides raw 32-byte
  `PK_X25519`/`SK_X25519` import, public derivation, caller-span agreement,
  and RFC 8410 load/save through `ZfASN1`. OpenSSL-specific operations stay
  in `Ztls::Backend`; do not add another X25519 path in the vault.
- [`kem2.md`](kem2.md): `ZtlsKEM.hh` provides the 32-byte-seed
  `PK_MLKEM768_X25519`/`SK_MLKEM768_X25519` hybrid, with 1216-byte raw
  public keys, 1120-byte encapsulated keys, and 32-byte shared secrets.
  It builds on standalone ML-KEM and X25519; it is not an age identity
  serializer or an HPKE implementation.
- [`hpke.md`](hpke.md): `ZtlsHPKE.hh` separately exposes
  `Ztls::HPKE::sealBase`/`openBase` for that hybrid KEM, borrowed `info`/AAD,
  and caller-owned output spans. `ZtlsKEM.hh` does not depend on HPKE.
  Do not pull the HPKE key schedule into `Indirect` or `ZtlsAge`.
- [`age.md`](age.md): `ZtlsAge.hh` exposes `Ztls_::Age`/`ZtlsAge`, typed
  borrowed recipients and identities, `ZtlsAgeKeyType`, synchronous
  `encrypt(Random &, recipients, plaintext, ZiFile &)` and
  `decrypt(ZiFile &, identities, plaintext)` into caller storage. It already
  handles X25519, scrypt, SSH RSA/Ed25519, and hybrid recipients, plus age
  header/payload framing, Bech32, authorized-key input, and unencrypted
  OpenSSH private keys. Password-protected OpenSSH keys are out of scope.
  The age layer uses normalized `Ztls::Backend` operations and
  backend-independent zpicotls HKDF/AEAD, not direct OpenSSL calls. The
  vault owns paths, publication, passphrase storage, and its aggregate
  schema; `ZtlsAge` retains no operation state or secrets between calls.

The focused TAP binaries are `ZtlsX25519Test`, `ZtlsHybridTest`,
`ZtlsHPKETest`, and `ZtlsAgeTest`. The Age test includes local CCTV and
SSH fixtures; `ztls/interop/ztlsageinterop` is an optional reference-age
driver, not part of the ordinary TAP target. Reuse these gates when vault
integration changes their inputs or dependencies; do not duplicate their
format tests inside `ZtlsVaultTest`.

## 1. Land the frozen API and identity rules

- Add `ztls/src/ZtlsVault.hh`, `ZtlsVaultStore.hh`, and `ZtlsVault.cc` with the
  now-resolved lifecycle and backend declarations from `goal.md`. Give each header the normal
  Z header skeleton, direct includes, and installed-header entry. Keep private
  implementation state out of the application header. Do not introduce
  another plugin interface or application-visible error taxonomy.
- Implement the non-copyable, owner-thread-confined `Vault` with private `m_`
  state. Its constructor establishes no backend; `init(config)` copies and
  resolves `program`, `envPrefix`, `account`, `module`, and home once. `open()`
  acquires the PID file for `File` or `Indirect` before activating an aggregate
  backend,
  returning failures through `VaultResult`; `close()` drains and finalizes the
  backend and releases any PID file. A later `open()` may open another session
  with the retained configuration. `final()` requires the stopped state and releases
  configuration before the destructor. `load`/`save` outside the open session
  fail without implicit activation. Do not keep a session open as an
  application-lifetime credential cache; copy needed loaded values in the
  callback and close the vault promptly.
  Empty program comes from `ZiLog::program()`;
  uppercase only ASCII for the default environment prefix. Read
  `<PREFIX>_HOME`, otherwise `$HOME/.<program>`. Resolve a relative override
  against the `init()`-time working directory with `ZiFile`, and retain
  the same absolute path thereafter. Derive an empty account as
  `vault|<first-16-lowercase-hex-of-SHA256(home)>`. Do not make identity
  depend on whether the directory exists.
- At each call, reject empty secret names and empty environment names, and
  reject `/` in either UTF-8 component. Build exactly `global/<name>` or
  `env/<environment>/<name>` in one `ZtScratch` buffer with heap fallback,
  appending the component bytes unchanged. Do not percent- or hex-encode
  components or adopt Codex's ASCII-only name restriction. Pass this opaque
  key to the backend;
  never treat it as a path. Borrow input spans only for the call. Keep the
  callback's loaded byte view valid through its invocation and invoke it
  exactly once only on success, on the calling owner thread.
- Add initial `ZuTestUtil`/TAP tests for fixed key vectors (including UTF-8,
  literal percent, rejected slash, NUL, and empty-value cases), distinct scopes, stable
  home/account identity,
  relative home, callback lifetime, and invalid names. Check that diagnostics
  expose no program/account/scope/name/value/passphrase text.

## 2. Implement the common backend lifecycle and basic stores

- Implement the built-in `Ephemeral`, `File`, and platform `KeyRing` stores as
  concrete `Ztls_::VaultStore` classes; `Module` supplies another concrete
  instance, while `Auto` composes native-first and `File` fallback paths.
  The `Vault` owner holds the
  selected `ZmRef`, initializes it once per successful `open()`, calls the
  backend's `final()` during `close()` or after partial initialization failure,
  then releases it. A later `open()` constructs a new backend instance. A successful backend load
  invokes its callback synchronously once and retains no supplied spans.
- `Ephemeral`: one owner-thread `ZmHash` of intrusive key/value entries. Use
  a named fixed-node heap and tagged variable-size value storage. Replace
  in place where practical, clear the displaced value immediately, and clear
  everything at teardown. No locks, timer, second index, or hidden persistence.
- `File`: keep one unencrypted, versioned JSON aggregate per program at
  `<home>/vault/secrets.json`. Use `ZfStruct`/`ZfJSON` with account and composed
  key as separate map levels, and the JSON adapter's Base64 byte-field
  support for exact binary values, including zero-length. A missing file or
  record is missing; reject malformed JSON rather than overwriting it.
  Decrypt nothing and do not invoke `ZtlsAge` in this backend. Under the
  shared PID guard, load or start an empty aggregate, replace only the named
  record, then use `ZiFile` to write a same-directory exclusive temporary,
  sync and rename it over `secrets.json`; clean only this operation's temporary
  on failure. Use restrictive permissions and `NoFollow` on opens. The file
  relies on filesystem permissions for confidentiality; add no per-key
  files, filename digest, lock manager, or handwritten JSON/Base64 codec.
- Unit-test `Ephemeral` replacement and cleanup and `File` persistence,
  missing versus empty values, binary bytes, multiple accounts and records,
  malformed JSON, permissions, PID contention, replacement, and
  interrupted-write recovery before moving to `Indirect` encryption.

## 3. Implement `Indirect` as a variant over the selected store

- Keep `Direct` as a straight call-through to the selected store. Give
  `Indirect` one private adapter owning that same store; it uses the frozen
  virtual `load`/`save` functions only for one reserved passphrase key outside
  the `global/` and `env/` user-key space. Do not duplicate `KeyRing`,
  `File`, or `Module` implementations in the adapter.
- During an `Indirect` session holding its PID file, a read of a missing
  `secrets.age` returns missing without generating a passphrase. On the first
  write, follow the [Codex passphrase model](https://github.com/openai/codex/blob/main/codex-rs/secrets/src/local.rs):
  fill 32 bytes with `Ztls::Random`, encode them with standard **padded**
  `ZuBase64` into a 44-character ASCII passphrase, and save that text under
  the reserved key through the selected store. Use the stored text itself as
  the age scrypt passphrase; do not decode it, derive it from vault identity,
  or regenerate it for each record or write. Reuse that one passphrase for
  subsequent decrypt/encrypt operations, and make sure it remains
  authoritative before publishing the first ciphertext. Clear the random
  bytes and passphrase scratch on every exit path. Any store failure fails
  closed. This matches Codex's passphrase construction, not its keyring
  identity or file contents, which are not interoperability requirements.
- Represent the decrypted aggregate as unambiguous length-delimited binary
  key/value records; preserve empty and arbitrary binary values. Load:
  obtain the passphrase, open the age file with `ZiFile`, pass one
  `ZtlsAge::ScryptIdentity` to `ZtlsAge::decrypt`, parse only its returned
  authenticated plaintext length, and invoke the callback for the selected
  record. Save: decrypt existing content or start empty, replace only the
  selected record, pass one `ZtlsAge::ScryptRecipient` and the aggregate
  plaintext to `ZtlsAge::encrypt`, and publish the caller-owned
  same-directory `ZiFile` temporary through the vault's sync/rename path.
  `ZtlsAge` neither chooses the path nor publishes it. Do not overwrite
  malformed or unauthenticated content. Keep `ZtScratch` with tagged heap
  fallback for bounded scratch; use appropriately sized owned storage for
  variable-length aggregate plaintext and clear it and the passphrase on
  every exit path. Do not round-trip through JSON/CBOR, re-encode age
  headers, or copy a second whole-file body.
- Test `Indirect + Ephemeral` first, then `Indirect + File`, then a module
  fixture. Test multiple records, replacement without losing other records,
  the 32-random-byte/padded-Base64 passphrase form and its stable reuse,
  cross-instance recovery with persistent passphrase stores, missing versus
  empty, wrong passphrase, tampering, malformed aggregate, interrupted write,
  and a second `Indirect` process being denied file access until the first
  stops or exits.
  `Indirect + Ephemeral` is deliberately non-recoverable after teardown; test
  that without treating it as a failure
  of encryption/decryption. Rely on the precursor's published age vectors
  and reference interoperability for wire conformance, and test the vault's
  use of that API rather than compatibility with an existing Codex vault.

## 4. Implement platform `KeyRing` stores

- Linux: put platform code in a Linux-only source file, include
  `ZdbusClient.hh` there, and link in-tree `libZdbus` only on Linux. Define
  Secret Service request/response/signal bodies as `ZfStruct` with the `DBUS`
  facet; build and parse them with `ZfDBUS`. Use `ZdbusClient` for framing,
  serials, calls, errors, and signals. Open a `plain` session, use stable
  the program value in the existing `"service"` attribute, plus account/key
  attributes; search/unlock/get on load, and
  ReadAlias/CreateCollection/unlock/CreateItem(`replace=true`) on save.
  Subscribe to `Prompt.Completed` before prompting; handle immediate and
  prompted result paths, dismissal, timeout, and teardown exactly once.
  Preserve denial, locked items, missing items, and timeout as their actual
  outcomes; `Auto` decides fallback without an unavailability classification.
- Bridge async D-Bus operations to the synchronous vault with `ZmBlock` on
  the vault owner thread, never on either Zdbus shard. Return/timeout/error
  feed one continuation with result or `ZeException`; serialize completion
  on the owner shard, cancel the loser, and drain callbacks before teardown.
  Move `ZiIOBuf` references between shards rather than copying bodies.
  Close the Secret Service session on orderly teardown. The `plain` session
  does not encrypt bus traffic; do not claim otherwise.
- Windows: put Credential Manager calls in a Windows-only source file. Build
  one bounded UTF-16 `CRED_TYPE_GENERIC` target from program/account/key,
  use `CRED_PERSIST_LOCAL_MACHINE`, preserve empty binary values, and expose
  the `CredRead` blob only until `CredFree`. Reject oversize blobs without
  truncation. Windows `KeyRing + Direct` is rejected before storage calls;
  its default `Indirect` variant places only the short passphrase in Credential
  Manager.
- Exercise the Linux store against a controlled `ZdbusServer` peer in
  `ztls/itest` and an independent Secret Service in `ztls/interop`. Cover
  session open/close, binary values, replacement, missing, prompts, denial,
  timeout, and service loss. Add Windows KeyRing tests for normal operation,
  missing, blob limit, and a large vault value taking the `Indirect` path.

## 5. Wire `Module`, `Auto`, and the build

- Load `Module` through `ZiModule` with the exact factory symbol in the
  frozen contract. Reject an empty module path, missing symbol, null factory
  result, or failed `init()`. Load without `ZiModule::GC`; unload only before
  calling a factory, never once module code can remain reachable. Call
  `final()` before releasing the returned `ZmRef`. Build a small module
  fixture to test this lifecycle and both variants.
- Resolve `VaultVariant::Default` once from the preferred native store;
  Windows `KeyRing` selects `Indirect`, elsewhere `Direct`. Preserve that
  variant, or an explicit variant, on both `Auto` paths. Initialize native
  first; if its `init()` fails, initialize `File` instead. Otherwise try the
  native path on every load/save, lazily initializing `File` when fallback is
  first needed. A missing credential or any native load failure (including
  denial and timeout) tries `File`; any native save failure tries `File`.
  Native success wins even if a fallback file exists. Return the fallback
  error if it also fails. Before an `Auto + Direct` operation first falls
  back to `File`, acquire the same `vault.pid` used by `Indirect` and hold it
  until `close()`. Do not acquire it twice for `Auto + Indirect + File`.
  No error-category wrapper or exception-text parsing is needed.
- Update `ztls/src/Makefile.am`, `ztls/test/Makefile.am`, `ztls/Makefile.am`,
  and `configure.ac` only as needed for new sources, Linux-only `libZdbus`,
  and vault `itest`/`interop` directories. `ZtlsAge.hh/.cc`, its TAP target,
  and the optional age interoperability driver are already wired into the
  current build; do not add them again. Keep Windows free of
  `libZdbus`, GLib, and `libsecret-1`; follow existing Automake `LINUX`
  conditionals. Install only the two public vault headers, not private
  implementation headers.

## Verification and completion gates

1. At each major milestone, rebuild `ztls/src` with `make -C ztls/src -j8`,
   then the affected test target with `make -C ztls/test -j8`; reuse current
   objects and configuration. Regenerate build files only when build inputs
   change. No other compiler, build flag, or platform configuration is
   introduced merely for an intermediate check.
2. Run TAP unit tests in `ztls/test`, controlled-peer tests in `ztls/itest`,
   and independent Secret Service tests in `ztls/interop`. Keep the
   precursor's local age vectors and reference-age interop as format gates;
   vault tests should additionally prove that its `Indirect` files can be
   opened by `ZtlsAge`, without reimplementing age tests. Skip only genuinely
   unavailable external services, with a diagnostic. The Age precursor's
   Windows/MSYS2 run is out of scope for this plan update; run vault Windows
   build/tests in a Windows environment before declaring cross-platform
   vault completion.
3. After dependent integration, run the required top-level `make -j8` and
   `make test`. Use `libtool exec` for debugger/Valgrind runs; use Valgrind
   sparingly for suspected corruption and as the final leak/corruption gate.
   Review plaintext and passphrase lifetime, callback/shard teardown, every
   allocation/copy, and every changed line against `GUIDELINES.md`.
4. Completion means the frozen contracts remain intact; the full
   store × variant matrix works where permitted; age-format and
   concurrent first-write tests pass; `Auto` follows native-first per-call
   fallback on missing credentials and native failures; and no secret or
   identity appears in diagnostics.

## Open questions and ambiguities

Question IDs are permanent. Append new questions with the next unused ID;
when one is resolved, keep its number and record the decision beneath it.
Do not renumber or silently delete resolved questions. Resolved answers here
are implementation decisions that `goal.md` must reflect; unresolved
questions do not authorize speculative API changes.

1. **Q001 — `Vault` object lifetime and hidden state. Status: resolved.**
   `Vault` is a non-copyable class with private `m_` state. It follows the
   owner-thread, `ZmEngine`-aligned lifecycle: constructor, `init(config)`
   (configuration load), `open()`, restartable `close()`/`open()`, `final()`
   (stopped precondition), destructor. Backend activation belongs to
   `open()`, not a first `load`/`save`. The explicit answer authorizes the
   corresponding change to the final API and application contract in
   `goal.md`; do not replace private state with an external side table.
   Each `open()`/`close()` pair is a short access session, not the
   application's credential-retention lifetime. `File` and `Indirect`
   aggregate access share the one PID file specified in Q004.

2. **Q002 — Meaning of Codex compatibility. Status: resolved.** Codex is a
   reference implementation, not a data-interop target. `ZtlsVault` defines
   its own aggregate schema aligned with Codex's structural and security
   requirements; it need not read or write existing Codex `secrets.age`
   files, reproduce Codex's passphrase-store identity, or migrate them. The
   `.age` extension still denotes the age format required by `goal.md`.

3. **Q003 — Age implementation. Status: resolved and implemented.**
   `Ztls_::Age`/`ZtlsAge` under [age.md](age.md) reads and writes all four
   agreed profiles, including the vault's scrypt profile. It uses
   `Ztls::Backend` for OpenSSL-specific operations and backend-independent
   zpicotls for HKDF/AEAD; it implements age framing, not a custom cipher
   or KDF. No Rust, FFI bridge, external age library, or new crypto
   dependency was added. The current Linux configuration has passed its
   local vectors, reference-age interop, and Valgrind gate; Windows/MSYS2
   precursor verification remains out of scope.

4. **Q004 — Aggregate-file coordination. Status: resolved.** The program-
   specific files `<home>/vault/secrets.json` and
   `<home>/vault/secrets.age` share `<home>/vault/vault.pid` via
   `ZiPIDFile`, with home defaulting to `$HOME/.<program>`. Acquire it once
   before either aggregate is accessed and hold it until `close()`; a
   contender fails acquisition. Explicit `File` and all `Indirect` sessions
   acquire it at `open()`; `Auto + Direct` acquires it only if `File`
   fallback is needed. Native-only `Direct` has no PID file. Applications
   still close sessions promptly and retain needed secrets in memory.

5. **Q005 — `Auto` fallback. Status: resolved.** Align with Codex: try the
   native path first on each load/save; fall back to `File` on native
   initialization failure, missing credential, or any native operation
   failure, including denial and timeout. Native success takes precedence
   over an existing fallback file. Keep the variant fixed across paths.

6. **Q006 — Persistent key and filename encoding. Status: resolved.** The
   logical key follows Codex's `global/<name>` and
   `env/<environment>/<name>` layout with UTF-8 components appended unchanged;
   `/` is prohibited in both components. `File` uses the fixed
   `<home>/vault/secrets.json` aggregate with accounts and logical keys as
   separate JSON map levels, not per-key filenames or digest inputs.

7. **Q007 — Atomic replacement on both platforms. Status: resolved.** Use
   the existing same-directory temporary file and `ZiFile::rename` path.
   On Windows, `ZiFile::rename` calls `MoveFileEx` with
   `MOVEFILE_REPLACE_EXISTING` and without `MOVEFILE_COPY_ALLOWED`; there is
   no observed need for a new platform API or an in-place rewrite. Test
   replacement of an existing target on Windows when that platform is in
   scope. `ZiPIDFile` prevents overlapping access but does not protect the
   old ciphertext from a crash during an in-place truncate/write.
