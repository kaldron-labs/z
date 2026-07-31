# IPv6 Plan for ZiIP and Dependents

## Context

`ZiIP` is currently an IPv4-only wrapper over `struct in_addr`.  That leaks
through the public API and storage model:

- `ZiIP` inherits `in_addr`, stores only `s_addr`, prints dotted IPv4, resolves
  with `AF_INET`, and reverse-resolves through `sockaddr_in`.
- `ZiSockAddr` stores only `sockaddr_in`; `sa()` and `len()` always describe
  `AF_INET`.
- `ZiMultiplex` creates `AF_INET` sockets, sizes Windows `AcceptEx` buffers for
  `sockaddr_in`, and uses IPv4 multicast APIs (`ip_mreq`, `IPPROTO_IP`,
  `IP_ADD_MEMBERSHIP`, `IP_MULTICAST_*`).
- Resolver HTTPS/SVCB code parses only `ipv4hint`; there is no `ipv6hint`.
- Telemetry, flatbuffers transforms, zdb/zcmd schemas, examples, and higher
  modules use `ZiIP` as if it were always four bytes.

The implementation should follow `GUIDELINES.md`: preserve low-latency value
semantics, avoid heap allocation and STL, use explicit sentinel values, prefer
compile-time dispatch/switches over virtual abstraction, and propagate breaking
API changes through dependent code instead of adding compatibility shims.

Verification scope follows the current top-level `Makefile.am`.  Update source
where APIs or schemas require it, including work-in-progress modules that are not
in active top-level `SUBDIRS`, but do not make tests or gates depend on those
modules.  Examples include `zcmd`, `zrest`, `zws`, `zproxy`, and similar WIP
trees: update their source/schema references when required, but do not attempt to
build or test them.  Windows-specific code paths should be kept correct at the
source level, but Windows-only libraries and IOCP/Winsock test runs are excluded
from this plan while they are outside the active top-level build.

Except for adding the `ZiIPType` discrimination needed to identify IPv6, keep
the existing IPv4 behavior and representation style as-is.  The IPv6 path
should align with the IPv4 precedent: use the underlying OS socket/address types
directly wherever possible and do not add redundant byte swapping or byte-order
normalization.

## Target API Shape

1. Make `ZiIP` a compact POD value that carries address family plus address
   bytes.
   - Use `ZtEnumNS(ZiIPType, int8_t, V4, V6)` as the address-family
     discriminant. Keep null/unspecified state outside the enum value set, for
     example by pairing the enum with the existing default/null sentinel logic.
   - Store address data inline with `ZuUnion<in_addr, in6_addr>` plus family.
     Do not use an ad hoc byte array or raw C union for the primary
     representation.
   - Keep default construction as the null/unspecified sentinel.
   - Delete `ZiIP{uint32_t}`, `operator =(uint32_t)`, and
     `operator uint32_t()`. A repository scan found direct integer construction
     only in tests, and integer conversion is little-used relative to the risk of
     IPv6 truncation. Use string parsing or native `in_addr` construction for
     IPv4 literals instead.
   - Provide cheap predicates/accessors: `type()`, `v4()`, `v6()`, `unspec()`,
     `wildcard()`, `loopback()`, `multicast()`, and `mappedV4()` if IPv4-mapped
     handling is needed.
   - Add accessors returning native socket structures without copying through
     temporary heap state: `inAddr()` and `in6Addr()`.

