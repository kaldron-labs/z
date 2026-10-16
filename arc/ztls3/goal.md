# ZtlsVault design and implementation plan

[`plan.md`](plan.md) is authoritative for implementation decisions. This
document records the application and module contracts and must agree with
the resolved decisions in that plan.

## Requirements

**Final requirements:** the following source requirements and the immediately
following `Direct`/`Indirect` clarification are fixed for subsequent plan
iteration. Do not reinterpret or change either without an explicit request.

`ZtlsVault` dependent apps can be configured to choose a `VaultStore` from among `Ephemeral`, `File`, `KeyRing`, `Module` and `Auto`.

All `VaultStore` options are implemented via the same base class API defined by pure virtual member functions.

If `VaultStore` is `Module`, a `module` path indicates the `.so` (Linux) or `.dll` (Windows).

- `Module` is a `Ztls::Vault` dynamic-loading module implementing the same back-end API as the other options. It resolves an instance of the base class API from the loaded module using a well-known symbol, in the usual way.

- `KeyRing` is the O.S./platform native credential store, i.e. Windows: Credential Manager or Linux: D-BUS Secrets Manager. Depending on the platform, one of these is built-in and bundled.

Due to Windows credential store blob size limits, there are two sub-variants of `KeyRing`:
- `Direct`, which directly stores the whole blob in the platform's credential store (this is
used under Linux with the D-BUS secrets manager), and
- `Indirect`, which stores the blob in an encrypted file that is decrypted using the secret
stored in the platform's credential store.
- On Windows, `Indirect` is auto-selected for `KeyRing` and cannot be overridden by app configuration
- On other OS, `Direct` is the default, but `Indirect` can be used instead

`Direct` and `Indirect` are an independent payload strategy combined with a
`VaultStore`, not additional `VaultStore` values. `Direct` is the default with
every backend except Windows `KeyRing`, where `Indirect` is mandatory. The
combination is also exposed to tests so `Indirect` can run over `Ephemeral`,
`File`, and `Module` without depending on an OS keyring.

## Public contract

Add `ztls/src/ZtlsVault.hh`, `ZtlsVaultStore.hh`, and `ZtlsVault.cc`. The
application-facing API is:

**Final API declaration:** keep this C++ block unchanged during further plan
iteration unless an explicit request changes the public contract.

```cpp
namespace Ztls {

ZtEnumNS(ZtlsAPI, VaultStore, int8_t, Ephemeral, File, KeyRing, Module, Auto);
ZtEnumNS(ZtlsAPI, VaultVariant, int8_t, Default, Direct, Indirect);

using VaultResult = ZuUnion<void, ZeException>;
using VaultLoadFn = ZmFn<void(ZuBSpan), ZmFnHeapID<"Ztls.Vault.LoadFn">>;
using VaultString = ZtString<ZtStringHeapID<"Ztls.Vault.Config">>;

namespace Scopes {
struct Global { };
struct Environment { ZuCSpan name; };
using Union = ZuUnion<Global, Environment>;
}
ZuDerive(Scope, Scopes::Union);

struct VaultConfig {
  VaultString program;
  VaultString account;
  VaultString envPrefix;
  VaultString module;
  int store = VaultStore::Auto;
  int variant = VaultVariant::Default;
};

class ZtlsAPI Vault {
public:
  Vault();
  ~Vault();

  Vault(const Vault &) = delete;
  Vault &operator =(const Vault &) = delete;

  VaultResult init(const VaultConfig &);
  VaultResult open();
  void close();
  void final();

  VaultResult load(Scope, ZuCSpan name, VaultLoadFn);
  VaultResult save(Scope, ZuCSpan name, ZuBSpan);

private:
  struct State;
  State *m_state = nullptr;
};

}
```

## Application-facing contract

**Final application-facing contract:** this entire section defines required
behavior. Preserve it during subsequent plan iteration unless an explicit
request changes the contract.

