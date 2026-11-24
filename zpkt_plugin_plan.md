# Zpkt plugin implementation plan

Author: hrogers-kaldron

Date: 2026-11-22
Status: implemented and verified in the current configuration (2026-11-22).

## Requirements and variations

The baseline is [zpkt_plugin.md](zpkt_plugin.md). Later user instructions take
precedence over its examples. Implement the decisions below; do not carry the
open alternatives from `concepts_brief.md` into the implementation.

| ID | Difference from the initial document | Basis and required behavior |
| --- | --- | --- |
| V1 | Plugin initialization loads configurations, not named interfaces. | User clarification. Retain an owned typed map; create and open each device only in `nif`. EAL initialization may discover native ports as part of its existing operation. |
| V2 | Both `Plugin::nif` and `Env::nif` take `ZiMultiplex *mx` before the name. | User clarification. The factory initializes the device before returning. |
| V3 | Remove the old device `init` completely. | User clarification. There is no public or private `Nif::init`, no rename of that method, and no second caller initialization step. Put opening in concrete construction with scoped rollback. |
| V4 | Applications explicitly manage intrusive Env ownership and pass Env to TCP. | User clarification. Keep `Env::init(cf)` without `m_mx`; add Env alongside the existing TCP multiplexer parameter. Last-reference release destroys Env and consequently its Plugin/EAL, replacing ZmSingleton-controlled destruction. All other lifecycles remain unchanged. |
| V5 | DPDK configuration includes `port X`. | User-confirmed naming decision. The map key is the Linux interface name; `port` is the native DPDK Ethernet-port selector. Never configure numeric `ifindex` or repeat the key as `name`. |
| V6 | Make `nif` idempotent through Env's name-keyed ZmRBTree. | User instruction. Env owns ZmRef<Nif> values; repeated lookup returns the existing Nif for the same name/multiplexer. Each child has a raw Env backpointer. |
| V7 | Define `preload`, absent from the initial sample aggregate. | Plan choice correcting its inconsistent loader sample. Add `bool preload = false` and Cf metadata, following `Zdb_::StoreLoadCf`. Preserve the sample's conditional `ZiModule::Pre` behavior. Tests use false unless explicitly testing that flag. |
| V8 | Validate the factory result and publish only a successfully initialized plugin. | Plan choice correcting the initial sample's null-result dereference and partially published plugin. Keep structured exceptions and permanent module residency. |
| V9 | The core queries the kernel Nif and supplies its information to the plugin. | Revised user instruction. Rename `queryLink` to `nifQuery`, use `NifInfo` instead of `LinkState`, and remove duplicate Linux queries and Linux/DPDK MTU/MAC validation from plugins. |
| V10a — resolved | Define startup versus steady-operation errors. | Settled rule: startup-only init/config may throw `ZeException`; fallible functions callable during steady operation return `ZuUnion<..., ZeException>`. Apply the API matrix below. |
| V10b — resolved | Keep explicit caller-to-TCP reference handoff. | The caller acquires a `ZmRef<Nif>` and TCP consumes it only on successful initialization. |
| V10c — resolved | The explicitly created/destroyed plugin directly owns EAL. | EAL is a plugin member, with no ZmSingleton for either object. Preserve existing EAL behavior and EALParams structure/metadata; change ownership, not the EAL lifecycle. |
| V10d — resolved | Preserve Nif lifecycles under an explicit Env parent. | Env precedes and outlives every Nif. Callers stop/drain children; Env::final asserts that precondition and idempotently finalizes them. Per-Nif and EAL resource semantics remain unchanged. Module code stays resident. |
| V11 | Pass `const NifInfo &` into the concrete backend factory. | Signature variation implementing V9. Env performs lookup/query in the core and calls the plugin's virtual nif operation with trusted metadata. |
| V12 | Named lookup/finalization return result unions. | Error-signature variation from the initial raw-pointer Env example, applying V10a to the plan's steady-operation lookup and teardown behavior. Success still carries the same Nif pointer. |
| V13 | Env owns children without an engine or start/stop API. | Revised user instruction. Use ZmRBTree keyed by name with ZmRef<Nif> values and raw child-to-Env pointers. Env::nif requires initialized m_plugin. init/final must not be called concurrently; final asserts all Nifs are already stopped/drained, then calls their idempotent final operations. |
| V14 | Allow a dependent to substitute the Nif query function. | User instruction resolving IR-003. Env::nifQueryFn(fn) accepts a plain function pointer with the same signature as nifQuery; normal operation defaults to the real query. |
| V15 | Place code according to actual backend use. | User instruction resolving IR-001. Anything only needed by AF_XDP belongs in libZpkt_xdp; anything needed by both AF_XDP and DPDK belongs in core. |

**V10 is resolved.** Its four entries record settled decisions, not alternatives
for an implementer to choose. The startup sequence below explains the intended
design; the user's explanation is not an additional design revision. To express
that sequence unambiguously, the module-entry signature receives cf and returns
an initialized Plugin. This corrects the initial sample's no-argument factory
followed by a separate member-init call; no new EAL parameter schema is involved.

## Startup sequence

1. The application acquires `ZmRef<Zpkt::Env>` and calls `env->init(cf)`.
2. Env loads the shared library named by `module` and resolves `ZpktPlugin`, the
   module's plugin-initialization entry point.
3. Env calls that entry point with the configuration. The entry point constructs
   the concrete plugin, retains its owned interface configurations, and, for
   DPDK, initializes the plugin's EAL member from the passed EAL configuration.
4. The call returns the fully initialized Plugin pointer. Env acquires its
   intrusive reference and publishes it. There is no second Plugin::init call.
5. Later, Env performs named lookup in its tree, queries the kernel on a miss,
   and delegates native device
   creation. Plugin initialization itself creates no named Nif.

EALParams and its existing ZfStruct metadata remain unchanged. Moving ownership
inside the explicitly created/destroyed DPDK plugin requires no singleton and
no new EAL configuration aggregate or facet.

### Object mapping

| Before | After | Responsibility |
| --- | --- | --- |
| Zpkt::Instance | Zpkt::Nif | The same per-interface device state and lifecycle, with initialization performed by its factory. |
| Singleton-owned DPDK EAL | EAL state contained directly in Dpdk::Plugin | DPDK-specific environment state; existing EAL parameters and metadata are unchanged. |
| ZmSingleton ownership | Explicit, intrusive Zpkt::Env ownership | Env owns m_plugin and its name-keyed tree of Nif references; releasing the last Env reference replaces singleton-controlled destruction after children have retired. |

Env has no backend-specific data or multiplexer pointer. It owns
ZmRef<Plugin> m_plugin and a name-keyed ZmRBTree whose values are ZmRef<Nif>.
It has no ZmEngine base/dependency, lifecycle state machine or start/stop methods.
Env must be constructed before, and outlive, every child Nif. Children hold a
raw Env backpointer; they do not retain Env and create an ownership cycle.
AF_XDP has no EAL equivalent: its concrete plugin needs only typed configuration;
AF_XDP sockets, arenas, BPF attachment and other per-interface resources stay
in Xdp::Nif and its existing dependents. Do not invent AF_XDP global native state
to mirror DPDK. Common configuration-independent lookup/query belongs to Env;
the concrete plugin consumes trusted metadata and owns backend configuration.

### Lifecycle preservation — IR-002 resolved

The significant lifecycle difference is explicit Env ownership. Env derives
from ZmPolymorph and is held by intrusive references. Its Plugin reference owns
the loaded backend instance, and the DPDK instance contains EAL. Construction,
initialization and finalization are under explicit application
control; destruction follows release of the last Env reference, which in turn
releases Plugin and destroys its EAL member. This replaces the previous
ZmSingleton-controlled destruction.

Callers stop and drain Nifs through their existing lifecycle. Env::final asserts
that all retained children have reached that condition, then finalizes each one.
Every per-Nif lifecycle remains unchanged: TCP, pool/buffer ownership,
backing retention, scheduler service, stop/final ordering, restart behavior and
native failure/cleanup behavior. Relocating initialization into the named
factory is an API refactor, not a new device lifecycle. Do not add an Env engine,
start/stop methods, an independent state machine, process-global attempt guards,
attachment quarantine/transfer, cleanup retry ownership, or new destruction
failure policy to this refactor. Preserve the existing implementations and
their lifecycle preconditions, subject only to the explicitly requested query
and error-contract changes.

Use the initially listed filenames `libZpkt_xdp.so` and `libZpkt_dpdk.so`.
Use those filenames consistently in configuration examples. Implement on the existing Linux
zpkt build; keep C linkage and export macros suitable for DLL interfaces, but
do not introduce a new Windows packet backend or enable zpkt on Windows.

