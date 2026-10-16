# ZtlsVault design and implementation plan

codex resume 01a0c3bf-bfe2-70d3-9edb-faee61c95460

## Goal

Add `Ztls::Vault` to `libZtls` as the small credential-storage abstraction used
by TLS and authentication clients. A vault stores opaque credential bytes under
a caller-supplied name and exposes the same `load`/`save` operations for six
storage policies:

- `ephemeral`: process-local memory, with no persistence;
- `file`: an on-disk file below the resolved application home;
- `direct`: the operating-system credential store;
- `secrets`: an encrypted local secret file whose passphrase is in the
  operating-system credential store;
- `auto`: the platform-default credential store with file fallback;
- `module`: a dynamically loaded vault backend.

The public API must not expose backend-specific handles or third-party types.
Backend selection must be explicit and replaceable, while allowing additional
backends to be supplied later through `ZiModule`.

## Proposed API

Add application-facing `ztls/src/ZtlsVault.hh`, installed module interface
`ZtlsVaultStore.hh`, and `ZtlsVault.cc` in the `Ztls` namespace. Keep
credential values binary-safe and use existing Z spans/arrays and `ZeException`
conventions rather than STL types.

The initial API should cover:

```cpp
namespace Ztls {

namespace VaultStore { enum { Ephemeral, File, Direct, Secrets, Auto, Module }; }

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
  VaultString service;
  VaultString account;
  VaultString envPrefix;
  VaultString module;
  int store = VaultStore::Auto;
};

class ZtlsAPI Vault {
public:
  Vault(const VaultConfig &);

  VaultResult load(Scope, ZuCSpan name, VaultLoadFn);
  VaultResult save(Scope, ZuCSpan name, ZuBSpan);
};

}
```

## Application-facing contract

An application creates one `VaultConfig`, constructs a `Vault`, then calls
`load` and `save` with an application-dependent `Scope` and secret `name`.
`Vault` is the only public vault object: applications neither construct a
backend nor interact with a `ZiModule` directly. Construction establishes and
copies the native-store `service` and `account`; they are immutable for the
vault's lifetime. Backend initialization, including module loading, may be
deferred until the first operation so its failure can be returned as a
`ZeException`.

`store` selects `VaultStore::Ephemeral`, `File`, `Direct`, `Secrets`, `Auto`,
or `Module`. `service` identifies the application to the native credential
store. If empty, it resolves to `ZiLog::program()` (for example, `codex` or
`zdash`). `account` is the application's stable native-store account. If empty,
derive it once from the resolved absolute home path as `vault|<home-hash>`,
where `home-hash` is the first 16 lower-case hexadecimal characters of its
SHA-256 digest. Use `Ztls::MD<SHA256>` and Z hex formatting; do not hand-code
hashing or number-to-text conversion. Do not switch between canonical and
textual paths depending on whether the directory exists: that would change
the native-store identity after the first write. A caller may supply a
non-empty account to override the default. The account is never derived from
a per-operation scope or name.
`envPrefix` names the application's environment-variable prefix. If empty, it
resolves to the ASCII upper-case `service` (`CODEX`, `ZDASH`, and so on). A
private `upper(VaultString &)` performs that in-place environment-prefix
normalization; it maps only ASCII `a` through `z`, since environment-variable
prefixes are ASCII identifiers. The vault copies the resolved configuration, so
an application may release its configuration object after construction.

The vault obtains its application home from `<envPrefix>_HOME` (for example,
`CODEX_HOME` or `ZDASH_HOME`). If the variable is unset or empty, home defaults
to `$HOME/.<service>`, such as `$HOME/.codex` or `$HOME/.zdash`. `File` and
`Secrets` use paths below this resolved home. `Direct` uses the stable
`service` and `account` as its native credential-store namespace.
Use `ZiFile` path facilities to resolve a relative override against the
construction-time working directory before deriving `account`; retain that
same absolute path for subsequent file operations. Do not require the home to
exist merely to select a native store.
`VaultStore::Module` requires `module` to name the module file to load; the
other store choices ignore it.