An application creates one `VaultConfig`, constructs a non-copyable `Vault`,
calls `init(config)` to load resolved configuration, then `open()` before
calling `load` and `save` with an application-dependent `Scope` and secret
`name`. The vault is a short-lived credential-store session: open it, perform
the needed loads/saves, then close it. Each backend independently ensures
the integrity of its own calls. `File` and `Indirect` aggregate access share
one program-specific `<home>/vault/vault.pid` through `ZiPIDFile`: acquire it
before accessing either `<home>/vault/secrets.json` or
`<home>/vault/secrets.age`. A native-only `Direct` session needs no PID file;
`Auto + Direct` acquires it if its `File` fallback is used. Explicit `File`
and every `Indirect` session acquire it during `open()`; acquire it only once
for `Indirect + File`. Hold it until `close()` drains and finalizes the active
backend, then release it. Create `<home>/vault` on demand through `ZiFile`
with mode `0700` (equivalent owner-only permissions on Windows) before
acquiring the PID file; native-only `Direct` must not create it. Fail `open()`
if acquisition reports `Running`
or `Error`, and release the guard after any subsequent start failure. A later
`open()` may reuse the resolved configuration. Applications retain the
credentials, secrets, and tokens they need in their own memory; they must not
hold the vault open as a credential cache.
`final()` releases configuration and remaining private state before
destruction; being stopped is its precondition. This lifecycle is aligned
with `ZmEngine` without adding its concurrency machinery to an
owner-thread-confined vault. The class has only private `m_` state, not
public shared data members.
`Vault` is the only public vault object: applications neither construct a
backend nor interact with a `ZiModule` directly. `init()` establishes and
copies the native-store `program` and `account`; they are immutable until
`final()`. `open()` activates the selected backend, including module loading,
and returns aggregate PID-file contention or initialization failures through
`VaultResult`. Operations before `open()` or after `close()` fail rather than
activating a backend implicitly.

`store` selects `VaultStore::Ephemeral`, `File`, `KeyRing`, `Module`, or `Auto`;
`variant` independently selects `VaultVariant::Direct` or `Indirect`, with
`Default` resolving to `Direct` for every selected store except Windows
`KeyRing`, where `Indirect` is mandatory and explicit `Direct` is rejected.
`program` identifies the application to the native credential store. If empty,
it resolves to `ZiLog::program()` (for example, `codex` or `zdash`). The rename
from `service` is API-only: Linux still stores the program value under the
`"service"` Secret Service attribute, and Windows still encodes it in the same
position of the unchanged `ZtlsVault:` credential target. Existing credentials
remain addressable when the value is unchanged.
`account` is the application's stable native-store account. If empty, derive it once
from the resolved absolute home path as `vault|<home-hash>`, where `home-hash`
is the first 16 lower-case hexadecimal characters of its SHA-256 digest. Use
`Ztls::MD<SHA256>` and Z hex formatting; do not hand-code hashing or
number-to-text conversion. Do not switch between canonical and textual paths
depending on whether the directory exists: that would change the native-store
identity after the first write. A caller may supply a non-empty account to
override the default. The account is never derived from a per-operation scope
or name.
`envPrefix` names the application's environment-variable prefix. If empty, it
resolves to the ASCII upper-case `program` (`CODEX`, `ZDASH`, and so on). A
private `upper(VaultString &)` performs that in-place environment-prefix
normalization; it maps only ASCII `a` through `z`, since environment-variable
prefixes are ASCII identifiers. The vault copies the resolved configuration, so
an application may release its configuration object after `init()`.

The vault obtains its application home from `<envPrefix>_HOME` (for example,
`CODEX_HOME` or `ZDASH_HOME`). If the variable is unset or empty, home defaults
to `$HOME/.<program>`, such as `$HOME/.codex` or `$HOME/.zdash`. `File` and
`Indirect` use paths below this resolved home. `KeyRing` uses the stable
`program` and `account` as its native credential-store namespace.
Use `ZiFile` path facilities to resolve a relative override against the
`init()`-time working directory before deriving `account`; retain that
same absolute path for subsequent file operations. Do not require the home to
exist merely to select a native store.
`VaultStore::Module` requires `module` to name the module file to load; the
other store choices ignore it.

