# `zdbus`: Linux D-Bus protocol plan

## Scope and public names

Create Linux-only `zdbus`, using a nonblocking `AF_UNIX` stream owned by
`ZiEventLoop`.  It is a native D-Bus implementation connecting applications
to an existing session or system bus.  The implemented `ZfDBUS` codec supplies
all header-value and body binary serialization through `ZfDBUS::signature`,
`save`, `handler`, `View`, `Context`, and `Any`; see `zdbus1.md`.  `zdbus`
owns framing, routing, serials, lifecycle, and the application-facing header
interface.  No header catalog or protocol policy is added to `ZfDBUS`.

All implementation types live in `namespace Zdbus_`.  Public headers expose
application-facing `ZdbusX` aliases, including `ZdbusClient`, `ZdbusServer`,
`ZdbusConnection`, `ZdbusAddress`, and the `ZdbusReqBuilder`,
`ZdbusResParser`, and related adapter aliases.  Applications need not name
`Zdbus_`.

`ZdbusClient` connects, authenticates, calls methods, and receives returns,
errors, and subscribed signals.  `ZdbusServer` is a service-side bus client:
it connects and authenticates, performs `Hello`, acquires well-known names,
dispatches calls, returns results/errors, and emits signals.

`ZdbusServer` owns or privately composes a `ZdbusClient` connection core.
This reflects the bus topology--a service is also a client of the bus--and
keeps connection serial allocation encapsulated in `ZdbusClient` instead of
duplicating it in the server facade.