`Auto` follows Codex's policy: try the platform default native store, then fall
back to `File` only if native-store initialization establishes that the store
is unavailable. Once selected, a backend's load/save errors propagate; an
absent *credential* is not an unavailable store and must not expose a stale
file fallback. The platform
default is `Secrets` on Windows, avoiding Credential Manager's credential-blob
size limit, and `Direct` on Linux. `Direct` and `Secrets` are explicit choices
and never silently fall back. After its factory returns the module backend,
`Vault` passes its resolved configuration to that backend's virtual `init()`.

`Scope` and `name` are application-facing only. `Scopes::Global` has no
additional identifier. `Scopes::Environment::name` selects an
application-defined environment, while the `load()`/`save()` `name` selects the
secret within that scope. The two are distinct and neither is a path. `name` and
an environment name must be non-empty. Before calling a backend, `Vault`
validates and composes the opaque logical key as `/global/<name>` or
`/env/<environment>/<name>`, with each component encoded using existing
`ZuHex`/`ZtQuote::Hex` facilities so delimiters are unambiguous. Use
`ZtScratch` with heap fallback for the call-local composed key; do not build
separate intermediate strings for its components. Backends receive that key
only; they do not receive or interpret a
`Scope`. The vault may use the key in a record, native-store attribute, or
encoded filename, but never treats it as a filesystem path. `Scope` and its
spans are borrowed for the call only and are never retained. `value` is
arbitrary binary data. An empty value is a valid saved credential, not a delete
operation. The initial API deliberately has no delete operation; add one
explicitly if callers require revocation.

Both operations return `VaultResult`: its `void` alternative is success and its
`ZeException` alternative describes failure. Backend initialization reports
native-store unavailability to `Auto` before converting it to the public
diagnostic; no application-visible category wrapper or message parsing is
needed. Missing scope/name pairs, invalid configuration, denial, timeout,
I/O, malformed records, and module failures are errors. Diagnostics must
never contain service, account, scope, name, value, or secret material.

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

## Back-end pluggable interface and module contract

The back-end interface is the installed `ZtlsVaultStore.hh` module contract. It
must conform to the `ZiModule` guidance in `GUIDELINES.md` and follow
`Zdb_::Store` directly. Do not use a function table, opaque context, ABI
version negotiation, host-mutation callback, or bespoke callback protocol.
Every built-in and loaded backend implements this abstract `ZmPolymorph` base:

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
teardown, it calls virtual `final()` before releasing its `ZmRef<VaultStore>`.
This deliberately has the same C++ ABI assumptions as `ZdbStore`: the module
and host are built against compatible Z headers, compiler, and runtime.

`Vault` owns the returned polymorphic instance with the normal `ZmRef` lifetime.
Load a `VaultStore::Module` module without `ZiModule::GC`, as Zdb does: the
temporary `ZiModule` may leave scope after resolving the factory, while the OS
module remains resident through process teardown. Never call `unload()` after a
factory can have returned an object or registered code reachable from the
vault. Explicit `unload()` is permitted only before invoking the factory, such
as after a failed symbol resolution.

## Ephemeral backend

Implement one owner-thread `ZmHash` of vault-composed keys to owned byte
arrays. Keep key and value in the same intrusive node where practical; use a
named `ZmHeap` for fixed-size nodes and a tagged variable-size heap for values.
Replacement releases and clears the old value immediately. An absent key is a
load error; an empty stored value is valid. Do not add locks, a deletion API,
background cleanup, file I/O, or secret-service calls to this backend.

## File backend

Store each value as the file's exact bytes; a private record header, duplicate
service/account/key metadata, checksum, and parser add no value to an
unencrypted byte store. Derive a fixed-size filename from the stable
service/account/composed-key identity using `Ztls::MD<SHA256>` and Z hex
formatting, not a hand-rolled hash or the logical key as a path. Use `ZiFile`
for file operations, `NoFollow` on opens, and restrictive permissions on
new files/directories. A missing file is a missing credential; an empty file
is a valid empty value. Save through a same-directory temporary file and
atomic rename, with last-writer-wins semantics. Do not add a lock or scan the
directory for cleanup. The file backend's protection is filesystem
permissions, not encryption or an OS keyring.