2. Update `ZiIP` parsing, formatting, comparison, and hashing.
   - Parse numeric IPv4/IPv6 with a small platform wrapper first, then fall back
     to resolver. Do not call POSIX `inet_pton` directly from common code:
     - Unix uses `::inet_pton` from `<arpa/inet.h>`;
     - Windows uses `InetPtonA`/`InetPtonW` from `<ws2tcpip.h>` when available,
       with `WSAStringToAddress` as the documented fallback for MinGW/SDK gaps.
   - `ZiIP::print` switches on `type()`.
   - The IPv4 branch intentionally keeps today's implementation: pun
     `in_addr::s_addr` to bytes and emit the bytes in sequence with
     `ZuBoxed(...) << '.' ...`. Do not replace it with `inet_ntop` or redundant
     byte swapping.
   - The IPv6 branch should be similarly efficient and leverage Z printing
     capabilities: operate directly on `in6_addr` bytes/16-bit groups, emit to
     the supplied output stream, and avoid heap allocation or C library string
     formatting unless a measured platform reason requires it.
   - Compare first by type, then compare the active OS address object in the
     same spirit as today's `s_addr` comparison. Do not byte-swap IPv4 or IPv6
     just to compare.
   - Hash type plus the active OS address object. Do not hash inactive union
     storage or padding, and do not normalize through redundant byte swaps.
   - Keep `ZuTraits<ZiIP>::IsPOD = 1` only if the new layout remains trivially
     copyable and directly value-safe.

3. Replace `ZiSockAddr` with a family-aware value wrapper.
   - Store native socket addresses with `ZuUnion<sockaddr_in, sockaddr_in6>`.
     Do not use `sockaddr_storage` for the primary representation; it obscures
     the active type and encourages padding-sensitive comparisons or hashing.
   - `init(ZiIP, uint16_t)` chooses `AF_INET`, `AF_INET6`, or `AF_UNSPEC`.
   - `ip()` and `port()` switch on `ss_family`.
   - `len()` returns `sizeof(sockaddr_in)` or `sizeof(sockaddr_in6)`.
   - Add `type()` and typed helpers used by socket setup; keep `sa()` as the
     generic boundary for syscalls.
   - Add tests that exercise IPv4, IPv6, null, and port round trips.

## Implementation Phases

### Phase 1: Core Address Type

- Rewrite `zi/src/ZiIP.hh` so `ZiIP` no longer inherits `in_addr`.
- Define the `ZiIP` address payload as a `ZuUnion<in_addr, in6_addr>` and keep
  the active member selected solely by `ZiIPType`.
- Keep IPv4 `in_addr` construction/assignment, `print()`, `cmp()`, `hash()`,
  and `multicast()` behavior equivalent to the existing `in_addr::s_addr`
  implementation except for the new type check/discriminant.
- Delete integer IPv4 interop (`ZiIP{uint32_t}`, `operator =(uint32_t)`, and
  `operator uint32_t()`) and update tests/callers to use string literals or
  native `in_addr` values.
- Implement the IPv6 `print()` branch beside the IPv4 branch, not through a
  separate allocating conversion; use `ZuBox`, `ZuFmt`, and stream emission to
  format canonical IPv6 text efficiently.
- Define `ZiSockAddr` as a `ZuUnion<sockaddr_in, sockaddr_in6>` plus helpers
  that switch on the active family; avoid generic `sockaddr_storage` except at
  syscall boundaries if a platform API forces it.
- Move non-trivial parsing/printing helpers into `zi/src/ZiIP.cc` if the header
  would otherwise grow bulky or pull in extra platform details.
- Put numeric address parsing behind local helpers in `ZiIP.cc`, for example
  `ZiIP_pton4`/`ZiIP_pton6`, so Windows `InetPton*`/`WSAStringToAddress` and
  Unix `inet_pton` differences do not leak into the public header.
- Keep constructor and assignment coverage for:
  - default/null
  - `struct in_addr`
  - `struct in6_addr`
  - string-like input
- Add native accessors and type predicates before updating dependents.
- Extend `zi/test/ZiIPTest.cc` for:
  - `127.0.0.1` parse/print
  - `::1` parse/print/name, where platform resolver permits it
  - multicast boundaries for IPv4 and IPv6 (`ff00::/8`)
  - compare/hash distinctions between null, `0.0.0.0`, and `::`
  - `ZiSockAddr` IPv4/IPv6 length and round trip