A deliberate change from the **current code**, consistent with the named-Linux-
interface requirement, is that a real Nif factory requires its Linux key to
resolve. Existing standalone DPDK fixtures that passed `ifindex == 0` must create
dummy kernel Nifs for routing. Pure pool/EAL tests need no Nif and remain unprivileged.
Do not preserve anonymous, uninitialized, or kernel-Nif-less device factories
through a compatibility API.

## Public interfaces

Keep `Zpkt.hh` as the installed device-interface header and rename its base to
`Zpkt::Nif`. Preserve DeviceEngine and all allocation, service, status, queue,
checksum-offload, and stop interfaces except the removed initialization method.
Remove `Type`, the exported `instance(Type)` factory, the public backend Params,
and their union. There are no aliases or forwarding shims for removed APIs.

Add installed `ZpktPlugin.hh`:

```cpp
namespace Zpkt {
class Nif;
struct NifInfo;
struct Plugin : public ZmPolymorph {
  virtual ZuUnion<Nif *, ZeException> nif(
    ZiMultiplex *, ZuCSpan name, const NifInfo &) = 0;
  virtual ZuUnion<void, ZeException> final() = 0;
};
typedef Plugin *(*PluginFn)(const ZfCf::AnyNode *);
}
extern "C" { typedef Zpkt::PluginFn ZpktPluginFn; }
#define ZpktPluginFnSym "ZpktPlugin"
```

Apply `ZpktAPI` to the interface and follow repository header conventions.
Concrete plugins use `ZmNHeap` and export exactly one C-linkage `ZpktPlugin`
initialization function per module, taking cf and returning a fully initialized
Plugin. For DPDK its implementation returns `new Dpdk::Plugin{cf}`; the
constructor loads configuration and initializes its EAL member before returning.
Factory-created objects initially have no caller reference;
the recipient immediately acquires a `ZmRef`. There is no C function table or
version-negotiation protocol.

Add installed `ZpktEnv.hh` and common implementation `ZpktEnv.cc`. Env is an
application-owned, noncopyable ZmPolymorph allocated through ZmNHeap<Env>,
with a destructor, `void init(cf)`,
`ZuUnion<Nif *, ZeException> nif(ZiMultiplex *, ZuCSpan)`, and
`ZuUnion<void, ZeException> final()`. Its plugin reference is exactly
`ZmRef<Plugin> m_plugin`. It has no multiplexer member. Define the child container
with ZmRBTreeKV<CfString, ZmRef<Nif>>, unique keys, ZmNoLock, and the heap ID
Zpkt.Env.Nifs. The tree owns its child references. Env::nif implements lookup,
nifQuery and insertion; the plugin has no second child container or lookup layer.
Env has no start/stop/state API, engine base, engine include or engine hooks.
Keep all existing Nif engine implementation unchanged.

Define `NifInfo` in Zpkt.hh, containing queried kernel-interface geometry such
as ifindex, MTU, MAC and Rx queue count. Keep XDP attachment/program IDs and
their ownership checks in the AF_XDP module, not in common metadata. The core's
`nifQuery(ZuCSpan name)` returns `ZuUnion<NifInfo, ZeException>`. Keep kernel
query declarations internal in `ZpktNif.hh`; the metadata type is part of the
plugin interface. Native DPDK capabilities remain backend-specific information.

### Replaceable core query — IR-003 resolved

Env exposes an instance setter `void nifQueryFn(NifQueryFn fn)`, where the
public alias is exactly:

```cpp
using NifQueryFn = ZuUnion<NifInfo, ZeException> (*)(ZuCSpan);
```

Store that plain pointer as m_nifQueryFn, initialized by Env's constructor to
the real Zpkt::nifQuery. The setter requires a nonnull pointer and is configured
by the dependent during serialized setup. It needs no lock, closure, context
pointer, virtual adapter or additional allocation. It remains set across init;
init must not overwrite a dependent's supplied mock.

On a cache miss Env calls m_nifQueryFn(name) instead of directly naming the
kernel function. A cache hit calls neither the real query nor the mock. The
mock's success/error contract is identical to the real query's; Env propagates
an error without invoking the plugin. It must provide valid NifInfo on success,
which is passed to the plugin unchanged. The function is used only for core
named creation, not to replace native BPF attachment transactions.

In ZpktEnvTest, install a static function with this signature, returning fixed
synthetic metadata for test names or a controlled ZeException. Count calls in
test-owned state. Combined with the test plugin this exercises actual Env
lookup/insertion, metadata forwarding, creation failure, idempotence and child
finalization without querying the kernel or requiring interfaces/privileges.
Verify two same-name lookups make one mock query, a mock error makes zero
plugin calls, and init preserves the installed function. Native integration
tests retain the default real function and existing namespace fixtures.

Retain TCP's existing device handoff and add the borrowed environment parameter:

```cpp
ZuUnion<void, ZeException> Zutcp::Instance::init(
  ZiMultiplex *mx, Zpkt::Env *xdpEnv,
  ZmRef<Zpkt::Nif> &&nif, Params params);
```

Store `Env *m_xdpEnv` without a reference count. The application selects the
interface name and invokes `env.nif`; TCP does not invoke the factory itself.
This avoids inventing a TCP interface-name configuration field. Preserve all
existing validation before moving the device reference. On first success, TCP
owns the Nif. On failure, the caller still owns and finalizes it. An identical
repeat may pass an empty Nif reference and must match Env, multiplexer and TCP
configuration. A changed Env is a conflicting repeat. Do not acquire another
Nif to implement repeated TCP initialization.

The application retains its Env reference until all Nifs and external Nif
references have retired. Env exists before every child and outlives them all.
Its owning tree does not remove that application lifetime obligation. Preserve
device/TCP finalization ordering and return all native buffers. TCP finalization
clears its borrowed Env pointer;
it never calls `Env::final` or destroys Env.

The first handoff follows this pattern; `cf` and `tcpParams` are already loaded
application configuration, and mx already has the sender slot configured:

```cpp
ZmRef<Zpkt::Env> env = new Zpkt::Env;
env->init(cf); // ZeException propagates to the application's startup handler
ZiMultiplex mx{mxParams}; // stopped; sender slot matches the interface Cf
auto opened = env->nif(&mx, "veth0"_z);
if (opened.is<ZeException>()) {
  ZiLogEvent(ZuMv(opened).p<ZeException>());
  return;
}
ZmRef<Zpkt::Nif> nif = opened.p<Zpkt::Nif *>();
ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance;
auto result = tcp->init(&mx, env.ptr(), ZuMv(nif), ZuMv(tcpParams));
if (result.is<ZeException>()) {
  // Failed TCP initialization has not consumed nif.
  nif->final();
  // Report nif->error() if set, and the original TCP initialization error.
  nif = nullptr;
  auto finalized = env->final();
  if (finalized.is<ZeException>())
    ZiLogEvent(ZuMv(finalized).p<ZeException>());
  return;
}
// nif is now empty. Install tcp in this same mx before allocator registration.
// Start configured child services through mx/TCP; Env has no start/stop.
// At shutdown, quiesce TCP owners and stop/drain children while schedulers run;
// return dependent buffers and release external Nif references, finalize
// mx/TCP and Env, then release Env references. The last
// reference destroys Env/Plugin/EAL. There is no Nif::init call.
```

Use normal application error handling around the entire scope, not only the
TCP result branch. Constructor and configuration errors follow the same
structured-exception path; app-owned Env and multiplexer lifetimes cover every
successfully returned Nif.

## Owned configuration

`PluginLoadCf` has `CfString module` (required, mutable Cf String) and
`bool preload = false` (mutable Cf Bool). Use the named string heap in the
initial document. Load it through `ZfCf::handler<PluginLoadCf>(cf).update(...)`.

Each backend defines an `InterfaceCf` aggregate with a Cf facet and an owned
`ZmHashKV` map of Linux-name strings to that aggregate. Use ZfCf's existing
`AsMap<ZfFieldTC::UDT>` adaptation, as exercised in `zf/test/ZfCfTest.cc`;
do not add a second configuration parser or extend ZfCf for this refactor.
The backend plugin aggregate has a required `interfaces` UDT field. An empty
map is valid for pool/EAL-only tests. DPDK additionally reads the `eal` child
using the existing EALParams aggregate and its existing metadata, defaulting
to the existing EAL defaults when absent.

| Interface field | AF_XDP | DPDK | Cf type and rule |
| --- | --- | --- | --- |
| `spin` | default 1000 | default 1000 | UInt32; retain current zero/spin behavior |
| `txSid` | required | absent | UInt32; nonzero; checked against the supplied multiplexer at creation |
| `port` | absent | required | owned String; nonempty native Ethernet-port name |
| `rxInterval` | absent | default 50 | UInt32; current maximum idle wait in microseconds; zero keeps polling |