This release excludes a message-bus daemon, activation, policy, direct peer
listeners, TCP/nonce transports, Windows, UNIX_FD/`SCM_RIGHTS`, reconnection,
and implicit name reacquisition.  The D-Bus protocol's required initial NUL,
EXTERNAL authentication, `Hello`, framing, and message semantics follow the
[D-Bus specification](https://dbus.freedesktop.org/doc/dbus-specification.html).

## Build and event loop

1. Add `zdbus/{src,test,util,itest,example,interop}` and its normal Makefile
   hierarchy.  Add `ZdbusLib.hh/.cc`, `Zdbus.hh`, `ZdbusClient.hh`, and
   `ZdbusServer.hh`; divide private code into address/authentication, envelope
   framing, connection I/O, client, and service sources.  Keep installed
   headers in a directed acyclic include graph with idempotent guards, the
   standard header skeleton, and direct dependencies only.  Use CamelCase
   module-prefixed basenames in `src` and `test`, and lowercase basenames in
   `util`, `itest`, `example`, and `interop`.  Traverse/build in dependency
   order: `src`, `test`, independently buildable `util`, then `itest`,
   `example`, and `interop`; `util` must not depend on `itest`.
2. Add a Linux host conditional in `configure.ac` and use it in the top-level
   `Makefile.am` so `zdbus` appears in `SUBDIRS` only on Linux.  Include
   `zdbus` in `DIST_SUBDIRS` for source distribution, but never traverse it
   for a non-Linux build or top-level `make test`.  Gate every `zdbus/src`
   `libZdbus.la` target, the `zdbus/{util,example}` targets, and every
   `zdbus/{test,itest,interop}` test target and invocation on Linux; non-Linux
   builds must neither compile or link `libZdbus` nor build or run its tests,
   including when those directories are entered directly.  Guard installed
   public headers and source entries so non-Linux platforms do not expose an
   incomplete API or include `<sys/un.h>`.  Link `libZdbus.la` against `zi`,
   `zf`, `ze`, `zt`, `zm`, and `zu` plus established platform libraries.  Do
   not depend on `Ztls` or add any third-party runtime dependency.  Detect
   `libdbus-1` with `PKG_CHECK_MODULES` as an optional test-only dependency:
   build the reference peer in `util` and run `interop` only when present.
   The private-bus fixture and `itest` must remain independent of it;
   `libdbus-1` must never enter `libZdbus`'s link interface.
3. Register the `ZiEventLoop` socket with its send callback from the start.
   Include `EPOLLOUT` at registration when the send callback is present;
   the callback and interest remain fixed for the socket's lifetime.  The
   initial writable event completes a nonblocking connect.  When output is
   queued after an idle period, attempt nonblocking output immediately on the
   Rx shard; after `EAGAIN`, the next writable edge resumes it.  Do not add a
   socket-interest mutation API or delete/re-add the registration.

## Engineering and audit constraints

1. Implement in GNU C++2b using Z framework idioms.  Use CRTP, ADL tags,
   `ZuTypeList`/`ZuSwitch`/`ZuUnroll`, SFINAE detectors, harmless CRTP base
   defaults, access-changing `using` declarations, Z assertions, Z formatting,
   `ZmFn`, and Z containers.  Do not use concepts/`requires`, `enum class`,
   anonymous namespaces, nonspecific lambda captures, STL substitutes for Z
   facilities, hand-rolled hashes, `thread_local`, `FILE`/`syslog`, or printf
   formatting.  Use `switch` for one-value dispatch and flatten error paths.
2. Keep names concise and within the 28-byte limit; use the established
   `req`, `res`, `rx`, `tx`, `cli`, `srv`, and `cxn` abbreviations and trailing
   underscores for internal or shard-dedicated functions.  Use overloaded
   getters/setters and the standard `init`/`final`/`start`/`stop` lifecycle
   vocabulary.  Do not retain compatibility shims or dead predecessor APIs.
3. Follow class/struct and layout rules throughout: all-public struct data
   precedes functions and has no `m_`; class state is private, `m_`-prefixed,
   and in the final `private:` section.  Order storage to limit padding,
   separate shared and shard-owned data, begin independently accessed shard
   groups on cache-line boundaries, and introduce short local aliases where a
   member type would defeat tabular alignment.  Move template-independent
   declarations out of templates.
4. Use hard tabs and prevailing Z layout.  Use `int`/`unsigned` for native
   locals, fixed-width integers only for wire/storage widths, Z conversions
   instead of unnecessary casts, `ZuString` for compile-time strings, and
   `ZuSpan` only for run-time views.  Cache invariant lengths/indexed values in
   loops; count elements while traversing instead of snapshotting container
   size; do not default-initialize storage that is immediately overwritten.
5. Treat every red/amber flag in `GUIDELINES.md` as an implementation and
   review requirement across `src`, `test`, `util`, `itest`, `example`, and
   `interop`.
   Any unavoidable exception must be documented beside the declaration with
   its correctness, ownership, and performance rationale; do not silently
   accept a flagged pattern.
6. Use `ZuAssert` for compile-time invariants, `ZmAssert_` for fatal integrity
   failures, `ZmAssert` for debug-only assertions, and `ZiAssert` with a
   release fallback at this layer.  Represent protocol codes and small named
   constants with the established enum/`ZtEnum` patterns, use sentinel states
   instead of parallel validity booleans, use `ZuMatcher` for multiple auth or
   address tokens, and use framework span hashes rather than custom hashing.
7. Use `ZiFile` and `ZiStat` for filesystem operations, including file access,
   existence checks, and test-tool discovery.  Linux-only scope permits native
   AF_UNIX socket, pipe, and process-control calls, but does not justify raw
   filesystem syscalls.

## Connection lifecycle and addressing

1. Define `ZdbusAddress` for parsed D-Bus `unix:` addresses.  Support
   `path=` and Linux `abstract=` forms, D-Bus percent decoding, required-key
   validation, and `sockaddr_un` limits.  Provide explicit-address, session
   environment, and conventional system-bus constructors.  Store retained
   variable text in named-heap `ZtString` types and decode mutable address
   scratch in place; do not allocate an intermediate component tree.  Treat
   the fixed `sockaddr_un::sun_path` array as an external ABI boundary, validate
   its mandated capacity before copying, and do not mirror it in another fixed
   application array.
2. Define `ZdbusConnection` states with a compact `ZtEnum`: `Stopped`,
   `Connecting`, `Authenticating`, `Hello`, `Ready`, and `Stopping`.  The
   `ZiEventLoop`/socket registration, receive accumulator, decoded-frame
   routing, subscriptions, service routes, and inescapably shared connection
   state are Rx-owned.  Application send ingress, serial allocation, pending
   calls, message construction, completion, and the producer queue are
   Tx-owned.  Tx posts completed `ZiIOBuf` handles plus fixed metadata to Rx;
   Rx posts a retained return/error frame plus fixed correlation metadata to
   Tx.  Neither shard reads or mutates the other's state.  Public cross-shard
   entry points are thin `rxInvoke`/`txInvoke` dispatchers to shard-asserting
   trailing-underscore variants.  No lock makes non-owner access acceptable;
   diagnostic counters remain primitive and tolerate unclean reads unless a
   stable value is explicitly required.
3. Create `socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)`, set it
   nonblocking, call `connect`, and register it with `ZiEventLoop` with the
   send callback already present.  Resolve `SO_ERROR` on the write callback.
   Drain reads and
   writes through `recvmsg`/`recv` and `sendmsg`/`send`, correctly handling
   partial I/O, `EINTR`, EOF, and epoll errors.  Cap bytes/messages processed
   per callback with named, run-time-tunable budgets: stop at `EAGAIN` or the
   budget, and post/rearm a continuation for remaining work so a continuously
   ready socket cannot starve the scheduler.  Remove the loop FD before every
   terminal close.
4. Implement client SASL on the same buffers: initial NUL, `AUTH EXTERNAL`
   with the decimal effective-UID string encoded as hexadecimal octets, `OK`,
   then `BEGIN`.
   This uses the process credential and AF_UNIX peer authentication supplied
   by the kernel; it needs no TLS, cryptography, key store, certificates, or
   application credential callback.  Reject malformed ASCII/hex, unexpected
   NULs/commands, invalid state transitions, and unsupported FD negotiation.
   `ZdbusServer` uses this same client protocol because it connects to the bus.
5. Make `start()` and `stop()` idempotent.  Stop with the required asynchronous
   three-phase teardown: first prevent new ingress and post an Rx continuation;
   on Rx, deregister/close the socket, drain callbacks and the socket-output
   queue, and detach subscriptions/routes; finally post a Tx continuation to
   cancel every pending-call timer with scheduler `del()`, drain late timer and
   reply handoffs, complete pending work exactly once, drain the producer
   queue, release ownership, and invoke `stopFn`.  Do not block outside the
   main thread.
   Short-lived calls, timers, frames, queue nodes, and callbacks use raw
   back-pointers to the longer-lived connection; they never retain it through
   a reference cycle.  Destructors assert that owned activity was already
   drained rather than attempting teardown themselves.

## Envelope, serials, and errors

1. Parse and emit the first `PrefixSize` bytes directly: byte order, message type,
   flags, protocol version, body length, and serial.  The `a(yv)` header-fields
   array begins at `PrefixSize`; its initial `uint32_t` array length completes
   the `MinHeaderSize` header.  Define both names from D-Bus's mandated 12- and
   16-byte wire layout with maintenance comments; do not use either literal as
   a storage capacity.  Use internal single-field `ZfStruct` renderings and
   `ZfDBUS::save`/`handler` for each typed variant value (`o`, `s`, `g`, or
   `u`) while the envelope writes/reads the array and variant framing and
   backpatches the array length in the final buffer.  Use `ZfDBUS::Any` to
   retain unknown well-formed variant values.  This avoids assuming that the
   codec's generic `AsVariant` union can distinguish `s`, `o`, and `g`: those
   share `ZfFieldTC::String` and are selected by field properties on the
   internal renderings.  Define the standard-mandated body alignment as the
   named `BodyAlignment` constant, align to it, and marshal the body with its
   absolute message offset.  On receive, construct `ZfDBUS::View` instances
   over the retained frame with the advertised signature, absolute offset, and
   byte order.  The envelope owns complete frame-length arithmetic, mandatory
   headers, protocol version, configured message-size limits, and routing
   semantics.  Preserve unknown well-formed header fields for compatibility
   while ignoring them at dispatch, tolerate irrelevant ordering and
   extensible flags, and restrict rejection to malformed framing, unsafe
   resource claims, missing semantic requirements, or known fields with
   duplicate/wrong types.
2. Validate required headers: calls need path/member, returns/errors need reply
   serial, errors also need error name, and signals need path/interface/member.
   Use named limits and protocol codes from the
   [D-Bus definitions](https://dbus.freedesktop.org/doc/api/html/dbus-protocol_8h_source.html),
   allowing smaller configured resource limits.
3. `ZdbusClient` exclusively owns the nonzero message-serial allocator,
   pending-call map, and signal subscription registry.  It allocates/registers
   before enqueueing, avoids outstanding collisions on wraparound, and removes
   deterministically on reply, cancellation, timeout, disconnect, or stop.
   `ZdbusServer` obtains outbound serials only through its contained client
   core.  Neither applications, adapters, the connection I/O layer, nor
   `ZfDBUS` allocate serials.  Pending-call objects carry their intrusive hash,
   timeout, and queue nodes; do not store `ZmRef<Call>` inside a Z container or
   allocate a separate container node; stack/shadow intrusions when one call
   participates in multiple containers.  The allocator and pending-call map
   are Tx-owned, while subscriptions are Rx-owned.  Rx validates reply framing
   and posts the retained frame to Tx; Tx performs lookup, typed decoding,
   removal, and completion.  Each fixed-size concrete call type uses its own
   aliased `ZmHeap` specialization; variable-sized storage uses a named
   `ZmVHeap`/`ZmHeapID`.  Each call timeout is a distinct
   `ZmScheduler::Timer` contained by value in its owning call and cancelled
   with scheduler `del()` before that call can be destroyed; late callbacks
   are drained on the Tx shard.
4. Every outbound message, including method returns, errors, and signals,
   receives a fresh serial from `ZdbusClient`.  A return/error also sets
   `REPLY_SERIAL` to the inbound call's serial.  The reply serial correlates a
   message; it is not the reply's own serial.
5. A call completion is a typed alternate result: one declared return body or
   one declared named error body, both decoded by `ZfDBUS` from their own
   schemas.  Local timeout, cancellation, disconnect, authentication, queue,
   and malformed-wire failures are separate local results.  A dynamic remote
   error retains its name and validated body rather than becoming only a text
   diagnostic.
6. A matching return/error is decoded through its pending call's typed parser.
   Discard an unmatched or late reply after cancellation/timeout without a
   second completion.  A protocol violation closes the peer before application
   dispatch.  Limit iterator scope to lookup/removal, release container state
   before callbacks or shard posts, and delete each terminal call/subscription
   from every owning container immediately--never by periodic scan.

## Application headers, bodies, and adapters

1. Define shared `Builder`/`Parser` defaults and CRTP
   `ReqBuilder<Impl, Object, Headers>`, `ReqParser<Impl, Object, Headers>`,
   `ResBuilder<Impl, Object, Headers>`, `ResParser<Impl, Object, Headers>`,
   and corresponding error and signal adapters in `Zdbus_`.  `Headers` is an
   application-facing `zdbus` template parameter, analogous to the Zhttp/Zrest
   header catalogs; it is not a `ZfDBUS` type.  Factor common message-kind
   behavior.  Clients build calls and parse returns/errors/signals; servers
   parse calls and build returns/errors/signals.
2. Provide the `zdbus` header-key catalog with D-Bus field IDs and protocol
   value types.  Follow Zhttp's fixed and runtime
   `header<Key>(emit)`/`header(emit)` provider pattern with typed values.
   Applications can supply fixed or runtime destination, path, interface,
   member, and error name values from the same request object used for the
   body.  Incoming parsers receive typed known headers plus a runtime callback
   for unknown fields.  Reject duplicate/conflicting protocol headers.  The
   envelope, not the application catalog, controls message type, serial,
   reply serial, sender, computed body signature, lengths, and byte order.
3. Each adapter has `bodyObject(Object *)`, defaulting to the full object, and
   a `Body_DBUS_Facet` defaulting to `ZuFacet::DBUS`; header callbacks may use
   fields from that same object.  For output, obtain the body signature with
   `ZfDBUS::signature<Body, Body_DBUS_Facet>()` and call
   `ZfDBUS::measure<Body_DBUS_Facet>` with the body-start `Context::offset`
   and message byte order, reject its codec error or an over-limit length,
   reserve the entire final frame in a checked `ZiIOBuf::ensure()` call, then
   call `ZfDBUS::save<Body_DBUS_Facet>` into a sink adapter over that buffer.
   `save()` returns the sink, as with the other `ZfStruct` adapters; after it
   returns, verify the final buffer length matches the measured end offset
   before enqueueing.  The sink must not trigger another allocation or flush
   part of a frame; there is no temporary contiguous body copy.  Keep the
   source object stable across measurement and save.  For input, call
   `ZfDBUS::handler<Body, Body_DBUS_Facet>(View)` and only `ctor`, `alloc`,
   `new_`, or `load` after the handler succeeds.  Keep the retained frame alive
   for the entire callback whenever the selected schema contains borrowed
   strings, spans, or `ZfDBUS::Any`.  Use these adapters for `Hello`, name
   management, and match rules too.
4. Define request catalogs with `Responses` and `Errors`, and signal catalogs.
   Borrow Zrest typelist/union dispatch: pending state retains the request's
   response schema; returns dispatch by request/signature, errors by
   name/schema, and signals by headers/schema.  Server catalogs match
   path/interface/member before decoding a call body.  Statically reject
   ambiguous registrations and offer explicit dynamic route/schema callbacks.
5. Header processing precedes typed body processing and each message receives
   one completion.  A body signature mismatch is a typed-dispatch failure;
   malformed body bytes report the `ZfDBUS::Result` error and absolute offset.
   Keep parser resets, object construction, cancellation, callback lifetimes,
   shard posts, and retained `ZmRef` frame ownership explicit.  Use specific
   captures, never `[&]`/`[=]`; consolidate type-erased callback capture into
   one 64-bit handle where practical.  `ZiLOG` lambdas capture immutable
   values by copy and use `ZeString` for diagnostic scratch--never pointers or
   references whose lifetime ends before the logger thread runs.

Proposed application shape:

```cpp
struct Args {
  using Name = ZtString<ZtStringHeapID<"Zdbus.Invoke.Name">>;

  Name name;
  uint32_t count;
};
ZfStruct(, Args,
  (((name), (Mutable)), (String)),
  (((count), (Mutable)), (UInt32)));
ZfStructRender(, Args, DBUS, name, count);

struct Invoke : ZdbusReqBuilder<Invoke, Call, CallHeaders> {
  using Body_DBUS_Facet = ZuFacet::DBUS;
  using Responses = ZuTypeList<InvokeResult>;
  using Errors = ZuTypeList<InvokeError>;

  const Args &bodyObject(const Call *call) const { return call->args; }
};
```

The body signature is `su`, generated by `ZfDBUS`.  `Call` supplies routing
headers and `Args`; matching service parsers and return/error builders reuse
the same schemas.

## Resource management and tests

1. Bound input frames and transmit messages/bytes.  On overflow, fail the
   producer rather than grow memory indefinitely.  Use pooled `ZiIOBuf` storage
   for the receive accumulator, complete retained frames, and queued output;
   receive directly into uninitialized buffer tail space.  Serialize the fixed
   prefix, `a(yv)` header fields, alignment, and body into the final outbound
   buffer; do not introduce a JSON/CBOR round trip or a temporary contiguous
   body.  Do not lock shard-owned state or scan containers for delayed cleanup.
2. Put every fixed-size allocated concrete object on its own `ZmHeap` using the
   established `Type_<Heap = ZuVoid>`/heap-alias/`ZuDerive` pattern; never hide
   allocation in a generic base or custom `operator new`.  Give every
   variable-size allocation a named `ZmVHeap`/`ZmHeapID`.  Consolidate
   refcount and intrusive hash/list/queue nodes in the application object and
   use `ZuObject`/`ZuRef` for shard-affined ownership or `ZmObject`/`ZmRef` only
   for genuinely shared ownership.  Short-lived owned nodes retain raw owner
   pointers.  No fixed-size object is allocated through `ZmVHeap`.
3. Use named, documented limits and tunable parameters for frame size, queued
   bytes/messages, pending calls, subscriptions, dispatch batch size, and I/O
   work per turn.  Do not use fixed C arrays or fixed lookup tables.  Choose
   `ZtScratch` for bounded hot scratch with heap fallback, `ZtBuiltin` only from
   measured 95--99% hot-path sizing, `ZtArray` for cold variable arrays, and
   `ZmHash`/`ZmList`/`ZmQueue` with explicit heap IDs for long-lived state.
   Reserve final wire storage once and fill uninitialized space directly.
4. Keep all loops bounded and cache invariant lengths/values.  Use direct
   keyed lookup for calls, routes, and subscriptions instead of scans; batch
   fan-out and post a continuation when a signal has more recipients than one
   scheduler turn permits.  Finish and destroy iterator scopes before cleanup,
   callbacks, container mutation outside the traversal, or cross-shard posts.
5. On Linux only, unit-test addresses, SASL transcripts, framing,
   mandatory/unknown headers, serial allocation/correlation/wraparound,
   no-reply, timeouts, queue limits, initial writable registration,
   static/runtime headers, subobject bodies, typed returns/errors/signals,
   generic remote errors, and retained callback
   input.  Verify each message kind gets a fresh serial independently of
   reply correlation.  Implement repository-owned unit tests with
   `ZuTestUtil`/`ZuTest` and valid TAP.  Synchronize concurrent tests with
   posted continuations and `ZmBlock`, or `ZmSemaphore` only where `ZmBlock`
   does not fit; never poll, sleep, or use elapsed time to infer completion.
6. Add a focused `ZiEventLoop` socketpair test for partial I/O, `EAGAIN`,
   budget exhaustion/continuation, disconnect, initial writable readiness, and
   three-phase stop ordering.
7. On Linux only, add an integration test under `zdbus/itest` that launches a
   private `dbus-daemon --session --nofork --print-address`, starts a
   `ZdbusServer` and `ZdbusClient`, and verifies `Hello`, name acquisition,
   a metadata-defined return, structured named error, `AddMatch` signal, and
   teardown.  Skip only when `dbus-daemon` is unavailable; failed startup is
   a test failure.
8. On Linux only, put reusable, independently buildable test tools in
   `zdbus/util`: a private-bus launcher/fixture that reports its address and
   readiness through a pipe, and a `libdbus-1` reference peer able to act as
   caller, service, and signal subscriber.  The latter is a test-only tool,
   not part of `libZdbus` or its public API.  Reuse the fixture from `itest`
   and `interop`, and use the reference peer only from `interop`; keep
   external-process tests out of `test`.  Register each
   repository-owned test driver with its directory's `make test` target and
   emit valid TAP through `ZuTestUtil`/`ZuTest`.
9. In `zdbus/interop`, exercise both directions against the reference peer:
   `ZdbusClient` calls a libdbus service and consumes returns, named errors,
   and signals; a libdbus caller invokes `ZdbusServer` and consumes its
   returns, named errors, and signals.  Compare routing headers, signatures,
   serial/reply-serial correlation, and decoded values, including nested
   structures, arrays, maps/`a{sv}`, and variants.  Cover mismatched reply
   signatures and unknown named errors without assuming a fixed serial or
   event order.
   `dbus-send` and `dbus-monitor` may be diagnostic probes, but are not the
   structured-value oracle.  Use pipe/event-loop readiness and explicit
   completion rather than sleeps or polling; bound and reap every child and
   clean up the private bus.  Skip only when `dbus-daemon` or `libdbus-1` is
   unavailable; a started peer's failure is a test failure.
   Document an example for all four message kinds.
10. Retain the current build configuration.  Other compilers, release/debug
   flags, and platforms are out of scope for this implementation.
11. Rebuild infrequently: batch source changes into large protocol milestones
   and do not rebuild or retest between the edits within a milestone.  At a
   major milestone, rebuild incrementally using the current build artifacts;
   do not clean or reconfigure.  Rebuild the changed `src` target with `-j8`
   before its dependent directory's default target, then run `make test`; the
   build must be warning-free.
12. Defer dependent rebuilding until the end of the implementation.  At
   completion build top-level `make -j8`, then build each test directory's
   default target before its test target.
13. Use Valgrind only to diagnose suspected memory corruption when needed and
   as the final memory-leak and corruption gate.  Do not include it in routine
   milestone testing.  Invoke source-tree programs under debugging and
   checking tools through `libtool exec`, never directly from `.libs`.
14. Before completion, audit declarations and uses in every repository-owned
   C++ directory present in the in-scope `zdbus` and `zi` modules--including
   `src`, `test`, `util`, `itest`, `example`, `bench`, and `interop` wherever they
   exist--against every `GUIDELINES.md` red/amber flag.  Inspect both relevant
   declarations and uses.  Remove dead code and compatibility remnants, verify
   immediate deterministic removal from long-lived containers, and document
   any explicitly accepted exception.  The current-configuration restriction
   above is the explicit scope exception to the guideline's
   multi-configuration acceptance matrix; the focused tests and final
   Valgrind gate remain mandatory.

## Implementation order

1. Treat the implemented and focused-tested `ZfDBUS` from `zdbus1.md` as the
   codec prerequisite; do not duplicate its signature, validation, or scalar
   mapping logic in `zdbus`.
2. Ensure/test `ZiEventLoop` initial writable registration with a send callback.
3. Add Linux connection/authentication/envelope handling and `ZdbusClient`
   serial/pending-call lifecycle.
4. Add adapters, catalogs, server dispatch, signals, and bus-management calls.
5. Add the independent `util` tools, examples, private-bus integration,
   interoperability tests, and complete build verification.