Gate: `make -C zi/src -j8 && make -C zi/test -j8 && ./zi/test/ZiIPTest`.

### Phase 2: Resolver

- Change `ZiResolver::resolve` callback type to `ZmFn<bool(ZiIP)>` that may
  emit either family.
- Use `AF_UNSPEC` hints and accept `sockaddr_in` and `sockaddr_in6` results.
- Consider address ordering deliberately:
  - preserve OS `getaddrinfo` order by default;
  - add policy only if callers require IPv4-first/IPv6-first behavior.
- Change `ZiResolver::name` to build a `ZiSockAddr` or family-specific native
  sockaddr from `ZiIP`.
- Add SVCB/HTTPS `ipv6hint` support:
  - add `SvcIPv6Hint = 6`;
  - add `ipv6Hint`, `nIPv6Hint`, and `hasIPv6Hint` fields to
    `ZiResolver::HTTPS`;
  - emit both hint kinds in `http3`, with duplicate suppression across families.
- Replace fixed `ZiIP seen[H3MaxIPs]` behavior only if `H3MaxIPs` is no longer
  sufficient for combined hints; use a named constant if raised.
- Update `zi/test/ZiResolverTest.cc` with synthetic DNS records for `ipv6hint`
  so tests do not depend on public DNS.

Gate: `make -C zi/src -j8 && make -C zi/test -j8 && ./zi/test/ZiResolverTest`.

### Phase 3: ZiMultiplex TCP/UDP Socket Paths

- Create sockets with `remoteIP.type()` or `localIP.type()` instead of
  hard-coded `AF_INET`.
  - If both addresses are unspecified, default to IPv4 initially unless an
    explicit type option is added.
  - If local and remote types conflict, fail early with `ZiEINVAL`.
- Add a type field to `ZiListenInfo`, `ZiCxnInfo`, or derive type from the
  populated `ZiIP`; be explicit for wildcard binds because `ZiIP{}` cannot
  distinguish `0.0.0.0` from `::`.
- For listeners, decide whether wildcard IPv6 listeners should be IPv6-only:
  - recommended default: set `IPV6_V6ONLY = 1` and require separate IPv4 and
    IPv6 listeners; this keeps telemetry and listener lookup unambiguous.
  - document and test if dual-stack sockets are intentionally allowed.
- Replace all hard-coded `sockaddr_in` lengths with `ZiSockAddr::len()` or a
  named `ZiSockAddr::MaxLen`.
- For Windows `AcceptEx`, size local/remote buffers from `sizeof(sockaddr_in6)`
  plus the required padding, and parse returned addresses by family.
- On Windows, use Winsock spelling/types where they differ from POSIX:
  `SOCKET`, `sockaddr_in6`, `IN6_ADDR`, `IPV6_MREQ`/`ipv6_mreq` availability,
  `DWORD`/`BOOL` option scalars, and `WSAGetLastError`/`ZeLastSockError`.
- Update `getsockname`, `accept`, `recvfrom`, `sendto`, `ConnectEx`, and
  `GetAcceptExSockaddrs` call sites to treat `ZiSockAddr` as variable length.
- Keep Rx/Tx sharding intact: public methods stay thin dispatchers and capture
  only value address/port/options data into Rx-owned work.

Gate:

- Linux epoll: `make -C zi/src -j8 && make -C zi/test -j8 &&
  ./zi/test/ZiMxLoopTest`
- Windows IOCP source paths are updated for `sockaddr_in6` sizing and family
  handling, but the mingw/msys2 build and loop test are excluded from this gate.

### Phase 4: Multicast Options

- Replace `struct ZiMReq : ip_mreq` with a type-tagged value that stores the
  native OS request directly:
  - `ZiIPType::T m_type`
  - `ZuUnion<ip_mreq, ipv6_mreq> m_req`
  - default construction should preserve today's all-zero/null behavior for the
    IPv4 member unless a stronger null sentinel is already established nearby.