## Linux direct credential-store backend

Implement Linux `VaultStore::Direct` against the freedesktop Secret Service
using the in-tree `libZdbus`, not `libsecret-1`, GLib, or another D-Bus client.
The backend includes `ZdbusClient.hh`; it uses `ZdbusClient` for session-bus
connection, serials, calls, replies, errors, and prompt signals. Define the
Secret Service method and signal schemas with `ZfStruct` and the `DBUS` facet;
use the existing `ZdbusReqBuilder`/`ZdbusResParser` headers/body boundary and
`ZfDBUS` for direct binary body save/load. Do not add Secret Service behavior
to `libZdbus` or introduce a second codec. The service destination is
`org.freedesktop.secrets`, rooted at `/org/freedesktop/secrets`.

Connect to the user's session bus, then call
`org.freedesktop.Secret.Service.OpenSession` with the specified `plain`
algorithm and an empty-string variant. Retain the returned session object path
for that connection; close it through `org.freedesktop.Secret.Session.Close`
on normal teardown. The `Secret` D-Bus struct is `(object-path session,
array<byte> parameters, array<byte> value, string content-type)`: with
`plain`, parameters are empty and content type is
`application/octet-stream`. Values remain binary-safe, including empty values
and embedded NULs. `plain` does not encrypt traffic between the process and
the Secret Service; do not claim that the keyring provides in-transit
encryption. A later encrypted-session mode is a separate feature, not an
implicit `Zdbus`/`Ztls` capability.

Use the stable, non-secret lookup attributes `service`, `account`, and `key`,
encoding each to an unambiguous stable string if needed. These are
`a{ss}` in `SearchItems` and the `org.freedesktop.Secret.Item.Attributes`
property, not D-Bus object paths or filenames; the item label is fixed and
non-sensitive. For `load`, call `Service.SearchItems`, use an unlocked match or
request `Service.Unlock` for a locked match, then call `Item.GetSecret` with
the open session. No match is the distinct missing-key result. For `save`,
resolve the `default` collection through `Service.ReadAlias`, creating it
through `Service.CreateCollection` only when absent; unlock it when needed,
then call `Collection.CreateItem` with full-name `Label` and `Attributes`
properties (`a{sv}`), the `Secret` struct, and `replace=true`. Use the
service's replacement semantics instead of a client-side search/update race.
Do not treat a locked match as missing or create an unprotected file after a
denial.

`Unlock`, `CreateCollection`, and `CreateItem` can return a prompt path.
When it is not `/`, install the `Prompt.Completed` signal match and local
subscription before calling `Prompt.Prompt`, then wait for its
`(dismissed, variant result)` completion. Unsubscribe/remove the match on
completion, timeout, cancellation, or shutdown; call `Dismiss` when abandoning
an active prompt. Interpret each operation's result, including an item or
collection path produced by a prompt, rather than assuming the method's
immediate path is usable. A dismissed prompt, locked collection, or access
denial is a classified failure, not a missing key or `Auto` fallback. Bound
connection, method, and prompt waits so headless operation cannot hang.

`ZdbusClient` is asynchronous, while the proposed `VaultStore` methods are
synchronous. Give this backend a private scheduler/client with Rx/Tx shards
and use `ZmBlock<ZuUnion<Value, ZeException>>` for each asynchronous
operation, where `Value` is that operation's successful reply payload (not
the public `Ztls::VaultResult` alias). Pass the block's continuation to the
operation: a D-Bus return completes it with `Value`, while a D-Bus error,
disconnect, cancellation, or deadline expiry completes it with `ZeException`.
The timeout callback invokes that same continuation; it does not need a
separate blocking or polling mechanism. Serialize completion and timeout on
the operation's owning shard, cancel the losing path, and invoke the
continuation exactly once. `ZmBlock` itself has no timeout, so a missing peer
or prompt must still reach the deadline path. Never block either `Zdbus` shard
waiting for itself. The vault owner calls `load`/`save` only from a thread
outside those shards and outside a UI/event-loop thread. Preserve the
public contract by invoking `VaultLoadFn` exactly once on the calling owner thread
before returning; transfer the returned bytes into one owned temporary buffer
only when crossing from the `Zdbus` callback to that thread requires it, then
clear it immediately after the callback. Keep request values alive until
`Zdbus` has built the outbound frame, avoid additional body round-trips, and
clear backend-owned secret-bearing buffers on success and failure. Stop and
drain pending calls/prompts before releasing the client and scheduler.