The key is owned storage, not a span into the caller's tree. On first creation,
the core resolves and queries the kernel Nif through `nifQuery` and passes
`NifInfo` to the concrete backend's nif operation. A missing kernel Nif is a
core factory error. DPDK
resolves only its configured `port` through native DPDK APIs and discovers native
capabilities. It trusts the supplied NifInfo and the configured port association:
remove the Linux/DPDK MTU/MAC agreement checks, repeated Linux queries, and the
old duplicate pre-activation `checkPort` validation. This is an intentional
change from current validation behavior. Never infer pairing from a PMD's host
index or search by MAC. AF_XDP likewise consumes the core's supplied metadata.

Do not change the EALParams structure or its ZfStruct declarations/definitions,
add a Cf facet, or introduce a second EAL configuration aggregate. Reuse its
existing CLI facet through ZfCf's explicit facet parameter when reading the
`eal` child: `ZfCf::handler<EALParams, ZuFacet::CLI>(ealNode).load(params)` on a
default-constructed EALParams. This loads existing field metadata without
requiring Mutable properties or changing that metadata. Existing CLI argv
generation remains unchanged. The following table records the existing fields;
it does not prescribe additional EAL schema or metadata.

| Cf kind | EAL keys |
| --- | --- |
| String | `lcores`, `numaMem`, `numaLimit`, `hugeDir`, `mbufPoolOps`, `filePrefix`, `traceDir`, `traceBufSize`, `vfioVFToken` |
| StringVec | `allow`, `block`, `vdev`, `driverPath`, `logLevel`, `trace`, `args` |
| Bool, default false | `noPCI`, `noHuge`, `inMemory`, `legacyMem`, `singleFileSegments`, `matchAllocations`, `noShconf`, `noTelemetry` |
| optional UInt32 | `mainLcore`, `memoryMB`, `memoryChannels`, `memoryRanks`, `maxSIMDBits` |
| optional UInt64, existing address format | `baseVirtAddr` |
| Int8 with existing maps and Default sentinel | `iova`, `hugeUnlink`, `procType`, `traceMode`, `vfioIntr` |

`args` remains individual argv tokens, not shell text. Preserve rejection of
conflicting named/raw EAL options and current affinity placement/restoration.
Validate the complete typed configuration before attempting irreversible EAL
initialization. Do not open named interfaces during configuration loading.
After successful init, the caller may destroy its entire configuration tree.

AF_XDP example, with the caller configuring scheduler slot 5 before constructing
the stopped multiplexer:

```text
module /opt/z/lib/libZpkt_xdp.so
interfaces {
  veth0 { spin 1000 txSid 5 }
}
```

DPDK example:

```text
module /opt/z/lib/libZpkt_dpdk.so
eal {
  noPCI true
  noHuge true
  noShconf true
  noTelemetry true
  vdev [ "net_af_packet0,iface=veth0" ]
}
interfaces {
  veth0 { port net_af_packet0 spin 1000 rxInterval 50 }
}
```

`preload` defaults false in both examples. Ring fixtures configure known future
port names and Linux keys before Env initialization, initialize EAL via Env,
then construct stock ring ports and matching dummy links before calling `nif`.
Lazy device opening is necessary for this ordering.

## Loading, errors and global lifetime

Preserve existing initialization, finalization and error behavior while changing
the owner from singleton-managed state to explicit intrusive Env/Plugin lifetime.
Env has no engine or lifecycle state enum. Follow Z's caller-serialized
init/final convention and assert application-owned lifecycle preconditions.

**Application contract:** init and final are not MT-safe and must NOT be invoked
concurrently, either with another call to themselves or with each other. The
application owns serialization and lifecycle ordering; do not add locking,
atomic admission or concurrent-init error handling to implement this contract.
Apply the same setup serialization to named creation/tree mutation.

`Env::init` asserts pre-initialization with an unset m_plugin and an empty child
tree. Successful module initialization installs m_plugin. `Env::nif` asserts
m_plugin is set before either lookup or creation; loading and initialization
are not deferred to nif. No separate initialized flag or Env engine state is
needed. `Env::final` asserts all retained Nifs are already stopped/drained; it
does not stop them or convert that application precondition into a recoverable
validation path. Existing Nif engines, packet-service synchronization and
buffer-return synchronization are unchanged.

1. Parse loader configuration and load a local `ZiModule` without `GC`, adding
   `Pre` only when configured. Resolve `ZpktPlugin`.
2. On resolution failure, unload before invoking any factory and throw the
   original structured loader error with module path and symbol context.
3. Call `(*pluginFn)(cf)`, check the returned fully initialized Plugin pointer,
   and acquire a local `ZmRef<Plugin>`. Publish `m_plugin` only on success.
   There is no post-return initialization call on the Plugin.
4. After any factory invocation, never unload the code, including null results,
   exceptions or plugin initialization failures. The local ZiModule may drop
   its handle without unloading, as required by repository module policy.
5. On entry-point/construction failure, scoped ownership inside the module
   unwinds provisional plugin/EAL resources and preserves the original
   exception. Env publishes no plugin; no partially
   initialized Plugin pointer crosses the module entry point. A null result is
   likewise a structured startup error. Native cleanup keeps its existing
   behavior; changing the owner does not grant another EAL initialization attempt
   or permit unloading module code.

Use the following error contracts, according to permitted call timing rather
than method names:

| API | Timing and error contract |
| --- | --- |
| Module initialization entry point and Env::init | Startup-only; throw ZeException on failure. The entry point returns an initialized Plugin; there is no separate Plugin::init API. |
| Env::nif, Plugin::nif, nifQuery | Potentially called during steady operation; return ZuUnion<Nif *, ZeException> or ZuUnion<NifInfo, ZeException>, respectively. Do not throw through these interfaces. |
| Env::final, Plugin::final | Teardown may occur during program operation; return ZuUnion<void, ZeException>. Destructors log errors and never throw. |
| Zutcp::Instance::init | Preserve its existing result union: it supports repeated initialization of a stopped instance during program operation. |
| Existing packet-service and asynchronous lifecycle APIs | Preserve existing nonthrowing admission, completion and error() contracts; no broad runtime API migration in this refactor. |

Never represent failed creation as a successful null pointer. Preserve native
errors, interface name, native port and module context. A concrete constructor
may use scoped exception cleanup internally, but its factory catches ZeException
and returns it in the union. No partial Nif is published or operational exception
escapes. Startup callers may propagate startup-only initialization exceptions;
they inspect result unions even when calling steady-capable APIs at startup.

For DPDK, EAL is a direct member of the concrete plugin. The application owns
Env; Env creates and releases the plugin explicitly, which constructs and
destroys its EAL member. Neither the plugin nor EAL uses ZmSingleton, an EAL
instance() accessor, a process-cleanup object or a separately owned global EAL.
Keep existing native port-claim and EAL attempt bookkeeping in the backend;
do not introduce new process-global lifecycle state or native reinitialization
support. Preserve EAL's existing destructor cleanup, including its treatment of
native initialization that succeeded before a later initialization error.
Different independent EAL test cases continue to use fresh processes.

### Env finalization of already-stopped children

The application uses existing TCP/standalone Nif start/stop methods, keeping
each scheduler running until its Nifs are stopped/drained. Env participates
in neither start nor stop and needs no asynchronous control callbacks.

After the application has completed child stop and returned dependent buffers,
Env::final first asserts that every retained Nif is stopped/drained, using the
children's existing lifecycle preconditions. It then calls each child's existing
idempotent `nif->final()`. Hold a temporary ZmRef while finalizing/removing each tree
entry, so tree release cannot destroy a child during that call. Preserve native
failure handling and diagnostics; if a child has not finalized successfully,
retain its reference and propagate the error rather than finalizing its plugin
under it. Do not introduce new attachment quarantine or retry owners.

Only after children have been finalized and the tree's references released does
Env delegate finalization to Plugin. The application must release external Nif
references before releasing the final Env reference: Env precedes and outlives
all Nifs, without reverse strong references. Plugin/EAL destruction then follows
intrusive Env/Plugin reference release rather than singleton cleanup scheduling.
Keep EAL's existing native cleanup implementation; do not invent a different
retry protocol. Module code remains resident after object destruction.

## Idempotent interface lookup and core-owned query

Env keeps a ZmRBTree mapping owned names to ZmRef<Nif>, using unique keys and
ZmNoLock. Nodes are allocated only during creation and released deterministically
during retirement/finalization. Each Nif has a raw owning Env backpointer and
its name; it never holds a reference back to Env. Concrete plugins retain their
typed configuration maps, not a second child container.
Do not introduce reservation states, locks or atomic pointers. The caller
serializes these control calls.