- Keep the IPv4 request path as close to current code as possible:
  - `ZiMReq(const ZiIP &addr, const ZiIP &mif)` remains the IPv4 constructor;
  - it initializes `m_type = ZiIPType::V4`, `ip_mreq::imr_multiaddr` from
    `addr.inAddr()`, and `ip_mreq::imr_interface` from `mif.inAddr()`;
  - `addr()` and `mif()` for IPv4 return `ZiIP` values built directly from the
    underlying `in_addr` fields;
  - `ip_mreq` is passed directly to `setsockopt`; do not copy into scratch
    structs or byte-swap.
- Add the IPv6 request path as the native parallel of the IPv4 path:
  - add `ZiMReq(const ZiIP &addr, unsigned ifIndex)` for IPv6 membership;
  - it initializes `m_type = ZiIPType::V6`, `ipv6_mreq::ipv6mr_multiaddr` from
    `addr.in6Addr()`, and `ipv6_mreq::ipv6mr_interface` from `ifIndex`;
  - expose `ifIndex()` for IPv6 instead of pretending the interface is a
    `ZiIP`;
  - pass the active `ipv6_mreq` member directly to `setsockopt`.
- Account for Windows Winsock naming if `IPV6_MREQ` is exposed as a typedef
  rather than `struct ipv6_mreq`; hide that in a module-local alias instead of
  spreading preprocessor branches through call sites.
- Do not support mixed-type multicast request data:
  - reject `ZiMReq(ZiIPType::V4 group, IPv6 interface)` and the converse at
    construction/config parse time with `ZiEINVAL`;
  - reject a V4 request on a V6 socket and a V6 request on a V4 socket before
    any `setsockopt` calls;
  - keep `addr().multicast()` validation type-aware.
- Keep `ZiCxnOptions_NMReq` and `using MReqs = ZuArray<ZiMReq,
  ZiCxnOptions_NMReq>`; multicast request storage remains inline and bounded.
- Split multicast interface option state along the same native line:
  - preserve `m_mif`/`mif(ZiIP)` as the IPv4 multicast interface API and
    implement it with `IP_MULTICAST_IF`;
  - add `m_mifIndex` plus `mifIndex(unsigned)` for IPv6 and implement it with
    `IPV6_MULTICAST_IF`;
  - do not resolve interface names in `ZiMultiplex::udp_`; if names are
    accepted by config/example code, resolve them before constructing
    `ZiCxnOptions`.
- Socket setup should be a `switch` on the socket address type:
  - for `ZiIPType::V4`, keep the existing `IPPROTO_IP`,
    `IP_ADD_MEMBERSHIP`, `IP_MULTICAST_IF`, `IP_MULTICAST_TTL`, and
    `IP_MULTICAST_LOOP` branches, argument types, error labels, and close/fail
    behavior;
  - for `ZiIPType::V6`, add the analogous `IPPROTO_IPV6`,
    `IPV6_JOIN_GROUP` (or `IPV6_ADD_MEMBERSHIP` where that is the platform
    spelling), `IPV6_MULTICAST_IF`, `IPV6_MULTICAST_HOPS`, and
    `IPV6_MULTICAST_LOOP` branches;
  - use `int` option values on Unix and the existing Windows scalar style
    (`BOOL`/`DWORD` where the current V4 code already does so);
  - do not add generic helper layers unless they remove real duplicated
    platform code without obscuring the native option names.
- `ZiCxnOptions::equals`, `cmp`, `hash`, and `print` must include:
  - `ZiMReq::type()`;
  - active native request fields only;
  - IPv4 `m_mif`;
  - IPv6 `m_mifIndex`;
  - TTL/hops and loopback exactly once.
- Telemetry should not overload IPv6 interface index into `ZiIP` fields:
  - keep existing `mreqAddr`, `mreqIf`, and `mif` semantics for IPv4;
  - add type/index fields for IPv6 multicast, or schedule the telemetry schema
    change in Phase 5 if it requires flatbuffer regeneration;
  - never report an IPv6 interface index as `0.0.0.0`.