Map session-bus/service absence, disconnection, and activation failure to
`unavailable`; map timeout, malformed replies, remote D-Bus errors, rejected
prompts, and locked/denied access to their appropriate non-missing failures.
Only actual absence of a matching item is `missing`. `Auto` may fall back to
`File` only for `unavailable`, never for denial, timeout, or a missing item.
Test this mapping against a deterministic Secret Service peer and, where a
usable desktop keyring exists, a real service.

## Windows direct Credential Manager backend

Implement a Windows backend using Credential Manager APIs and `CRED_TYPE_GENERIC`
with a stable target-name encoding of service, account, and the vault-composed
key. Use the project’s MinGW/Windows conventions, including the `wchar_t` APIs
already used by the repository; do not introduce `*W()` aliases.

Encode service, account, and the vault-composed key into one unambiguous UTF-16
target name, validate it against `CRED_MAX_GENERIC_TARGET_NAME_LENGTH`, and use
it consistently with `CredRead`, `CredWrite`, and (when deletion is added)
`CredDelete`. A save zero-initializes a `CREDENTIAL`, sets `Type` to
`CRED_TYPE_GENERIC`, `Persist` to `CRED_PERSIST_LOCAL_MACHINE`, `TargetName` to
the encoded identity, and `CredentialBlob`/`CredentialBlobSize` to the caller's
byte span, then calls `CredWrite(..., 0)`. `CredWrite` replaces an existing
generic credential with the same target and type.

For a load, call `CredRead(target, CRED_TYPE_GENERIC, 0, &credential)`, expose
`credential->CredentialBlob` and `CredentialBlobSize` to `VaultLoadFn`, and call
`CredFree(credential)` immediately after the callback. Reject a value larger
than `CRED_MAX_CREDENTIAL_BLOB_SIZE` rather than truncating it. `ERROR_NOT_FOUND`
maps to the distinct missing-key exception, which `Vault` maps to missing
scope/name; malformed identity/value inputs map to an invalid-input exception;
`ERROR_NO_SUCH_LOGON_SESSION` and unavailable
credential-set conditions map to unavailable; all other `GetLastError()`
failures map to a Windows `ZeException`. The provider must clear its own
temporary secret buffers and always release the API-allocated result with
`CredFree`.

## Secrets backend

Implement `VaultStore::Secrets` as Codex-style two-stage storage. The native
credential store holds one high-entropy passphrase under the resolved stable
service and account. The vault uses that passphrase to encrypt and decrypt one
versioned aggregate file at `<home>/vault/secrets.age`; its logical record keys
are vault-composed keys. The operating-system store therefore need not hold a
credential blob for every record.

Generate and save the passphrase on first write, but never create it on a read
that finds no aggregate file. Encode binary values length-delimited in the
encrypted file, use same-directory atomic replacement, and wipe passphrase and
plaintext buffers as soon as their operation completes. Failure to load or save
the passphrase is a `ZeException`, not permission to create an unprotected
file. The backend's platform behavior is otherwise the same: Windows uses
Credential Manager only for the short passphrase, avoiding its credential-blob
limit; Linux uses the Secret Service provider for the passphrase.

## Dynamic backends with `ZiModule`

For `VaultStore::Module`, load the backend at `VaultConfig::module` and resolve
`ZtlsVaultStoreFnSym` through `ZiModule`, exactly as Zdb resolves
`ZdbStoreFnSym`. Reject an empty module path, a missing `ZtlsVaultStore` symbol,
a null factory result, or a failed virtual `init()` before making the backend
selectable. Load without `ZiModule::GC` and do not unload the module after
resolution; its code must remain resident while the returned polymorphic object
or any framework-owned callbacks can exist. On vault teardown, invoke the
store's virtual `final()` and release its `ZmRef`; module code remains resident.
Surface module-load,
symbol-resolution, factory, and initialization errors through the normal vault
error path.