1. Env::nif asserts initialization (m_plugin is set), then finds the existing
   name in its tree. If present,
   return its Nif for the same multiplexer, including while running. A different
   multiplexer is a conflicting request. This hit performs no kernel query,
   plugin call, reopening, allocation or state change.
2. On a miss, create during caller-serialized setup, with the supplied
   multiplexer stopped. There is no parent engine state to check.
   The core calls
   the configured m_nifQueryFn(name) exactly once. By default nifQuery resolves the Linux name and
   owns the necessary kernel-interface query and validation. Propagate its
   structured error without invoking a plugin on failure.
3. Pass the successful NifInfo to the concrete `Plugin::nif(mx, name, info)`.
   The backend looks
   up its typed configuration and reports an unknown configured key as an error.
   It trusts the core's multiplexer and kernel-interface information. AF_XDP
   checks its backend-specific configured sender slot; DPDK resolves its native
   port and capabilities. Neither plugin repeats the core's Linux query or
   validates Linux MTU/MAC against DPDK values.
4. Preserve existing native port exclusivity between different configured keys;
   this is resource ownership, not repeat validation of core metadata. Preserve
   existing backend failure/cleanup behavior without a new quarantine protocol.
5. Construct the concrete Nif using the supplied information and owned backend
   configuration. Use scoped cleanup for provisional native resources. There
   is no Nif::init method. Return construction errors as a result union.
6. Env acquires a ZmRef, installs the raw parent pointer and indexes only a
   completely initialized returned Nif.
   Initial state is stopped, initialized, unallocated and never started. On
   failure publish no cache entry and use the existing native cleanup path.

On successful finalization the concrete backend invokes the common Nif
retirement operation, which removes its identity from Env's tree and clears
the raw back-pointer. Verify pointer identity before removing an entry, so an
old finalized Nif cannot remove its replacement. This is a direct core method,
not an exported callback protocol. Repeated finalization is harmless. Every
recipient acquires its own intrusive reference to the returned pointer; lookup
does not transfer another caller's reference or establish an independent TCP
lifecycle owner.

Preserve AF_XDP's existing BPFAttach ownership, guarded detach, finalization error
reporting and destructor behavior. Do not add move support or transfer failed
attachment ownership to configuration records for this refactor. Cache membership
must follow existing native retirement semantics; it does not define a new
cleanup lifecycle or require TCP to change its shutdown error handling.

## Source and build boundaries

Use these exact outputs and source assignments:

| Output | Source ownership |
| --- | --- |
| `zpkt/src/libZpkt.la` | existing ZpktLib; common DeviceEngine/Nif definitions in Zpkt.cc; initialized Env and owning tree; core nifQuery implementation |
| `zpkt/xdp/libZpkt_xdp.la` | concrete XDP plugin/Nif; XDP pool, ring, UMEM, BPF attachment and checksum-metadata code |
| `zpkt/dpdk/libZpkt_dpdk.la` | concrete DPDK plugin/Nif; EAL configuration/native lifetime and DPDK pools; only output with production DPDK native linkage |

Build backend libraries with libtool `-module -avoid-version` in the existing
library installation directory. Retain normal versioning for common libZpkt.
Use distinct backend export headers/macros; do not use `ZpktAPI` to export backend
definitions while those backends import the common interface. Installed public
headers are `ZpktLib.hh`, `Zpkt.hh`, `ZpktPlugin.hh`, and `ZpktEnv.hh`.
Backend headers and the core Nif-query header stay internal, accessible to
repository tests through build include paths only.

Extract common kernel-query implementation into `ZpktNif.hh/.cc` in namespace
`Zpkt`. Rename queryLink to nifQuery, openLink to nifOpen and common interface
geometry from LinkState to NifInfo;
the name-taking nifQuery performs name resolution and the existing rtnetlink
query. Keep the index/fd-taking query operation internal for guarded kernel
attachment operations. Rename kernel-interface members such as m_link/m_linkFD
to m_nifInfo/m_nifFD, without confusing them with the actual Nif object.
Do not introduce ZpktLink files or a separate Link abstraction.

### Backend-use boundary — IR-001 resolved

Anything used only by AF_XDP belongs in libZpkt_xdp; anything needed for both
AF_XDP and DPDK belongs in core. Apply that rule to the existing mixed BPF file:

| Existing functionality | Destination |
| --- | --- |
| Kernel name resolution and ordinary Nif geometry query | Core ZpktNif.hh/.cc, serving both backends through Env. |
| Common rtnetlink socket/send/receive/ACK/error handling | Core ZpktNif.hh/.cc; one implementation shared by core queries and native AF_XDP attachment operations. |
| RTM_SETLINK XDP attribute construction and setXDP | AF_XDP ZpktBPF.hh/.cc. The XDP-specific request capacity and attribute builder stay here. |
| XDP program IDs, occupied-program detection, guarded attach/detach and expected-FD recovery | AF_XDP ZpktBPF.hh/.cc, retaining existing BPFAttach/native ownership semantics. |
| BPF program construction/syscalls, UMEM, XSK rings, checksum metadata | AF_XDP module, in the backend files already listed. |
| Native EAL, port capabilities and mbuf/pool operations | DPDK module, in the backend files already listed. |

Keep the common transaction boundary independent of XDP-specific request types.
Use these internal declarations in ZpktNif.hh:

```cpp
using NifReply = ZtArray<uint8_t, ZtArrayHeapID<"Zpkt.Nif.Reply">>;
ZuUnion<int, ZeException> nifOpen();
ZuUnion<NifInfo, ZeException> nifQuery(ZuCSpan name);
ZuUnion<void, ZeException> nifTransact(
  nlmsghdr &request, NifReply *reply = nullptr,
  bool *rejected = nullptr, ZeError *native = nullptr, int fd = -1);
```

Relocate the existing transact loop into nifTransact, retaining sequence checks,
native errors, ACK handling and borrowed-versus-owned fd behavior. Null reply
means an ACK-only operation; nonnull reply receives the complete matching
RTM_NEWLINK message, beginning at byte zero with length nlmsg_len and owned by
the caller's array. Do not return a view into the transaction's local buffer.
Keep the existing index-taking query helper internal to the core implementation;
the replaceable named query signature remains unambiguous.

Core nifQuery parses ordinary geometry from that reply. AF_XDP uses the same
transport for its own XDP-ID query and XDP attribute updates, with its program-ID
parser and request builder local to the module. Those dynamic guarded ownership
operations do not repeat startup validation of core-supplied geometry. Reuse
the existing code and ownership semantics; introduce no generic netlink framework.
Remove DPDK's ZpktBPF include, queryLink calls, Linux/DPDK equality checks and
duplicate checkPort validation. DPDK has no dependency on the query helper.
Keep checksum discovery/metadata, UMEM and BPF syscalls out of DPDK, and native
mbuf code out of common.

Traverse common src, xdp, optional dpdk, test, util, then itest. Register the two
backend Makefiles in configure.ac and retain the existing Linux/DPDK conditions.
Document xdp/dpdk as module-specific source directories in zpkt/README.md and
GUIDELINES.md. Preserve the current Linux-header checks; no configure redesign
or unrelated dependency changes are part of this refactor.

Tests of private pools/classification may link their one backend directly and
include its private headers. Production callers and the loader contract test
link only the common library. The DPDK integration fixture may still link DPDK
and its ring PMD to construct the simulated wire. Keep its allocation/start
interposition and `RTLD_NEXT` path effective with `preload false`; explicitly
test that injected failures reach calls made from the loaded module. Do not
claim that `preload true` preserves symbol interposition.

## Exact file actions

Generated Makefiles, dependency files and header-link output follow the existing
build machinery; do not hand-edit them. Preserve unrelated working-tree changes.

Create common files:

```text
zpkt/src/ZpktEnv.hh
zpkt/src/ZpktEnv.cc
zpkt/src/ZpktPlugin.hh
zpkt/src/ZpktNif.hh
zpkt/src/ZpktNif.cc
```

Move and edit the following backend files; old paths disappear at completion:

| Existing path | Destination |
| --- | --- |
| AF_XDP portion of `zpkt/src/Zpkt.cc` | `zpkt/xdp/ZpktXDP.cc`; common portion stays in src |
| `zpkt/src/ZpktPool.hh`, `.cc` | `zpkt/xdp/ZpktPool.hh`, `.cc` |
| `zpkt/src/ZpktRing.hh` | `zpkt/xdp/ZpktRing.hh` |
| `zpkt/src/ZpktUMem.hh`, `.cc` | `zpkt/xdp/ZpktUMem.hh`, `.cc` |
| `zpkt/src/ZpktBPF.hh`, `.cc` | `zpkt/xdp/ZpktBPF.hh`, `.cc`, after core Nif-query extraction |
| `zpkt/src/ZpktTx.hh`, `.cc` | `zpkt/xdp/ZpktTx.hh`, `.cc` |
| `zpkt/src/ZpktDpdk.hh`, `.cc` | `zpkt/dpdk/ZpktDpdk.hh`, `.cc`; internal EAL/config only |
| `zpkt/src/ZpktDpdkInstance.hh`, `.cc` | `zpkt/dpdk/ZpktDpdkNif.hh`, `.cc` |
| `zpkt/src/ZpktDpdkPool.hh`, `.cc` | `zpkt/dpdk/ZpktDpdkPool.hh`, `.cc` |

Create backend control/build files:

```text
zpkt/xdp/Makefile.am
zpkt/xdp/ZpktXDPLib.hh
zpkt/xdp/ZpktXDPCf.hh
zpkt/dpdk/Makefile.am
zpkt/dpdk/ZpktDPDKLib.hh
zpkt/dpdk/ZpktDPDKCf.hh
zpkt/dpdk/ZpktDPDK.cc
```

Put XDP plugin and its private configuration/map implementation in ZpktXDP.cc;
put the DPDK plugin and its private configuration/map implementation in
ZpktDPDK.cc. Keep
backend-specific details out of the common Plugin interface. Rename concrete
device classes to `Xdp::Nif` and `Dpdk::Nif`, updating pool friendships.

Modify build inputs:

```text
configure.ac
zpkt/Makefile.am
zpkt/src/Makefile.am
zpkt/test/Makefile.am
zpkt/util/Makefile.am
zpkt/itest/Makefile.am
zutcp/src/Makefile.am
zutcp/test/Makefile.am
zutcp/util/Makefile.am
zutcp/itest/Makefile.am
zutcp/example/Makefile.am
zutcp/bench/Makefile.am
zfix/bench/Makefile.am
zfix/itest/Makefile.am
zfix/interop/Makefile.am
```

For each production program, provide build-tree module paths with quoted
per-target CPPFLAGS for its configured backend; use installed sonames/explicit
configuration outside build-tree fixtures. Do not install an absolute build
directory in a public header or production default. Remove DPDK compile/link
dependencies from the FIX benchmark's application path after EAL moves behind
Env. Tests that construct native ring PMDs retain their explicit native linkage.

Modify common/TCP code and direct callers:

```text
zpkt/src/Zpkt.hh
zpkt/src/Zpkt.cc
zutcp/src/Zutcp.hh
zutcp/src/Zutcp.cc
zpkt/util/zpktpacket.cc
zutcp/util/zutcppair.cc
zutcp/bench/zutcpbench.cc
zutcp/example/zutcp.cc
zutcp/example/zutcpd.cc
zfix/bench/zfixbench.cc
zfix/itest/transport_fixture.hh
zfix/interop/load_afxdp.hh
```

Make Env a lifetime-owned member of persistent caller/fixture state; do not put
it on a helper function's stack when the multiplexer/TCP survives that helper.
Extend the native FIX interop owner state in `zfix/util/load_native.hh` and
`zfix/util/load_native.cc` to retain its Env through disconnect/finalization.
Keep existing command-line behavior by translating parsed options into a ZfCf
tree and then using the same typed plugin loader, rather than adding a second
backend construction API. Module selection uses the configured path, not
`HAVE_DPDK` branches in application source.

Modify shared test infrastructure and callers:

```text
zutcp/test/ZutcpTestInstance.hh
zutcp/test/ZutcpUDPLeaseTest.hh
zutcp/test/ZutcpChecksumTest.cc
zutcp/test/ZutcpRuntimeTest.cc
zutcp/test/ZutcpDataTest.cc
zutcp/test/ZutcpHandshakeTest.cc
zutcp/itest/zutcpdpdkfixture.hh
zutcp/itest/zutcpdpdkfixture.cc
zutcp/itest/zutcpdpdktest.cc
zutcp/itest/zutcppairtest.cc
zutcp/itest/zutcppooltest.cc
zutcp/itest/zutcpudptest.cc
zpkt/itest/zpktfixture.hh
zpkt/itest/zpktdkfixture.hh
zpkt/itest/zpktdkfixture.cc
zpkt/itest/zpktpackettest.cc
zpkt/itest/zpktwaketest.cc
zpkt/itest/zpktfiltertest.cc
zpkt/itest/zpktdkpackettest.cc
zpkt/itest/zpktdkwaketest.cc
zpkt/itest/zpktdklooptest.cc
```

The TCP test double takes its multiplexer in construction, not through a
replacement init override. Its fixture supplies an initialized test Env using
the test plugin below; repeated apply retains the existing device. Preserve
deferred start/stop, burst, checksum and lease test behavior.

Modify isolated backend tests for private include/link changes:

```text
zpkt/test/ZpktInstanceTest.cc
zpkt/test/ZpktPoolTest.cc
zpkt/test/ZpktRingTest.cc
zpkt/test/ZpktClassifierTest.cc
zpkt/test/ZpktOffloadTest.cc
zpkt/test/ZpktDpdkInstanceTest.cc
zpkt/test/ZpktDpdkPoolTest.cc
```

Create these test sources:

```text
zpkt/test/ZpktEnvTest.cc
zpkt/test/ZpktTestPlugin.cc
zpkt/test/ZpktNoFactory.cc
zpkt/test/ZpktTestCf.hh
zpkt/itest/zpktniftest.cc
```

Build test-only modules from ZpktTestPlugin.cc with distinct build flags for
normal, null-result, entry-point failure and plugin-construction/EAL-init failure
cases; ZpktNoFactory.cc
omits the symbol. They exercise the loader without packet privileges. Install
none of them. ZpktTestCf.hh supplies typed test configuration/tree construction
shared by native fixtures; no hand-written shell parsing.

Update affected contracts and documentation:

```text
zpkt/GUIDELINES.md
zpkt/INTERFACE.md
zpkt/README.md
zpkt/test/README.md
zpkt/itest/README.md
zutcp/GUIDELINES.md
zutcp/README.md
zutcp/test/README.md
zutcp/itest/README.md
zutcp/bench/README.md
zfix/bench/README.md
zfix/interop/README.md
```

Do not edit the baseline zpkt_plugin.md to hide variations. Keep this plan as
the explicit record of those variations. No unrelated transport, Zi, or Zf
refactor belongs in the write scope above.

## Rebuild protocol and scope

For **every rebuild**, follow [REBUILD.md](REBUILD.md), restricted by the user's
scope below. This replaces this plan's earlier top-level rebuild, configuration-
matrix and generic acceptance-build instructions. This section governs every
implementation checkpoint and final verification run, not only the last build.

Rebuild only zutcp, zfix, and modules they depend on directly or transitively,
and never any module beyond zfix in the top-level traversal. From the current
module Makefile.am dependencies, the allowed set is:

```text
zu zm zt ze zf zi zdbus ztcp ztls zpkt zutcp zfix
```

Apply the top-level Makefile.am/current-configuration gates as REBUILD requires;
an allowed module is not authorization to enable it. The zpkt/xdp and zpkt/dpdk
outputs are part of zpkt, with DPDK included only when already enabled.
Do not rebuild zpb, zfb, zv or zrl merely because they appear before zfix.
Do not rebuild zquic or any later module. A dependency appearing outside this
allowed closure is a scope discrepancy to report, not permission to broaden it.

| REBUILD phase | Restricted work |
| --- | --- |
| 1 | Build/test zu, zm, zt, ze, zf, zi in dependency order; omit zv because neither target depends on it. |
| 2 | Build/test ztcp after Phase 1. |
| 3 | Build/test configured zdbus and ztls after Phase 1, preserving their dependency order and gates. |
| 4 | Skip zquic entirely. |
| 5 | Build/test zpkt, including configured native plugins and documented private integration fixtures, after Phase 1. |
| 6 | Build/test zutcp after Phases 2, 3 and 5, including its documented integration and independent kernel-TCP interoperability suites. |
| 7 | Build/test zfix and its configured zfixbench; perform the documented kernel TCP/AF_XDP comparison and DPDK comparison when enabled. Stop here. |
| 8–20 | Out of scope; do not execute. |