- Update config/example parsing prescriptively:
  - existing `addr->mif` IPv4 syntax continues to construct
    `ZiMReq(addr, mif)`;
  - IPv6 syntax must provide an interface index, for example through bracketed
    address parsing plus a numeric index option;
  - if interface names are accepted later, convert them to indexes during
    config parsing, not in the I/O path.

Gate:

- Add unit coverage for `ZiMReq` construction, `type()`, `addr()`, `mif()`,
  `ifIndex()`, `equals`, `cmp`, `hash`, and `print` for V4 and V6.
- Add `ZiCxnOptions` multicast tests covering V4-only, V6-only, and rejected
  mixed-type requests.
- Add platform smoke tests for IPv6 UDP multicast loopback where available; keep
  existing IPv4 multicast behavior unchanged.

### Phase 5: Persistence, Telemetry, and Wire Schemas

- Audit all `Zfb.IP` uses before changing schema. Current schema is
  `addr:[uint8:4]`, so widening it is a wire/storage break.
- FlatBuffers currently supports structs in unions for C++ code generation; this
  was verified against the local `flatc 25.12.19` with a scratch schema using a
  union of `IPv4`/`IPv6` structs.
- Replace the current `struct IP { addr:[uint8:4]; }` in
  `zfb/src/fbs/zfb_types.fbs` with the explicit struct union:
  ```fbs
  struct IPv4 { // ZiIP V4
    addr:[uint8:4];
  }
  struct IPv6 { // ZiIP V6
    addr:[uint8:16];
  }
  union IP {
    IPv4,
    IPv6
  }
  ```
  The short form above is permitted and was verified with the repository's
  local `flatc 25.12.19`; avoid the redundant `IPv4:IPv4` alias form.
- Update `ZfbTransform::IP` to save/load the new union:
  - `ZiIP::type() == ZiIPType::V4` saves the active `in_addr` bytes into
    `Zfb::IPv4`;
  - `ZiIP::type() == ZiIPType::V6` saves the active `in6_addr` bytes into
    `Zfb::IPv6`;
  - loading switches on the generated `Zfb::IP` type enum and constructs
    `ZiIP` directly from `in_addr` or `in6_addr`;
  - null/default `ZiIP` must be represented deliberately by the containing
    table/field semantics, not by an all-zero IPv4 union arm unless that is the
    intended value.
- Propagate schema changes through:
  - `zfb/src/fbs/zfb_types.fbs`
  - `zfb/src/ZfbStruct.hh`
  - `zdb/src/ZdbMemStore.hh`
  - `zdb_pq/src/ZdbPQ.hh`
  - `zcmd/src/fbs/ztel_telemetry.fbs`
  - `zdb/src/fbs/zdb_telemetry.fbs`
- PostgreSQL `inet` representation in `zdb_pq` is prescriptive:
  - keep the packed binary send/receive shape as `IPHdr` followed by address
    bytes, matching PostgreSQL's `inet_recv`/`inet_send` format;
  - redefine `ZdbPQ::IP` so it does not embed a whole `ZiIP` value after the
    header, because `ZiIP` becomes larger than the active wire payload;
  - use a packed value with `IPHdr hdr` plus inline storage sized for the active
    address, e.g. `ZuUnion<in_addr, in6_addr>` or a `ZuBArray<16>` payload with
    explicit length; do not send inactive union bytes or padding;
  - for IPv4 send: `hdr.family = PGSQL_AF_INET` (current value `2`), `hdr.bits =
    32`, `hdr.is_cidr = 0`, `hdr.len = 4`, followed by the active `in_addr`
    bytes exactly as PostgreSQL expects them;
  - for IPv6 send: `hdr.family = PGSQL_AF_INET6` (PostgreSQL wire value `3`),
    `hdr.bits = 128`, `hdr.is_cidr = 0`, `hdr.len = 16`, followed by the active
    `in6_addr` bytes;
  - on receive, validate `is_cidr == 0`, `len == 4` for family `2`, and
    `len == 16` for family `3`; reject unexpected family/length combinations
    with `ZiAssert`/`ZeError` rather than guessing;
  - convert directly between the active payload and `ZiIP` native accessors;
    do not byte-swap or normalize through textual `inet` conversion.