The built-in ephemeral, file, secrets, and platform direct implementations
should be selected without a plugin. On Linux, the Secret Service backend is
built against `libZdbus`; whether a session bus and Secret Service are running
is a runtime availability question. Dynamic loading is for additional
backends; it must not make the built-in choices depend on a module file.

## Build integration

Update `ztls/src/Makefile.am` and `pkginclude_HEADERS`, with `libZdbus` as a
Linux-only build/link dependency of `libZtls`; the top-level module order already
builds `zdbus` before `ztls`. Do not add `libsecret-1` detection or a GLib
dependency. Add the application and module-interface headers to the installed
`libZtls` headers. Keep platform-specific source files under the module and
guard them with the project's existing `_WIN32` conventions. Windows must not
build or link `libZdbus`. Document configuration names, runtime Secret Service
availability, and defaults in the module documentation.

## Tests

Add `ztls/test/ZtlsVaultTest.cc` and its Makefile entry using `ZuTest` and TAP.
Cover:

- save/load, replacement, missing scope/name pairs, empty values, binary values, callback
  lifetime, and backend `ZeException` mapping;
- ephemeral isolation and deterministic cleanup;
- service, account, and `envPrefix` defaults, including the stable home-derived
  account, explicit `<PREFIX>_HOME`, and default-home resolution from
  `$HOME/.<service>`;
- `Global` and `Environment` scope isolation, empty scope/name rejection, and
  unambiguous scope/name encoding;
- file and secrets persistence across vault instances, atomic replacement,
  malformed data, restrictive permissions, invalid names, and interrupted-write
  recovery;
- Linux Secret Service protocol tests using an isolated bus and a controlled
  `ZdbusServer` peer: typed wire signatures, open/close session, binary and
  empty values, missing item, replacement, locked item, prompt completion and
  dismissal, denied operation, timeout, service loss, and `Auto` fallback only
  on unavailability;
- Linux direct-store round trips against an independent Secret Service when
  available, with a skipped/diagnostic result when the environment has no
  usable keyring; no `libsecret-1` runtime or build dependency;
- Windows direct Credential Manager round trips under MinGW/Windows, including
  missing values and maximum-size rejection;
- Windows `Auto` selecting `Secrets`, storing only its short passphrase in
  Credential Manager, and falling back to `File` when the native store is
  unavailable;
- dynamic module loading through `ZtlsVaultStore`, missing symbols, null
  factories, virtual `init()` failure, `final()` before `ZmRef` release, and
  process-resident module lifetime.

Run the focused vault tests on Linux with the controlled D-Bus peer and, when
available, an external Secret Service, then run the Windows build/test path.
Finish with the repository's required top-level rebuild and `make test`;
record unavailable external secret services as environment limitations rather
than weakening backend behavior.

## Delivery sequence

1. Finalize `ZeException` semantics, application-home resolution, callback value
   lifetime, scope/name encoding, and the `ZtlsVaultStore` factory/base-class
   contract against the existing Zdb convention.
2. Add the application header, installed `ZtlsVaultStore` module contract, and
   ephemeral backend.
3. Add file and secrets backends with persistence/security tests.
4. Add Linux direct over `ZdbusClient` and Windows direct over Credential
   Manager, with platform-specific build integration and tests.
5. Add `Auto` policy selection, `ZiModule` factory loading, process-resident
   module lifetime handling, and module tests.
6. Integrate installed headers/build targets, update documentation, run the
   complete build/test matrix, and review secret lifetime and failure paths.

## Open design decisions to resolve before coding

- Whether resolved `service`, `account`, `envPrefix`, and home are constructor-only
  configuration or exposed through read-only getters.
- Whether to add a separate asynchronous vault API later; the initial
  synchronous API uses `ZmBlock` around the Linux `ZdbusClient` continuation
  path above.
- Whether `save` supports delete/clear explicitly or uses an empty value as a
  valid credential; deletion should be added if callers need revocation.