Use REBUILD's subagents and one shared make jobserver with a total concurrency
budget of eight across all workers. Phases 2, 3 and 5 may overlap after Phase 1;
respect the later dependency barriers. Recursive/subagent makes reuse that same
jobserver rather than each creating an independent -j8 pool. Use module/target-
scoped make invocations; never run an unrestricted top-level all/test/install/
clean target that traverses unrelated modules. Build each relevant source layer
and default target before its dependent tests, as required by docs/BUILD.md.

Use **only the current build configuration**, retain existing build products,
batch expensive rebuilds, and do not clean unnecessarily. Do not switch to a
different compiler, release/debug/sanitizer mode or DPDK-enabled/disabled matrix.
If new Makefile.am inputs require regeneration, preserve the current configured
options and restrict subsequent compilation to the allowed modules. Installed-
path verification/staging must likewise use only scoped allowed targets.

Honor REBUILD's phase freeze points after the corresponding final phase checks
pass. Keep development checkpoints distinct from a completed REBUILD phase:
an intermediate pre-refactor build does not freeze that module's unfinished
implementation. Batch the planned edits before the final ordered verification
pass; do not mark an upstream phase complete and then edit it in a later phase
of that same pass. Preserve unrelated working-tree changes and do not use
REBUILD's repair instructions to expand this plan's implementation write scope.

Follow REBUILD for listener/debugger execution, native fixture setup, Valgrind
memory-corruption diagnosis, and removal of phase-owned temporary/test/crash
residue. Keep only this task's residue in that cleanup scope. Record actual
commands, results and environment-caused skips. This document change adopts
the protocol; it does not itself execute a rebuild.

## Buildable implementation phases

### Phase 1: extract core Nif-query support

Create ZpktNif.hh/.cc, move the kernel-query and shared rtnetlink transaction
definitions out of ZpktBPF.hh/.cc, and rename the interface metadata/query names
and their current internal call sites. Add the common source to the existing
monolithic build. Preserve existing construction behavior at this extraction
checkpoint; transferring query responsibility to Env and removing DPDK's
duplicate checks occur atomically in Phase 2. Do not add a queryLink forwarding
alias. Backend file moves also follow in Phase 2.

Checkpoint: rebuild under the restricted REBUILD protocol above; existing zpkt
unit tests and relevant packet integration tests pass against the current
construction API. Reuse unchanged dependencies and existing build products.

### Phase 2: replace construction and migrate every caller atomically

Split the physical backend outputs as specified above. Implement Env, Plugin,
typed maps, Env-owned ZmRBTree/idempotent lookup/nifQuery and stopped-child finalization,
constructors and member-owned
EAL. Remove every plugin Linux query and Linux/DPDK agreement check. Pass the
core's NifInfo into the concrete plugin factory and update its result-union signature.
Rename the base; remove enum/union/factory/init APIs. Change TCP's exact signature
and migrate the listed callers, test doubles, pool-only tests and fixture owners
in the same checkpoint. Do not leave optional DPDK or FIX targets on the old ABI.
Use direct private BufPool construction in isolated pool tests; do not open a
real interface just to manufacture pool metadata.

Move real-device lifecycle assertions from ZpktDpdkInstanceTest into
zpktniftest; leave argv/affinity/EAL-attempt cases in isolated child processes.
Both ring and pcap integration fixtures create kernel dummy Nifs with the
intended routing MTU/MAC. Preserve the independent simulated wire and its destination Rx-pool
copy. No real network routing is delegated to the ring PMD.

Checkpoint: the restricted REBUILD pass builds the allowed dependency closure
and passes zpkt/zutcp unit tests in the current configuration, including DPDK
only if enabled. Do not reconfigure to exercise another DPDK mode.
No removed API references remain outside historical documents and this plan.

### Phase 3: loader, lifecycle and failure verification

Complete the test cases below and run native integration fixtures. Fix only
failures attributable to the refactor. Verify injection actually fires rather
than merely accepting a green happy-path test. Check installed module loading
in addition to build-tree loading. Update all listed contracts/examples.

Checkpoint: REBUILD Phases 5–7's in-scope native zpkt/zutcp/FIX tests and scoped
build/install dependency checks pass; no production target directly links a
native backend. Retain earlier phase results where inputs remain unchanged.

### Phase 4: current-configuration acceptance and publication preparation

Complete the restricted REBUILD protocol through Phase 7 in the current
configuration. Validate configured DPDK behavior and scoped installed application/
common/module/header compatibility as one revision. Use REBUILD's Valgrind
guidance for memory-corruption diagnosis; do not initiate other build modes.
Record actual test results and environment-caused skips in the final change
description. Do not call an unavailable hardware offload case verified.

## Concrete verification assertions

| Test location | Required assertions |
| --- | --- |
| ZpktEnvTest | Missing module, missing symbol, null/throwing factory and init failure preserve structured errors; no partially ready Env; final is idempotent; success module remains callable after the local ZiModule leaves scope. |
| ZpktEnvTest with nifQueryFn mock | The plain-pointer setter survives init. Synthetic metadata reaches the test plugin unchanged. A cache hit makes no second query; a mock error makes no plugin call. Factory/tree/finalization tests use no real kernel interface or privileges. Native integration uses the real default. |
| ZpktEnvTest and test plugin | Drop the source Cf tree after init; later factory sees owned values. Unknown key errors. Repeated lookup with the same name/mx returns the same pointer without reopening, including while running; another mx errors without changing it. Failed construction leaves no cached Nif. Finalization permits recreation, and an old finalized Nif cannot clear its replacement's pointer. |
| ZpktEnvTest and test plugin | Env's unique-name ZmRBTree owns Nif references; each Nif has a raw Env pointer. nif requires completed initialization. The caller stops/drains children; Env final asserts that condition and calls every retained child's idempotent final before Plugin. Repeated child finalization remains harmless. Env has no engine/start/stop. Last Env reference release drives destruction after all Nifs. |
| zpktniftest | AF_XDP factory returns initialized/unallocated/stopped Nif; validates scheduler slot; bad Linux name leaves no opened resources; wake, backing, restart, checksum and owner behavior retain current assertions. |
| zpktniftest | The core resolves/queries the dummy kernel Nif once; `port` selects an independently named ring/pcap port. A core query failure never calls the plugin. The plugin uses supplied metadata with no Linux query or MTU/MAC equality check. Missing native port publishes no cached Nif. |
| zpktniftest | Preserve existing detach-failure, guarded attachment, finalization and destructor assertions. Do not add configuration-owned quarantine or a new retry owner. |
| ZpktDpdkInstanceTest | Preserve named/raw option conflicts, ordering, affinity restoration, native init failures and exactly one process-wide EAL attempt. Cover native-init success followed by later failure and cleanup. Each independent case uses a fresh child process. |
| zutcp unit/shared fixtures | Success consumes the Nif reference; failure preserves it. Repeated empty-reference init with identical Env/mx/config succeeds; changed Env or configuration fails before any new factory call. |
| zutcpdpdktest | Preserve allocation rollback and activation failure via interposed native functions through the loaded plugin with preload false; preserve lease retirement, restart, finalization and independent Linux identity. |
| private pool/offload tests | Preserve no-privilege pool geometry, exhaustion, payload-overlay recycling, native slot identity, checksum metadata alignment and fallback; no public cold device factory is retained for testing. |
| build/install checks | Common libZpkt and production Zutcp/FIX binaries have no direct DPDK or native-plugin dependency. Each currently built backend loads without the other's file. When the current build omits DPDK, a missing DPDK module produces the normal loader error; do not reconfigure to test another mode. Use only allowed module/target rebuild and staging operations. |

All build/test execution follows REBUILD.md as restricted in this plan, with
docs/BUILD.md supplying module command conventions. Preserve the configured
prefix/options; run only scoped source/default targets before the corresponding
test targets, using the shared eight-job jobserver. Use libtool exec for Memcheck.
Network namespace, AF_XDP and locked-memory fixture permissions remain native
test prerequisites; record exact failures/skips rather than weakening assertions.

## Implementation-readiness gaps

These IDs are stable. Keep each ID when resolving or recording its disposition;
do not renumber remaining gaps after removing another item from the open list.
V10 remains resolved and is not reopened by these gaps.

| ID | Status | Gap |
| --- | --- | --- |
| IR-001 | Resolved | Apply the user's backend-use rule: common Nif query/rtnetlink transport stays in core; XDP-specific request construction, program-ID parsing and guarded attachment remain in libZpkt_xdp. Internal signatures and owned reply-buffer semantics are specified above. |
| IR-002 | Resolved | Env owns a name-keyed ZmRBTree of ZmRef<Nif> and outlives all children; each Nif has a raw Env backpointer. Env has no engine or start/stop. nif requires successful initialization. App must not invoke init/final concurrently. final asserts all Nifs are already stopped/drained and calls their idempotent final operations. |
| IR-003 | Resolved | Env::nifQueryFn accepts a plain function pointer matching nifQuery, defaults to the real query, and permits deterministic synthetic success/error results in isolated factory tests. |