- Update telemetry primary keys and display fields to carry type. Avoid
  treating null, IPv4 wildcard, and IPv6 wildcard as the same value.

Gate: rebuild generated flatbuffer outputs as required by the active repo build,
compile `zfb`, `zdb`, and `zdb_pq`, then run affected tests.  Update `zcmd`
schema/source references if required by the API change, but do not attempt to
build or test `zcmd` while it remains a work-in-progress module outside active
top-level `SUBDIRS`.

### Phase 6: Dependent Modules and Examples

- Update address parsing utilities that currently split on `:` as host/port.
  IPv6 literals require bracket handling (`[::1]:443`) or a structured parser.
  Known touch points include `zproxy`, `zi/example/ZiMxUDP*`, and other command
  line examples.
- Update modules that expose `localIP()` or keep `ZiIP` in app config:
  `zrest`, `ztcp`, `ztls`, `zhttp`, `zquic`, `zproxy`, and `zdb`.
- For `zquic`, align with its existing `IPFamily::{IPv4, IPv6}` socket option
  logic and read `zquic/GUIDELINES.md` before editing that module.
- Replace tests that manually create `sockaddr_in` only when they are testing
  framework networking behavior; leave isolated IPv4-specific tests explicit.
- Add IPv6 loopback variants for representative TCP, UDP, TLS, HTTP, and QUIC
  loop tests once `ZiMultiplex` supports the family.

Gate: top-level `make -j8`, then targeted loop tests for the touched modules.
Work-in-progress modules not in active top-level `SUBDIRS` are source-update
scope only; their builds and tests are not part of this gate unless they are
re-enabled and expected to build.

## Compatibility and Breakage

- This should be a breaking change. Do not add a parallel legacy `ZiIPv4` shim
  unless a caller has a measured migration need.
- Do not keep integer IPv4 conveniences such as `ZiIP{0x7f000001U}`. They are
  primarily test-only today and become ambiguous once `ZiIP` is family-aware.
  Use string literals, native `in_addr`, or named test helpers instead.
- Any binary persistence or telemetry consumer reading `Zfb.IP` must be updated
  with the schema change in the same work branch.
- No `ZiIP` API may silently map IPv6 to zero or truncate it to IPv4.

## Review Checklist

- No hidden heap allocation in address parse/print, socket setup, telemetry
  capture, or resolver callbacks.
- No fixed IPv4-only `sizeof(sockaddr_in)` remains in generic paths.
- No direct `AF_INET`, `IPPROTO_IP`, `sockaddr_in`, or `ip_mreq` remains except
  in family-specific branches.
- Null address, IPv4 wildcard, and IPv6 wildcard are distinct where family
  matters.
- Hash/equality/ordering include type and significant address data only.
- Public `ZiMultiplex` entry points remain thin Rx dispatchers; address work is
  value-captured and destination-owned.
- Tests cover active Linux epoll paths for `ZiSockAddr` sizing, accept/connect,
  UDP `recvfrom`/`sendto`, and resolver round trips.  Windows IOCP source paths
  are reviewed/updated but Windows-only test execution is excluded from the gate.

## Required Work Slices

These slices are prescriptive and must land in order. The detailed phases above
describe implementation content; this section is the controlling delivery
sequence. Each slice must leave the tree buildable, keep existing IPv4 behavior
green, and unlock the next slice without compatibility shims.