`Auto` follows [Codex's native-first, per-operation fallback](https://github.com/openai/codex/blob/main/codex-rs/login/src/auth/storage.rs):
on every load, try the platform native store first, then `File` if the native
credential is missing or the native load fails for any reason, including
denial or timeout. On save, try the native store first, then `File` if its
save fails. Native success wins even if a fallback file exists; file existence
does not select a store in advance. If fallback also fails, return its error.
Resolve `VaultVariant::Default` once from the preferred native store:
`Indirect` for Windows `KeyRing`, `Direct` elsewhere. Keep that variant on both
native and `File` paths; explicit `Direct` and `Indirect` choices also never
change on fallback. `Indirect` remains usable with `Ephemeral`, `File`, and
`Module`.
After its factory returns the module backend, `Vault` passes its resolved
configuration to that backend's virtual `init()`.

`Scope` and `name` are application-facing only. `Scopes::Global` has no
additional identifier. `Scopes::Environment::name` selects an
application-defined environment, while the `load()`/`save()` `name` selects the
secret within that scope. The two are distinct and neither is a path. `name` and
an environment name are UTF-8, must be non-empty, and must not contain `/`.
Follow [Codex's canonical scope layout](https://github.com/openai/codex/blob/main/codex-rs/secrets/src/lib.rs):
compose `global/<name>` or `env/<environment>/<name>` without a leading slash.
Append the UTF-8 bytes directly, without percent-escaping or whole-component
hex encoding; rejecting `/` makes the delimiters unambiguous. Do not adopt
Codex's restrictive ASCII-only secret-name validation. Use `ZtScratch` with
heap fallback for the call-local composed key; do not build separate
intermediate strings for its
components. Backends receive that key only; they do not receive or interpret a
`Scope`. The vault may use the key in an aggregate record or native-store
attribute, but never treats it as a filesystem path or per-key filename.
`Scope` and its spans are borrowed for the call only and are never retained. `value` is
arbitrary binary data. An empty value is a valid saved credential, not a delete
operation. The initial API deliberately has no delete operation; add one
explicitly if callers require revocation.

Both operations return `VaultResult`: its `void` alternative is success and its
`ZeException` alternative describes failure. `Auto` needs no native-store
unavailability category: any native load/save failure triggers its `File`
fallback without parsing exception text. No
application-visible category wrapper is needed. Missing scope/name pairs,
invalid configuration, denial, timeout,
I/O, malformed records, and module failures are errors. Diagnostics must
never contain program, account, scope, name, value, or secret material.

On a successful `load`, `value` is invoked exactly once with a non-owning view
of the stored bytes. The view is valid only for the callback invocation; an
application that retains it must make and own its copy there. On failure,
`value` is not invoked and any temporary secret buffer is cleared.

`save` replaces the value for `(Scope, name)` atomically where the backend
permits it. The caller retains ownership of its byte span for the duration of
the call. The vault copies or persists it before returning. A vault is owner-thread
confined by default. Applications that need another thread or shard must invoke
it on its owner rather than sharing it concurrently.

Sensitive buffers should be cleared deterministically when released or when a
load operation fails after partially producing data. Document the callback
view's lifetime and that the vault does not log identities or values.

These decisions follow `GUIDELINES.md` “Strings” (binary values use
`ZuBSpan`, text names use `ZuCSpan`), “Callbacks” (`ZmFn`), “Bad casting”
(rely on Z span conversions), “Framework fit” (use `ZiLog`/`ZiFile`), and
“Unnecessary atomic operations, locking and copies”.

## Back-end pluggable interface and module contract

**Final backend contract:** this entire section defines the module API and
lifetime rules. Preserve it during subsequent plan iteration unless an
explicit request changes the contract.

The back-end interface is the installed `ZtlsVaultStore.hh` module contract. It
must conform to the `ZiModule` guidance in `GUIDELINES.md` and follow
`Zdb_::Store` directly. Do not use a function table, opaque context, ABI
version negotiation, host-mutation callback, or bespoke callback protocol.
Every built-in and loaded backend implements this abstract `ZmPolymorph` base:

**Final backend declaration:** keep this C++ block unchanged during further
plan iteration unless an explicit request changes the plugin contract.

```cpp
namespace Ztls_ {

class VaultStore : public ZmPolymorph {
public:
  virtual Ztls::VaultResult init(const Ztls::VaultConfig &) = 0;
  virtual void final() = 0;
  virtual Ztls::VaultResult load(ZuCSpan key, Ztls::VaultLoadFn) = 0;
  virtual Ztls::VaultResult save(ZuCSpan key, ZuBSpan) = 0;
};

typedef VaultStore *(*VaultStoreFn)();

}

extern "C" {
  typedef Ztls_::VaultStoreFn ZtlsVaultStoreFn;
  ZtlsExtern Ztls_::VaultStore *ZtlsVaultStore();
}
#define ZtlsVaultStoreFnSym "ZtlsVaultStore"
```

`init()` and `final()` are synchronous. `init()` receives the already-resolved
configuration and either leaves the store ready for `load()`/`save()` or returns
an error. A successful `load()` invokes `VaultLoadFn` synchronously exactly once and
never retains it or the supplied spans. On an `init()` failure, `Vault` calls
`final()` before releasing the returned instance.

`ZtlsVaultStore` is the only well-known exported module symbol. A module
declares and defines it with `extern "C"`, returns a newly allocated concrete
`Ztls_::VaultStore`, and fulfills initialization, finalization, load, and save
through that instance's virtual functions. `Vault` loads the module with
`ZiModule`, resolves `ZtlsVaultStoreFnSym`, `reinterpret_cast`s it to
`Ztls_::VaultStoreFn`, checks both the factory and result, then calls virtual
`init()` with the resolved configuration before selecting the backend. On vault
close, it calls virtual `final()` before releasing its `ZmRef<VaultStore>`.
This deliberately has the same C++ ABI assumptions as `ZdbStore`: the module
and host are built against compatible Z headers, compiler, and runtime.

`Vault` owns the returned polymorphic instance with the normal `ZmRef` lifetime.
Load a `VaultStore::Module` module without `ZiModule::GC`, as Zdb does: the
temporary `ZiModule` may leave scope after resolving the factory, while the OS
module remains resident through process teardown. Never call `unload()` after a
factory can have returned an object or registered code reachable from the
vault. Explicit `unload()` is permitted only before invoking the factory, such
as after a failed symbol resolution.

## Store and variant selection

Select the built-in store with one `switch` on `VaultConfig::store`; all five
choices use the same virtual API. `Auto` keeps native and `File` paths rather
than selecting one for the session. Resolve the variant once from the
preferred native store; both paths use it, so Windows `Auto` remains
`Indirect` even when `File` is used. Try `File` after any failed or missing
native load, or any failed native save. Do not parse `ZeException` text or
add a public error taxonomy. Initialize the native path during `open()` and
the `File` path when fallback first needs it. If native initialization fails,
initialize `File` instead; fail `open()` only if that also fails. Finalize
every initialized path during `close()`. For a non-`Auto` choice, `Vault` owns
the store directly for `Direct`, or owns an `Indirect` adapter that owns it. One
vault-owned `ZiPIDFile` protects both aggregate files, including `Auto`
fallback. Call each store's `init()` once per start and `final()` during
`close()` before release, including after partial initialization failure. A
later `open()` creates and initializes a new backend instance from retained
configuration. `VaultVariant::Default` is a configuration sentinel,
not a third runtime strategy.

`Ephemeral` uses one owner-thread `ZmHash` with an intrusive entry containing
the composed key and owned value. Replace and clear an old value immediately;
remove all entries at teardown. Use a named `ZmHeap` for fixed-size nodes and a
tagged `ZtBArray` for variable-size values. Do not add locks, timers, a
second index, or background collection.

`File` keeps one unencrypted, versioned JSON aggregate per program at
`<home>/vault/secrets.json`, following Codex's plaintext-file fallback rather
than using `ZtlsAge` without an independent passphrase source. Use `ZfStruct`
and `ZfJSON` to represent accounts, composed keys, and binary values; JSON
byte fields use the adapter's Base64 facility. The account and logical key
remain separate map levels, so neither is interpreted as a path or needs a
filename digest. A missing file or record is missing; an empty binary value
is valid. On save, load the aggregate (or start empty), replace only the
selected record, and publish the complete JSON via a same-directory `ZiFile`
exclusive temporary file, sync, and rename. Clean up only that operation's
temporary on failure. Reject malformed content rather than overwriting
it. The shared `<home>/vault/vault.pid` must be held before reading or
writing this aggregate and is also used for `Indirect` access; acquire it once
per vault session, not once per layer. Use `ZiFile` for filesystem operations,
restrictive permissions, and `NoFollow` on opens. The file is not encrypted:
its confidentiality relies on filesystem permissions. Use `ZtScratch` with
heap fallback for bounded call-local scratch and `ZiIOBuf` only where an
actual I/O buffer is needed; clear owned secret-bearing scratch.

This follows `GUIDELINES.md` “Storage and capacity”, “Heap allocation”,
“Containers and intrusion”, “Data movement and initialization”, “Algorithm
and control flow” (`switch` and no polling), and “I/O and system
integration” (`ZiFile`, `ZiIOBuf`).

## Linux KeyRing store

Build the Linux backend against in-tree `libZdbus`, never `libsecret-1` or
GLib. Include `ZdbusClient.hh` only in the Linux implementation. Use
`ZdbusClient` for session-bus I/O, method calls, replies, errors, and signals;
use `ZfStruct` with the `DBUS` facet and `ZfDBUS` for typed Secret Service
bodies. Do not introduce a second D-Bus framing or body codec.

Use `org.freedesktop.secrets` and the documented `plain` session algorithm.
Open one service session per connection and close it on orderly teardown.
The D-Bus `Secret` body is `(object-path session, array<byte> parameters,
array<byte> value, string content-type)`; the byte array is the credential.
`plain` does not encrypt in-transit D-Bus traffic, and the plan must not claim
otherwise.
With `plain`, parameters are empty and content type is
`application/octet-stream`; the service destination is
`org.freedesktop.secrets` at `/org/freedesktop/secrets`.

For load, call `Service.SearchItems` using the stable `program` value in the
existing `"service"` attribute, plus `account` and composed-key attributes;
encode the key attribute as needed to keep arbitrary
name bytes out of D-Bus strings. Unlock a locked match when allowed, then call
`Item.GetSecret`. For save, resolve the default collection with `ReadAlias`,
create it only when absent, unlock as needed, then call `Collection.CreateItem`
with `replace=true`, its specified `a{sv}` properties, and the `Secret`
struct. Handle a returned prompt path by subscribing to `Prompt.Completed`
before invoking `Prompt.Prompt`; completion, dismissal, timeout, and shutdown
must all finish the pending operation once and release the subscription. Do
not treat a locked item or a denied prompt as missing. Remove prompt
subscriptions on every completion path and dismiss an active prompt when
abandoning it. A prompt result can contain the created item or collection
path; use it rather than assuming the immediate method reply supplied a
usable path. Bound connection, method, and prompt waits.

The public API is synchronous, but `ZdbusClient` is async. Use `ZmBlock` on
the calling owner thread, never on a `Zdbus` Rx/Tx shard. The async return or
deadline invokes the same continuation with a result or `ZeException`;
serialize competing completions on their owner shard. Use the client's call
timeout for calls and a `ZmScheduler::Timer` only for a prompt wait; cancel and
drain it before destroying its state. Move the received `ZiIOBuf` reference
through the continuation and parse on the caller thread while the frame is
alive. This avoids an extra secret-byte copy and permits the load callback to
borrow the decoded value. Drain callbacks before client/scheduler teardown.

This follows `GUIDELINES.md` “Metadata” (`ZfStruct`/`ZfDBUS`), “Framework
fit” (no reimplemented protocol), “I/O sharding”, “Timers”, “Callbacks”,
“Data movement and initialization”, and “Algorithm and control flow”
(`ZmBlock`, not polling or a manual semaphore bridge). The wire behavior
follows the [freedesktop Secret Service specification](https://specifications.freedesktop.org/secret-service/latest-single/).

## Windows KeyRing store

Use `CRED_TYPE_GENERIC` with one unambiguous UTF-16 target name derived from
`program`, `account`, and the composed key; use Z formatting/conversion
facilities for that name, not a handwritten parser, and respect
`CRED_MAX_GENERIC_TARGET_NAME_LENGTH`. `CredWrite` uses
`CRED_PERSIST_LOCAL_MACHINE` and replaces an existing credential of the same
target and type. `CredRead` exposes the returned blob to the load callback
before `CredFree`. A blob exceeding
`CRED_MAX_CREDENTIAL_BLOB_SIZE` fails without truncation. Release
API-allocated memory and clear backend-owned secret scratch. Do not introduce
`*W()` aliases or extra caching. Windows `KeyRing` supplies the short secret
for the mandatory `Indirect` variant; its credential blob limit is why
`Direct` is unavailable there.

This follows `GUIDELINES.md` “Windows”, “Data movement and initialization”,
“Framework fit”, and “Bad casting”.

## Indirect variant

`Direct` forwards value `load`/`save` to the selected store. `Indirect` wraps
that same store through its existing virtual API: reserve a logical key that
cannot collide with user keys and use it to load/save the encryption secret.
Keep encrypted values in one versioned aggregate file at
`<home>/vault/secrets.age`, following the Codex storage model. Define a
`ZtlsVault` aggregate schema satisfying the model's structural and security
requirements; reading or writing existing Codex vault files is not required.
This works with
`Ephemeral`,
`File`, `KeyRing`, and `Module`, as well as the store chosen by `Auto`; do not
duplicate native-store operations inside `Indirect`. `Indirect + Ephemeral` is
intentionally testable, but its encryption secret disappears on teardown, so
its encrypted file is not recoverable by a later instance. A store failure
never permits writing an unencrypted file.
With `Indirect + File`, the passphrase is a record in unencrypted
`secrets.json`; the age layer does not add at-rest confidentiality beyond
the file's filesystem permissions in that combination.

Generate one passphrase under the reserved key on the first successful write:
fill 32 bytes with `Ztls::Random`, encode them as 44 characters of standard
padded Base64 using `ZuBase64`, and persist that text through the selected
store before publishing the first ciphertext. The reserved key must be outside
the `global/` and `env/` user-key space. Use the stored text itself as the age
scrypt passphrase; do not decode, derive, or regenerate it per record or write.
A read of a missing aggregate file must not create the passphrase. Load the
passphrase through the selected store, decrypt the aggregate, and look up the
composed logical key. An absent record is missing;
an empty binary value is valid. On save, decrypt an existing aggregate (or
start an empty one), replace only the named record, encrypt the complete
aggregate, and atomically replace the ciphertext via a same-directory
exclusive `ZiFile` temporary file, sync, and rename. Use `NoFollow` on opens
and clean up only that operation's temporary on failure. Keep record keys unambiguous and values
length-delimited; reject an unauthenticated or malformed aggregate rather than
silently overwriting it. This preserves other records across updates and
instances, including when `Indirect` is combined with `File` or `Module`.

Because the file ends in `.age`, reuse the implemented
[`ZtlsAge` precursor](age.md) for the actual age v1 scrypt format; do not
write a custom AEAD record under that suffix or reimplement age framing,
KDFs, or ciphers in the vault. `ZtlsAge` handles the wire format; the vault
owns passphrase storage, aggregate schema, and file publication. Do not add
Rust, an FFI bridge, or a new crypto library. Use `ZtScratch` with heap
fallback for small scratch, and `ZuClear` for owned plaintext and passphrase
buffers. Wipe them on both success and failure; avoid a JSON/CBOR round-trip
or second whole-file body copy in the age path. The first-write provisioning
path must fail closed under concurrent processes rather than leave ciphertext
encrypted under a lost key; do not add a general lock manager to the vault.

This follows `GUIDELINES.md` “Framework fit”, “Data movement and
initialization”, “Heap allocation”, and “I/O and system integration”.

## Build and verification

Update `ztls/src/Makefile.am` and install both `ZtlsVault.hh` and
`ZtlsVaultStore.hh`. Link `libZdbus` from `libZtls` only on Linux; Windows
neither builds nor links it. The top-level build already orders `zdbus`
before `ztls`. Do not add `libsecret-1` or GLib. Reuse existing `ZiModule` for
`Module` and the already implemented and wired `ZtlsAge` precursor; do not
rebuild its framing or add a new external dependency. Do not add a second
D-Bus or hand-written crypto implementation.

Put isolated API, scope/key composition, ephemeral, file, and encrypted-file
tests in `ztls/test/ZtlsVaultTest.cc`. Exercise `Direct` and `Indirect` with
`Ephemeral` and `File`, and with the `Module` fixture where applicable; in
particular, test `Indirect + Ephemeral` without a native keyring. Cover default
resolution to `Direct`, except Windows `KeyRing` to `Indirect`, and preservation
of an explicit `Indirect` choice through `Auto` fallback. Cover
`Global`/`Environment` isolation,
empty and binary values, replacement, missing-key errors, empty scope/name
rejection of `/` in both names, unambiguous `global/` and `env/` keys with
UTF-8 and NUL, borrowed callback
lifetime, `<PREFIX>_HOME` and `$HOME/.<program>` defaults, relative-home
resolution, exact `vault|<home-hash>` identity stability, shared
`ZiPIDFile` contention/release/crash recovery for `secrets.json` and
`secrets.age`, atomic file replacement, and wrong-key/authentication failure.
Exercise `File` JSON round trips across vault instances with multiple accounts
and records, empty and binary values, and malformed-content rejection.
Exercise `Indirect`
encryption/decryption round trips across vault instances with at least two
records in the single age aggregate, updating one without losing the other;
test the padded 44-character passphrase form, stable reuse across writes,
malformed ciphertext, interrupted replacement, and passphrase-store failure
without plaintext fallback. Exercise all five `VaultStore` choices and both
variants where the
platform permits them; verify that Windows `KeyRing + Direct` is rejected.
Put the controlled multi-process D-Bus peer under `ztls/itest`; it covers
binary and empty values, replacement, missing items, prompts, timeout, and
shutdown, including prompt dismissal and service loss; verify `Auto` tries
`File` after native missing, denial, timeout, or other failure, while native
success takes precedence over an existing fallback file.
Test the `Module` factory, virtual `init`/`final`, and resident-code
lifetime there with a small loadable fixture. Put tests against an
independent installed Secret Service under `ztls/interop`, skipping with a
diagnostic if none is available. Test a Windows `KeyRing` value exceeding
Credential Manager's blob limit to prove it takes the encrypted-file path.
Verify the aggregate obeys the selected age format; do not accept a merely
`.age`-named private format. No Codex file-compatibility or migration test is
required.
Use `ZuTestUtil`/`ZuTest` for
repository-owned TAP tests and `ZmBlock` for concurrent completion; do not
poll or sleep to make tests pass.

These locations and tools follow `GUIDELINES.md` “Module layout”, “Tests”,
“Build configuration”, and “Acceptance”. Build and test incrementally in the
current configuration, batching source edits and rebuilding `ztls/src` and
affected test targets only at major milestones. Defer dependent top-level
rebuilding until integration; other compilers, flags, and platforms are out
of scope for intermediate runs. Then run the required top-level build and
tests. Use `libtool exec` for any Valgrind/ASan diagnostic run, and use
Valgrind sparingly for suspected corruption and the final leak/corruption
gate. Check secret lifetime and leaks there.

For implementation, also apply `GUIDELINES.md` “Naming” (short camel-case
names, `m_` only for private class state), “Header layout” (direct includes),
“Class and struct shape”, and “Storage and capacity” (no arbitrary fixed
buffers). These are review checks, not reasons to add new types or wrappers.