## Packet-service invariants

No new packet-path module/map lookup, payload copy, allocation, lock, owner
reference increment or scheduler thread is permitted. Preserve existing Rx/Tx
ownership, sender-slot protocol, CQ/driver versus caller leases, `txReady`,
checksum-offload fallback, exact pool allocation, backing retention over restart,
and scheduler-running stop. All new work described here is construction,
configuration, loading or deterministic teardown work.

## Execution record

This historical record describes plugin implementation verification before the
breaking package/API rename. Paths use current names for navigation; its ELF
fingerprints belong to that earlier revision. Rename verification is recorded
in the module README.

Implementation and the final ordered native/FIX acceptance pass are complete.
All gates below passed on the current configuration with unchanged runtime
fingerprints and no final failures or skips.

The implementation adds intrusive Env ownership, typed owned backend maps,
constructor-opened Nifs, the replaceable core query and name-keyed retirement,
separate unversioned native modules, and the explicit TCP Env/Nif handoff. All
listed production callers, private fixtures and affected contracts were migrated.
The baseline `zpkt_plugin.md` was preserved.

A shared internal `zpkt/src/ZpktCf.hh` guards map allocation with `ZuPtr` while
reusing the existing `ZfCf::AsMap` loader and save implementation. This fixes a
reproduced malformed-record startup leak without changing Zf or adding a parser.
DPDK validates every owned port string before EAL; EALParams and its metadata
were compared against the original and are unchanged apart from export macros.

### Configuration and build scope

The existing configuration was preserved: clang 21.1.8, x86_64 Linux,
`-march=x86-64-v2 -O3 -g -DNDEBUG`, prefix `/usr`, DPDK enabled, PostgreSQL
disabled, and existing SQLite/Gtk/QuickFIX options. No clean, compiler change,
sanitizer/release matrix or out-of-scope module rebuild was performed by this
task. Autotools regeneration used `autoreconf -fi`, `config.status --recheck`
and `config.status`, retaining the original configured arguments.

Builds were scoped to `zu zm zt ze zf zi zdbus ztcp ztls zpkt zutcp zfix`.
Source/default targets preceded their tests. The final builds used one GNU make
4.4.1 FIFO jobserver with eight slots and a wrapper that reserves a FIFO token
for each externally launched make's implicit slot. This corrected the initial
development dispatch, which inherited the FIFO but did not reserve those
implicit slots. No unrestricted top-level all/test/install/clean target ran.

The actual scoped command pattern was:

```sh
# Initial dependency checks inherited MAKEFLAGS from the shared jobserver.
for module in zu zm zt ze zf zi ztcp zdbus ztls; do
  make -C "$module/src"
  make -C "$module"
  make -C "$module/test" test
done
# Final checkpoints used the corrected token-reserving wrapper:
# shared_make was /tmp/zpkt-plugin-work/make, attached to the same jobserver.
shared_make -C zpkt
shared_make -C zpkt/test test
shared_make -C zutcp
shared_make -C zutcp/test test
shared_make -C zfix
shared_make -C zfix test
```

Earlier results were retained only where their tested inputs were unchanged.
Implementation checkpoints and the final ordered verification pass were kept
separate. Final native phases run serially with all binary writers stopped.

### Verified gates

| Gate | Result |
| --- | --- |
| zu | 42 programs / 974 TAP tests, PASS |
| zm | 40 / 326, PASS |
| zt | 17 / 394, PASS |
| ze | 1 / 1, PASS |
| zf | 12 / 353, PASS |
| zi | 22 / 158, PASS |
| ztcp | 3 / 7, PASS |
| zdbus | 7 / 26, PASS |
| ztls | 21 / 124, PASS |
| zpkt units | 8 / 20, PASS; includes malformed-map rollback and three fresh-process invalid-port-before-EAL cases |
| Zutcp units | 23 / 330, PASS |
| Final zpkt native phase | All 13 serial commands PASS, with no skips; actual query counts and independent Linux/native identity included |
| Final Zutcp native phase | 7 programs / 97 tests PASS, including all 107 stack assertions; kernel interoperability 36 PASS; four IPv4/IPv6 UDP/mixed commands PASS; DPDK 52 assertions PASS |
| Zfix units and kernel suites | Main units 29 / 390 and sign 1 / 9 PASS; kernel TCP 6 / 130, TLS 6 / 135, demo 1 / 2 and native I/O 1 / 1 PASS; schema compile fixtures and interop codec/load checks PASS |
| Final FIX AF_XDP suites | TCP: 6 programs / 125 tests PASS; TLS: 6 / 133 PASS |
| Final FIX canonical interoperability | Five TAP groups and all 76 detailed cases PASS |
| Production/common linkage | Nine current ELF outputs checked with readelf; no direct libZpkt_xdp/libZpkt_dpdk or librte dependencies |
| Release comparison | All six IPv4/IPv6 × kernel/AF_XDP/DPDK cases PASS; performance_comparison=1 and exact=1 at all twelve endpoints |
| Staged public API | Only ZpktLib.hh, Zpkt.hh, ZpktPlugin.hh and ZpktEnv.hh installed; an external consumer compiled, linked and ran against staged headers/libraries |
| Staged module loading | XDP and DPDK each initialized/finalized from a library directory without the counterpart module; loader tracing confirmed staged common/module resolution |
| Memcheck | Normal loader/lifecycle tests and malformed configuration through each real staged plugin: zero errors and zero definite/indirect/possible lost blocks |

The zpkt native commands were fresh private fixtures for each packet case:

```sh
for case in packets sender leases unstarted offload; do
  sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
    env ZPKT_PACKET_CASE="$case" ./zpkt/itest/zpktpackettest
done
for variant in 0 1 2; do
  sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
    env ZPKT_PACKET_CASE=metadata ZPKT_METADATA_CASE="$variant" \
    ./zpkt/itest/zpktpackettest
done
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh ./zpkt/itest/zpktfiltertest
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh ./zpkt/itest/zpktwaketest
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
  env ZPKT_NIF_CASE=xdp ./zpkt/itest/zpktniftest
sudo -n env ZPKT_RUN_FIXTURE=1 ./zpkt/itest/zpktlooptest
sudo -n prove -v ./zpkt/itest/zpktdkpackettest ./zpkt/itest/zpktdkwaketest \
  ./zpkt/itest/zpktdklooptest ./zpkt/itest/zpktniftest
```

Scoped installation used `shared_make -C <allowed-module>/src install
DESTDIR=/tmp/zpkt-plugin-work/stage`, plus the two zpkt backend directories,
in dependency order. The installed consumer used the configured compiler,
CPPFLAGS, AM_CXXFLAGS and CXXFLAGS, only staged public include paths, and staged
libraries. Memcheck used `libtool exec valgrind --tool=memcheck
--leak-check=full --show-leak-kinds=definite,indirect
--errors-for-leak-kinds=definite,indirect --error-exitcode=99`.

The startup leak was reproduced before repair as 192 bytes definitely lost and
2,048 bytes indirectly lost for an actual missing-required-field configuration.
The same reproducer now verifies the original `interfaces.invalid.txSid` / `port`
diagnostic through both real plugins with zero lost blocks. Additional tests
cover malformed UInt values, partially populated maps and successful retry.

The final Zutcp commands were:

```sh
sudo -n unshare --mount --net --propagation private \
  bash zutcp/util/zutcpfixture.sh bash zutcp/itest/zutcpmxfixture.sh \
  bash zutcp/itest/zutcpipfixture.sh env ZI_LOGDIR=/tmp/zpkt-plugin-work/final6-residue \
  bash -c 'cd zutcp/itest && prove -v zutcppairtest zutcppooltest zutcpstacktest zutcpiptest zutcpmxtest zutcpapptest zutcptlsapptest'
sudo -n unshare --mount --net --propagation private \
  env ZPKT_RUN_FIXTURE=1 shared_make -C zutcp/interop test
for family in 4 6; do
  for mode in udp mixed; do
    sudo -n unshare --mount --net --propagation private \
      env ZPKT_RUN_FIXTURE=1 bash zutcp/itest/zutcpudpfixture.sh "$family" "$mode"
  done
done
sudo -n prove -v ./zutcp/itest/zutcpdpdktest
shared_make -C zfix/itest afxdp-test
sudo -n unshare --mount --net --propagation private \
  bash zfix/interop/afxdp-fixture.sh python3 -B zfix/interop/matrix.py \
  --afxdp --profile canonical --artifacts /tmp/zpkt-plugin-work/phase7-canonical
```