1. Core address values
   - Implement `ZiIP` and `ZiSockAddr` as type-aware POD-style values.
   - Use `ZtEnumNS(ZiIPType, int8_t, V4, V6)` and native OS address structs in
     `ZuUnion`; do not use `enum class`, STL containers, `sockaddr_storage` as
     the primary representation, or ad hoc byte arrays.
   - Keep IPv4 native-`in_addr` construction, printing, comparison, hashing, and
     multicast behavior equivalent except for explicit family checks.
   - Delete integer IPv4 construction/conversion and replace existing test
     literals such as `ZiIP{0x7f000001U}` with string or native `in_addr`
     helpers.
   - Implement IPv6 parse/print/hash/compare without heap allocation on the
     normal path; use Z printing and span/array types, not `std::string`.
   - Implement numeric parsing through module-local platform helpers, not direct
     common-code `inet_pton` calls.
   - Acceptance:
     - `ZiIP` distinguishes null, IPv4 wildcard, and IPv6 wildcard where family
       matters.
     - `ZiSockAddr::len()`, `sa()`, `ip()`, and `port()` round-trip IPv4 and
       IPv6.
     - Numeric IPv4/IPv6 parsing works through Unix `inet_pton` and Windows
       `InetPton*`/`WSAStringToAddress` paths.
     - `ZiIP(S &&)` and string assignment have comments warning that fallback
       DNS resolution blocks.
     - Gate: `make -C zi/src -j8 && make -C zi/test -j8 &&
       ./zi/test/ZiIPTest`.
   - Unlocks: all later slices can pass family-aware address values without
     temporary casts or duplicated socket-address code.

2. Current resolver IPv6 surface
   - Update the existing blocking resolver only enough to make IPv6 behavior
     correct before the c-ares replacement in `resolver.md`.
   - Support `AF_UNSPEC`, IPv4/IPv6 reverse lookup, and HTTPS/SVCB `ipv6hint`
     parsing/emission in the current API shape.
   - Treat HTTP-specific HTTPS/SVCB and HTTP/3 policy here as temporary; the
     final c-ares resolver plan moves that logic to `zhttp`.
   - Acceptance:
     - Resolver tests use synthetic DNS payloads for `ipv6hint`; no public DNS is
       required.
     - IPv4 result order remains as close as possible to current behavior.
     - Combined IPv4/IPv6 hint duplicate suppression is family-aware.
     - Gate: `make -C zi/src -j8 && make -C zi/test -j8 &&
       ./zi/test/ZiResolverTest`.
   - Unlocks: socket and protocol code can rely on resolver-produced `ZiIP`
     values carrying accurate family state.

3. TCP/UDP socket paths
   - Convert `ZiMultiplex` TCP and UDP setup to select socket families from
     `ZiIP::type()`/`ZiSockAddr::type()`; do not include multicast in this slice.
   - Replace generic-path `AF_INET`, `sockaddr_in`, and fixed address-length
     assumptions with family-specific `switch` branches and `ZiSockAddr::len()`.
   - Keep public entry points as thin Rx/Tx dispatchers that value-capture
     address data; do not add locks to make non-owner access acceptable.
   - Acceptance:
     - IPv4 loop tests remain green.
     - IPv6 loopback connect/accept, `recvfrom`, and `sendto` paths pass on
       Linux epoll.
     - Windows IOCP address buffer sizing source is updated for `sockaddr_in6`.
     - Gate: `make -C zi/src -j8 && make -C zi/test -j8 &&
       ./zi/test/ZiMxLoopTest`; exclude the equivalent Windows IOCP test while
       Windows-only libraries are outside the active top-level build.
   - Unlocks: multicast and higher-level modules can use family-aware sockets.

4. Multicast option split
   - Replace IPv4-only multicast request and interface state with type-tagged
     native IPv4/IPv6 request storage.
   - Keep IPv4 behavior unchanged and reject mixed-family multicast requests
     before `setsockopt`.
   - Use `switch` on address family for native option names and argument types;
     do not hide platform differences behind generic helper layers unless they
     remove real duplication without obscuring native semantics.
   - Acceptance:
     - `ZiMReq` tests cover construction, `type()`, `addr()`, `mif()`,
       `ifIndex()`, equality, comparison, hash, and print for V4 and V6.
     - `ZiCxnOptions` tests cover V4-only, V6-only, mixed-family rejection, TTL
       and loopback state.
     - Winsock IPv6 multicast request type spelling is covered in source through
       a local alias, not scattered `#ifdef`s; Windows build/test execution is
       excluded from this gate.
     - IPv4 multicast behavior remains unchanged; IPv6 multicast loopback smoke
       tests pass where the platform supports them.
   - Unlocks: telemetry/schema changes can represent all multicast state
     without overloading IPv4 address fields.

5. Persistence, telemetry, and wire schemas
   - Widen `Zfb.IP` and update dependent transforms, PostgreSQL encoding,
     telemetry schemas, and display/primary-key fields in one slice.
   - Do not encode null/default `ZiIP` as an accidental all-zero IPv4 value;
     make containing field semantics explicit.
   - Acceptance:
     - Generated flatbuffer outputs are regenerated as required.
     - `ZfbTransform::IP` saves/loads IPv4 and IPv6 from native address structs
       without byte-order churn.
     - `ZdbPQ::IP` binary `inet` send/receive uses `IPHdr` family `2`/len `4`
       for IPv4 and family `3`/len `16` for IPv6, with no inactive `ZiIP`
       storage or padding on the wire.
     - `zfb`, `zdb`, and `zdb_pq` compile and affected persistence tests pass.
       `zcmd` source/schema references are updated as needed, but no `zcmd`
       build or test command is attempted while it remains a work-in-progress
       module outside active top-level `SUBDIRS`.
   - Unlocks: dependent modules can persist, report, and compare IPv6 addresses
     without local schema workarounds.

6. Dependent modules and examples
   - Sweep `zrest`, `ztcp`, `ztls`, `zhttp`, `zquic`, `zproxy`, `zdb`, command
     examples, and tests for bracketed IPv6 literals and type-aware address
     handling.  Work-in-progress modules such as `zrest`, `zws`, and `zproxy`
     remain source-update scope only while outside active top-level `SUBDIRS`.
   - Read module-specific guidelines before editing modules that have them, e.g.
     `zquic/GUIDELINES.md`.
   - Acceptance:
     - Representative TCP, UDP, TLS, HTTP, and QUIC IPv6 loop tests pass for
       active top-level modules.
     - Existing IPv4 examples and tests remain green.
     - Top-level `make -j8` succeeds with no warnings.
   - Unlocks: the repository is ready for the c-ares resolver replacement.

7. Handoff to `resolver.md`
   - Start `resolver.md` only after slices 1-6 are complete and green.
   - Treat any async resolver work that uncovers IPv6 address/socket gaps as a
     blocker in this plan, not as resolver-local compatibility code.

## Final Acceptance Criteria

- `ZiIP`/`ZiSockAddr` are family-aware value types that preserve IPv4 behavior
  and expose native IPv4/IPv6 address data without hidden heap allocation.
- Generic socket paths no longer assume fixed IPv4 address size or family.
- Multicast, telemetry, persistence, and examples represent IPv6 explicitly
  without overloading IPv4 fields.
- Public I/O entry points preserve Rx/Tx ownership and value-capture semantics.
- All slice gates have passed, followed by a top-level `make -j8`.
- Linux epoll builds compile the IPv6 address/socket code.  Windows-specific
  source uses Winsock APIs rather than assuming POSIX `inet_pton`, but
  Windows-only compile/test runs are excluded while those libraries are outside
  the active top-level build.
- Any optional platform-specific IPv6 test that cannot run in the local
  environment is documented with the platform/toolchain reason and an equivalent
  lower-level unit test remains in place.