### Release comparison and provenance


The existing release configuration was reused without rebuilding in another
mode. The actual final command was:

```sh
sudo -n env 'BENCH_BACKENDS=kernel native dpdk' BENCH_APP_COLOCATE=1 bash zutcp/bench/run-compare.sh zfix/bench/zfixbench /tmp/zpkt-plugin-work/phase7-comparison release
```

All rows used one connection, pipeline 1, 1,000 warmup and 10,000 measured order
messages, reserve 4,096 and spin 1,000. Client Rx/Tx CPUs were 0/1 and server
Rx/Tx CPUs 2/3, with managed application callbacks colocated on those owners.
Every endpoint reported exact=1 and 11,000 received messages. These are software
veth/AF_PACKET comparison observations, not a physical-NIC performance claim.

| IP family | Backend | Requests/s | Median µs | p99 µs |
| --- | --- | ---: | ---: | ---: |
| IPv4 | kernel TCP | 22924.940 | 42.086 | 58.251 |
| IPv4 | AF_XDP | 22402.827 | 42.741 | 64.940 |
| IPv4 | DPDK AF_PACKET | 10637.011 | 76.374 | 149.946 |
| IPv6 | kernel TCP | 23101.619 | 41.701 | 58.250 |
| IPv6 | AF_XDP | 22397.771 | 42.777 | 66.079 |
| IPv6 | DPDK AF_PACKET | 10182.380 | 76.952 | 153.020 |

The performance run waited 92.794 seconds for unrelated external build activity
to stop. Thirteen during-run process samples plus before/after checks showed
no overlapping builds/tests. Unrelated processes/files were left untouched.
Thirty-five configured dependency/native/application ELF fingerprints and the
configuration were identical before and after final FIX verification. Four
library and 23 program fingerprints were likewise unchanged across final zpkt
and Zutcp native verification.

Final ELF SHA256 values (paths relative to the repository root):

| ELF | SHA256 |
| --- | --- |
| zu/src/.libs/libZu.so | 84c26750c212256f9113d943fd48872fbb3cc34737b41233ab002732709cb846 |
| zm/src/.libs/libZm.so | bb25414f9cf065f9151f7e179283989d156b3ed708791232a3ff11054b45b568 |
| zt/src/.libs/libZt.so | cf4fdfa6a1a3dd29c83d08ea23e5f2c20d00586e51995b7235241dee3db0ae5c |
| ze/src/.libs/libZe.so | 73b3b483fb5997bb5c6700298db605c0bbb6ab3cad026f2b19f450762469a1d7 |
| zf/src/.libs/libZf.so | 9413f0e982b6426e5d9105c170ecdc61aa24a5c46b910df0a086093c05f855fc |
| zi/src/.libs/libZi.so | 011c1b3766b11f7c1b08af4771ef53bb9b55eb37a01856ceda0f2ed87b42d4c5 |
| ztcp/src/.libs/libZtcp.so | 3aa4cb778632410248eae7f0693ee0ec869034a1fb7ae68a99542786e0691e8e |
| ztls/src/.libs/libZtls.so | f879f3b3c5c4889f21219bbdaf4abb8ca604a5986541226862206a8d17467a34 |
| zpkt/src/.libs/libZpkt.so | 6ec112297173a3d967ce668781473f3fbc954487a677aee4353c2760b8e0e0ff |
| zpkt/xdp/.libs/libZpkt_xdp.so | 71606e0c15ad750d4038b878770ff11a06d22416e838486f91c6bbb7719de72c |
| zpkt/dpdk/.libs/libZpkt_dpdk.so | 3882fb51f5d34eb02af4e30c12320b3e0027457190c9c7081ff32c0bb487e58f |
| zutcp/src/.libs/libZutcp.so | f578a128a913d0d013f00a93bca8db350a137022c4769d7615bbba9b2e006983 |
| zfix/src/.libs/libZfix.so | d857c12a109ffa93151068a3d634962f2287e7b516480cd1e366325d016165fe |
| zfix/src/.libs/libZfixFmt.so | 3c2a5119b9ccfc0fcb1afc4148d8bd10a09763a578f709a4315370e9a1a1a671 |
| zutcp/util/.libs/zutcppair | d446520f11729072fc8cb3890250edbb3b4800fad72563fa6c502d2dd38ff342 |
| zutcp/example/.libs/zutcp | 9925c5dd1b811305d3f11bbce86d73ae905941737c1df0ba66b1937080b1352e |
| zutcp/example/.libs/zutcpd | 4b10dc3748a3d5b40134e605b994e7b49da0bf6a5e33a74fee68627faa9cf385 |
| zutcp/bench/.libs/zutcpbench | 5a676619bc7df0303f9d9007ef5bf8fde09a68e9fcd8a9dfab7db2bc898ca0be |
| zfix/bench/.libs/zfixbench | df7dd2e8c3766625b2127591c5fe49b61e1eeec7dae7ad1f1844e7f59193732a |
| zfix/interop/.libs/afxdp_zfix | ca458add78798d25a4ce6d881d93cb9074c38e0389d62c3885e720bba994f8b4 |
| zfix/interop/.libs/afxdp_zfixd | e541a0bb70eb3dae9719267b1cc30d3b97bc2b47c40c6cd0873d3bd44135552e |
| zfix/interop/.libs/quickfix | 1e0d6785e9c5dbb8856b250d0b061e71a350c090e5cb670b23735b7c3fa1fb6b |
| zfix/interop/.libs/quickfixd | 04d724ed00819c1605c5902921129a69ab8356bb046fcb3b26711d52895c8120 |
| zfix/itest/.libs/afxdp_tcp_test | 21df5f1d788ef0a3c7ba1f2ef1ce8f451a9cadd8cea7701e90f7fcbf8da19791 |
| zfix/itest/.libs/afxdp_ingress_tcp_test | 738ec5ccdf8a512487bcacfb84c9e4eb92fcb2e2998fb9eae2f8ac607b666c8f |
| zfix/itest/.libs/afxdp_raw_tcp_test | b3c65284f311c9fbd863f596e48bf877f730e8a34c07ce4060553420ac5e2f30 |
| zfix/itest/.libs/afxdp_reset_tcp_test | dd43ad568e98bfdf2b32268ea260a3c369b0eef970faa222da7c4860cbcced24 |
| zfix/itest/.libs/afxdp_session_tcp_test | 3360f49ff0fca2b252e4169d0ad4e3e06dc063ca1dd038345ce8c62db87b621f |
| zfix/itest/.libs/afxdp_recovery_tcp_test | 95baf278b68045385b50dd4ec700f2179b57a28d07524c9caee17d3ecfcd42ac |
| zfix/itest/.libs/afxdp_tls_test | 4ac6c32ce16b11966dbd07e12f58c030c88e438d70ca8fadecb6472e0802b3c6 |
| zfix/itest/.libs/afxdp_ingress_tls_test | 2774ddfb86b92c0d6b8755914316e4cd29f58b9d406c87ed6315d353c6ff29f3 |
| zfix/itest/.libs/afxdp_raw_tls_test | 9b7bfe632db134f2fb5f033d307e898d92d9ef7a884f33220e812a3c97f02bea |
| zfix/itest/.libs/afxdp_reset_tls_test | 85aca91c34322f69d2e16bcd975b61218a8dd3fbc750b2cfbf3833823d7a85af |
| zfix/itest/.libs/afxdp_session_tls_test | 559434bd75a1decf02ab156a29ceb0554941166f56508093722ff3e436aac3bd |
| zfix/itest/.libs/afxdp_recovery_tls_test | 8994b1dcd3c8aefb466d1f80536a3b20e463bc2ee291459f278132971193b784 |

### Development observations and limits

Overlapping development relinks caused transient missing/invalid ELF loader
errors; terminal build barriers precede final runtime checks. A release-only
native fixture read a compiled-out DiagCounter; it now checks the authoritative
Tx retransmission queue without changing production code or assertions.
Intermittent pre-final TLPv6 and FIX stop-drain timeouts were preserved for
review; timeouts and protocol code were not weakened. The unchanged full pair
suite subsequently passed all 28 cases. The final complete native suite and
isolated FIX AF_XDP suites also passed with their original timeouts.

Physical NIC DMA and hardware checksum offload were not exercised. The veth,
stock ring/pcap/AF_PACKET and emulated checksum-metadata checks prove their
specified software/native contracts, not physical hardware capability.

Task-owned temporary fixture, staging, diagnostic and jobserver files were
removed after recording these results. Existing build products and unrelated
working-tree changes were retained. The live configured prefix was not replaced;
installed compatibility was verified in scoped DESTDIR staging.
