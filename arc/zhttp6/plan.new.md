## Summary

Migrate hostname resolution out of `ZiIP` into a new `ZiResolver` facility, add DNS HTTPS/SVCB support for HTTP/3 endpoint discovery, and update the `zhttp` client/example probing path to use DNS metadata before attempting QUIC.

IPv6 remains out of scope. The new resolver should preserve the current IPv4-only transport surface while avoiding API shapes that would force a future HTTP/3 probing API change when IPv6 is added.

External API research:
- RFC 9460 defines SVCB and HTTPS RRs for service binding metadata. HTTPS is the HTTP-specific SVCB-compatible RR type; SVCB type is 64 and HTTPS type is 65. Relevant parsing and behavior requirements include AliasMode vs ServiceMode, `mandatory`, `alpn`, `no-default-alpn`, `port`, and `ipv4hint`.
- RFC 9460 requires strict validation of malformed SvcParam values. `alpn` is a sequence of single-octet-length-prefixed IDs that must exactly fill the parameter value; `no-default-alpn` has an empty value and is only self-consistent when `alpn` is present.
- Microsoft documents that `DNS_TYPE_SVCB` and `DNS_TYPE_HTTPS` records returned by DNS APIs can be parsed as `DNS_SVCB_DATA`, but `DnsQueryEx`/DNS query callers must set `DNS_QUERY_PARSE_ALL_RECORDS` to receive parsed SVCB data rather than flat record data.
- Linux resolver documentation says `res_query()` uses global resolver state and is non-reentrant, while `res_nquery()` takes explicit resolver state and should be preferred for thread-safe use. `res_nquery()` leaves the raw DNS response in the caller-provided buffer.
- Comparable HTTP clients treat HTTPS RRs as an optimization and policy input rather than the only upgrade mechanism. `zhttp` should keep its existing Alt-Svc HTTP/3 upgrade path and only make `--http3` DNS-first; `--http3-only` should fail clearly when DNS metadata does not advertise H3 unless a separate blind-probe option is intentionally added later.

High-level design:
- Add `zi/src/ZiResolver.hh` and `zi/src/ZiResolver.cc`.
- Keep `ZiIP` as a compact IPv4 address value type. Move `getaddrinfo()`/`GetAddrInfo` address enumeration and reverse-name support behind `ZiResolver`, leaving `ZiIP` constructors and assignment to delegate to the new resolver.
- Add a small DNS parser inside `ZiResolver.cc` for DNS message framing, compressed names, and HTTPS/SVCB RDATA. Expose only the resolver/result APIs in the public header.
- Add deterministic parser tests with static DNS wire buffers. Live DNS remains limited to the existing style of localhost/IPv4 resolution tests and must not be required for HTTPS/SVCB coverage.
- Update `zhttp/example/zhttp.cc` to replace `resolveForQUIC()` with a DNS HTTPS/SVCB-aware helper using `ZiResolver`. The library headers in `zhttp/src/` do not currently contain resolver behavior, so this phase is primarily client/example behavior plus tests.

## Architecture Documentation

### New Components

`zi/src/ZiResolver.hh`
- Public resolver interface for IPv4 address enumeration, first-address resolution, reverse name lookup, HTTPS/SVCB lookup, and HTTP/3 endpoint selection.
- Include only direct dependencies: `ZiLib.hh`, `ZiPlatform.hh`, `ZiIP.hh` where a complete `ZiIP` value is needed, `ZmFn.hh`, `ZtArray.hh` or `ZtLocalArray.hh` only if public retained result containers require them, and `ZePlatform.hh` for `ZeError`.
- Avoid STL and concepts. Use short Z-style names and SFINAE/CRTP-compatible simple types.

`zi/src/ZiResolver.cc`
- Owns platform-specific resolver code currently in `ZiIP.cc`.
- Owns DNS wire parser helpers in an internal namespace.
- Keeps `_WIN32`/Linux resolver conditionals isolated here.

`zi/test/ZiResolverTest.cc`
- Tests IPv4 resolution API and `ZiIP` delegation.
- Tests DNS wire-name and HTTPS/SVCB RDATA parsing with static buffers.
- Tests HTTP/3 endpoint selection from parsed records without live DNS.

Optional `zi/src/ZiResolverTestHooks.hh` should not be added unless necessary. Prefer making parser entry points public-but-low-level under `ZiResolver` only if tests and future callers genuinely need them. Otherwise keep parser helpers internal and expose a small test-only parse wrapper from `ZiResolver.hh` only under a local test macro.

### Changed Components

`zi/src/ZiIP.hh`
- Remove the primary `Zi::resolve()` declaration from this header after call sites migrate, or leave a temporary deprecation-free thin declaration only within the migration phase.
- Keep `ZiIP::resolve(S &&, ZeError *)` but delegate to `ZiResolver::resolve()`.
- Keep `ZiIP::name()` but implement it through `ZiResolver::name(ZiIP, ZeError *)`.

`zi/src/ZiIP.cc`
- Shrink to `ZiIP` value-type helpers and simple delegations.
- Move Windows `WSAStartup`, MinGW dynamic `GetAddrInfoW` compatibility, `getaddrinfo()`/`GetAddrInfo`, and `getnameinfo()`/`GetNameInfo` code into `ZiResolver.cc`.

`zhttp/example/zhttp.cc`
- Replace `resolveForQUIC()` with a helper that asks `ZiResolver` for H3 endpoints.
- Preserve existing Alt-Svc parsing and H1 fallback semantics.
- Keep the URL type's separation between display host (`ZtString<> host`) and DNS host (`Zi::Hostname dnsHost`), but pass both origin/SNI host and DNS host explicitly to the resolver selection API.

`configure.ac`
- Add feature checks for Linux resolver APIs and Windows DNS APIs.
- On Linux, detect whether `res_nquery()` links without extra libraries or requires `-lresolv`; append the needed library to `Z_IO_LIBS`.
- On MinGW/Windows, add `-ldnsapi` to `Z_IO_LIBS` when `DnsQueryEx`/DNS query support is enabled. Existing `Z_IO_LIBS` already includes `-lws2_32 -lwsock32`.

`zi/src/Makefile.am`
- Add `ZiResolver.hh` to `pkginclude_HEADERS`.
- Add `ZiResolver.cc` to `libZi_la_SOURCES`.

`zi/test/Makefile.am`
- Add `ZiResolverTest` to `noinst_PROGRAMS` and `test`.

`zhttp/test/Makefile.am`
- Add a focused `ZhttpH3DNSTest` only after the client probing logic has a testable seam. If the example binary remains hard to unit test, factor DNS selection into a small helper header/source under `zhttp/example/` rather than adding network-dependent tests.

### Interfaces

Proposed public API sketch:

```c++
namespace ZiResolver {

using Host = Zi::Hostname;

enum {
  H3MaxIPs = 8,
  H3MaxALPN = 8,
  H3AliasDepth = 4,
  DNSMsgMax = 4096
};

struct Addr {
  ZiIP ip;
};

struct HTTPS {
  uint16_t priority = 0;
  Host target;
  uint16_t port = 0;
  bool noDefaultALPN = false;
  bool hasALPN = false;
  bool hasIPv4Hint = false;
  bool unknownMandatory = false;
  bool hasH3 = false;

  bool alias() const { return !priority; }
};

struct H3Endpoint {
  Host dnsHost;
  Host tlsHost;
  ZiIP ip;
  uint16_t port = 443;
  bool fromHTTPS = false;
  bool fromIPv4Hint = false;
};

enum class H3Policy : int8_t {
  DNSOnly,
  DNSWithBlindFallback
};

ZiExtern int resolve(Host host, ZmFn<bool(ZiIP)> fn, ZeError *e = nullptr);
ZiExtern Host name(ZiIP ip, ZeError *e = nullptr);

ZiExtern int https(
  Host host, ZmFn<bool(const HTTPS &)> fn,
  ZeError *e = nullptr);

ZiExtern int http3(
  Host dnsHost, Host tlsHost, uint16_t port, H3Policy policy,
  ZmFn<bool(const H3Endpoint &)> fn, ZeError *e = nullptr);

}
```

Implementation may refine names, but keep them short and consistent with nearby code. The key behavior is callback-oriented streaming for address and record enumeration; retained arrays should be bounded and use `ZtLocalArray`/`ZtArray` with heap IDs.

### Data Flows

IPv4 resolution:
1. Caller passes `Zi::Hostname`.
2. `ZiResolver::resolve()` calls `getaddrinfo()`/`GetAddrInfo` with `AF_INET`.
3. Each unique `ZiIP` is emitted to the callback.

HTTPS/SVCB lookup:
1. Caller passes origin DNS host.
2. Linux backend calls `res_nquery()` where available with class `C_IN`, type `65` for HTTPS. Fallback to `res_query()` only behind a feature conditional and document the non-reentrant limitation.
3. Windows backend calls `DnsQueryEx` synchronously for `DNS_TYPE_HTTPS`, setting `DNS_QUERY_PARSE_ALL_RECORDS` when structured SVCB output is available. If SDK/runtime support is incomplete, use a raw-record fallback and parse wire data locally.
4. The parser validates DNS framing and emits parsed `HTTPS` records.

HTTP/3 endpoint selection:
1. Query HTTPS for the origin host.
2. Follow AliasMode records (`priority == 0`) with bounded recursion (`H3AliasDepth`).
3. Consider ServiceMode records only when they are self-consistent and their mandatory keys are understood.
4. Select records whose effective ALPN set contains `h3`.
5. Use ServiceMode `port` if present; otherwise use the origin URL port.
6. Prefer `ipv4hint` candidates when present, but do not make them the only path: also allow normal A lookup for the service target because hints can be stale and are not a substitute for address resolution.
7. If no DNS-advertised H3 endpoint is found, only emit direct origin candidates when policy is `DNSWithBlindFallback`.

### Event-Driven, Timer, Network, and Data Store Changes

- No new threads, schedulers, timers, event loops, or persistent data stores are required.
- DNS calls are synchronous in the initial implementation, matching current `Zi::resolve()` behavior. Do not integrate resolver queries into `ZiMultiplex` in this scope.
- Network programming changes are limited to resolver library calls and parsing DNS responses. Existing TCP/TLS/QUIC connection code remains unchanged.

## Detailed Design and Implementation Plan

### Phase 1: Add ZiResolver IPv4 Facade and Keep ZiIP Working

- Add `zi/src/ZiResolver.hh`/`.cc` with `resolve()` and `name()`.
- Move the existing platform address-resolution implementation from `ZiIP.cc` into `ZiResolver.cc` with minimal behavior changes:
  - Preserve Windows `WSAStartup` singleton behavior.
  - Preserve MinGW dynamic `GetAddrInfoW`/`GetNameInfoW` fallback if still needed by current headers.
  - Preserve Linux `getaddrinfo()` constrained to `AF_INET`.
  - Preserve retry on `EAI_AGAIN`.
- Update `ZiIP::resolve()` and `ZiIP::name()` to delegate to `ZiResolver`.
- Keep `Zi::resolve()` temporarily as a thin wrapper in `ZiResolver.cc` or `ZiIP.cc` for dependent call sites in this phase only. It should call `ZiResolver::resolve()` and be removed after migration.
- Add `ZiResolverTest` coverage for first IPv4 resolution and `ZiIP` string assignment/delegation. Keep current `ZiIPTest` intact.

Dependency rationale: this phase creates the replacement API before call-site migration and is behavior-preserving.

### Phase 2: Migrate Address Enumeration Call Sites

- Replace direct `Zi::resolve()` users with `ZiResolver::resolve()`:
  - `zhttp/example/zhttp.cc:797` currently uses `resolveForQUIC()` and should move later to H3 selection, but can first compile against `ZiResolver::resolve()` if needed.
  - Any future `rg "Zi::resolve"` hits in examples/tests should be migrated.
- Remove or stop exporting the old `Zi::resolve()` declaration from `ZiIP.hh` once all call sites are moved.
- Keep `ZiIP` constructors source-compatible for string construction/assignment.

Dependency rationale: remove external dependency on the old resolver entry point before adding more resolver behavior.

### Phase 3: Add DNS Wire Parser and RFC 9460 RDATA Parser

- Implement internal parser helpers in `ZiResolver.cc`:
  - DNS header parse with `qdcount`, `ancount`, `nscount`, `arcount`.
  - Question skipping.
  - Compressed-name parser with pointer bounds, loop/step limit, label length checks, and output truncation detection.
  - Answer iteration that filters RR type HTTPS (`65`) and optionally SVCB (`64`) where needed.
  - Big-endian integer helpers for 16-bit and 32-bit fields.
- Implement HTTPS/SVCB RDATA parse:
  - `SvcPriority`.
  - `TargetName`.
  - SvcParam sequence with strictly increasing key order. This rejects duplicate keys naturally and catches unsorted records.
  - `mandatory` list as 16-bit keys; reject malformed odd length.
  - `alpn` list as length-prefixed ALPN IDs; reject zero-length IDs and values that do not exactly fill the field.
  - `no-default-alpn` as empty only; reject if non-empty.
  - `port` as exactly two bytes.
  - `ipv4hint` as a multiple of four bytes.
  - Unknown keys as bounded opaque spans during parse only.
- Enforce record usability:
  - Reject malformed/truncated records.
  - Reject unknown mandatory keys.
  - Reject `no-default-alpn` without `alpn` for HTTP/3 endpoint selection.
  - Treat the root target name (`"."`) according to RFC 9460: in ServiceMode it means use the owner/origin host; in AliasMode special root handling prevents following an empty alias.
- Avoid heap allocation while parsing individual records. Copy only target names and retained ALPN/IPv4 data needed beyond the DNS response buffer.

Dependency rationale: static parser tests can be completed before any OS DNS query backend is enabled.

### Phase 4: Add HTTPS/SVCB Query Backends

- Linux:
  - Include resolver headers only in `ZiResolver.cc`.
  - Add `configure.ac` checks for `res_ninit`, `res_nquery`, `res_nclose`, and `res_query`.
  - Detect whether `-lresolv` is needed and append it to `Z_IO_LIBS`.
  - Prefer per-call or thread-local `__res_state` initialized with `res_ninit()` and closed with `res_nclose()`.
  - Use caller-provided/static bounded response storage initially sized by `DNSMsgMax`; if a response is truncated, return a clear resolver error rather than silently accepting partial data. A later enhancement can retry with TCP/larger buffers.
- Windows:
  - Add `dnsapi` linkage in `configure.ac`/`Z_IO_LIBS`.
  - Use `DnsQueryEx` synchronously by omitting the completion callback.
  - Set `DNS_QUERY_PARSE_ALL_RECORDS` when using `DNS_SVCB_DATA`.
  - Free returned record lists with `DnsRecordListFree`.
  - Add compile-time guards for `DNS_TYPE_HTTPS`, `DNS_TYPE_SVCB`, `DNS_SVCB_DATA`, and related structs. If MinGW headers expose constants but not structured data, query raw records and feed the common wire/RDATA parser.
- Make `https()` return:
  - `Zi::OK` when at least one usable record is emitted.
  - `Zi::IOError` with `ZeError` for query failure, no records, or unsupported platform capability.
  - No live DNS dependency in parser tests; backend tests may be limited to disabled/manual smoke tests if environment variance is too high.

Dependency rationale: backend work relies on the parser from Phase 3 and the build-system probes from this phase.

### Phase 5: Add HTTP/3 Endpoint Selection API

- Implement `ZiResolver::http3()` on top of `https()` and `resolve()`.
- Inputs:
  - DNS host used for resolver queries.
  - TLS/SNI host used by the caller for certificate validation and connection identity.
  - Origin/default port.
  - Policy (`DNSOnly` vs `DNSWithBlindFallback`).
- Candidate construction:
  - AliasMode follows target names with bounded recursion and cycle detection by depth. Do not allocate unbounded visited sets.
  - ServiceMode target `"."` resolves to the owner/origin host.
  - ServiceMode target names override the DNS/connect host but not the TLS/SNI origin unless the caller explicitly models delegated identity later. For this scope, preserve current `zhttp` behavior: URL host remains the TLS host.
  - Emit IPv4 hints first when present, deduplicated against normal A results.
  - Fall back to A lookup of the selected target.
  - Deduplicate emitted `ZiIP` candidates with a bounded local array sized like the current `DNSMaxIPs` (`8`) unless testing shows a better default.
- Failure behavior:
  - `DNSOnly`: no H3-capable HTTPS/SVCB record means failure/no candidates.
  - `DNSWithBlindFallback`: no H3-capable DNS metadata emits direct origin A-record candidates, preserving current `--http3` fallback attempt behavior.

Dependency rationale: this is the vertical feature seam consumed by `zhttp`; it should be tested independently before wiring the client.

### Phase 6: Wire zhttp HTTP/3 Probing

- Replace `resolveForQUIC()` in `zhttp/example/zhttp.cc` with a resolver selection helper:
  - `--http3`: call `ZiResolver::http3(..., DNSWithBlindFallback, ...)`; attempt QUIC for emitted candidates or retain current `run<QUICClient>()` if connection code still resolves internally. Log whether DNS advertised H3 or the client is blind-probing.
  - `--http3-only`: call `ZiResolver::http3(..., DNSOnly, ...)`; fail clearly when no H3-capable HTTPS/SVCB endpoint is found.
  - H1 Alt-Svc upgrade: call `ZiResolver::http3()` for the alternate endpoint host/port with `DNSWithBlindFallback` or a dedicated Alt-Svc policy. Alt-Svc itself is authoritative enough to justify trying H3 even when HTTPS RR is absent, so do not make Alt-Svc upgrades DNS-only.
- Preserve existing redirect handling, Alt-Svc parsing, output-file restore behavior, and TLS fallback semantics.
- Keep connection API changes minimal. If `run<QUICClient>()` cannot consume selected IPs directly, first use `ZiResolver::http3()` as the policy gate and leave actual connection address resolution unchanged. A later phase can thread explicit IP candidates into `Zquic::CliLink` if needed.

Dependency rationale: this is the external behavior change and should come only after resolver selection is tested.

### Phase 7: Tests and Behavior Coverage

- Add `zi/test/ZiResolverTest.cc`:
  - IPv4 `resolve("127.0.0.1", ...)`.
  - `ZiIP` string construction and assignment still resolve via `ZiResolver`.
  - DNS compressed-name parser:
    - normal labels.
    - compressed names.
    - compression loop rejection.
    - out-of-bounds pointer rejection.
  - HTTPS/SVCB parser:
    - valid `alpn=h3`.
    - valid `port`.
    - valid `ipv4hint`.
    - valid AliasMode target.
    - truncated RDATA rejection.
    - duplicate key rejection.
    - unsorted key rejection.
    - unknown mandatory key rejection.
    - invalid ALPN vector rejection.
    - invalid `port` length rejection.
    - invalid `ipv4hint` length rejection.
    - `no-default-alpn` without `alpn` rejected for selection.
  - H3 selector:
    - selects HTTPS record advertising `h3`.
    - rejects `h2`/`http/1.1` only.
    - uses ServiceMode `port`.
    - applies AliasMode within depth.
    - fails on alias depth exhaustion.
    - deduplicates IPv4 hints and A results.
- Add `zhttp` behavior tests where practical:
  - DNS-advertised H3 allows `--http3-only`.
  - No H3 DNS metadata makes `--http3-only` fail before QUIC.
  - `--http3` still falls back to TLS when H3 DNS metadata is absent and QUIC fails.
  - Alt-Svc H3 path resolves alternate endpoint through `ZiResolver`.
- Avoid live DNS in deterministic tests. Build static DNS wire buffers in code using small helpers rather than long opaque byte arrays where that improves maintainability.

Dependency rationale: parser and selector tests belong before `zhttp` wiring; zhttp tests validate CLI behavior after the integration phase.

### Phase 8: Cleanup and Documentation

- Remove obsolete resolver implementation and old `Zi::resolve()` wrapper after all users are migrated.
- Update comments in `ZiIP.hh` to state that `ZiIP` is IPv4-only and hostname resolution is delegated.
- Update `zhttp/README.md` to describe HTTP/3 discovery policy:
  - DNS HTTPS/SVCB is consulted first for `--http3`.
  - `--http3-only` requires DNS-advertised H3.
  - Alt-Svc upgrade remains supported.
  - IPv6, DoH, DoT, ECH, WebTransport, DATAGRAM, and QUIC v2 remain non-goals.

## Code References to Impacted Code

- `zi/src/ZiIP.hh:32` - Current `Zi::resolve()` declaration should move to `ZiResolver.hh` as `ZiResolver::resolve()` and later be removed from `ZiIP.hh`.
- `zi/src/ZiIP.hh:131` - `ZiIP::resolve()` should delegate to `ZiResolver::resolve()`.
- `zi/src/ZiIP.cc:36` - Windows dynamic Winsock helpers should move to `ZiResolver.cc`.
- `zi/src/ZiIP.cc:166` - Current `Zi::resolve()` implementation should move to `ZiResolver::resolve()`.
- `zi/src/ZiIP.cc:203` - Reverse lookup should move behind `ZiResolver::name()`.
- `zi/src/Makefile.am:12` - Add `ZiResolver.hh` to installed headers.
- `zi/src/Makefile.am:21` - Add `ZiResolver.cc` to `libZi_la_SOURCES`.
- `zi/test/Makefile.am:14` - Add `ZiResolverTest` to `noinst_PROGRAMS` and `test`.
- `zi/test/ZiIPTest.cc:19` - Keep existing IPv4 parse/resolve/print coverage; add resolver-specific tests rather than bloating this file.
- `configure.ac:70` - Extend platform library setup and feature checks for resolver APIs.
- `configure.ac:98` - Add Windows DNS API linkage (`dnsapi`) alongside Winsock libraries when available.
- `zhttp/example/zhttp.cc:65` - Existing `URL` structure already separates display host and `dnsHost`; reuse this for resolver inputs.
- `zhttp/example/zhttp.cc:208` - Existing Alt-Svc parser remains supported; resolver is used after parsing.
- `zhttp/example/zhttp.cc:797` - Replace `resolveForQUIC()` with `ZiResolver::http3()` policy gating/selection.
- `zhttp/example/zhttp.cc:830` - `runH3DNSFirst()` should distinguish DNS-advertised H3 from blind fallback policy.
- `zhttp/example/zhttp.cc:842` - `runH1AltSvcFirst()` should resolve alternate endpoint through `ZiResolver`.
- `zhttp/test/Makefile.am:23` - Add zhttp DNS/H3 behavior test only once a deterministic test seam exists.

## Detailed Test Plan

Use `ZuTestUtil` TAP-style tests matching existing `zi/test/*Test.cc` patterns.

`ZiResolverTest` design:
- Provide local helper builders for DNS messages:
  - `put16`, `put32`, `name`, `ptr`, `question`, `answerHTTPS`.
  - Keep helpers in the test file; no production test-builder API.
- Build static response buffers for:
  - owner `example.com`, HTTPS answer priority `1`, target `.`, `alpn=h3`.
  - ServiceMode `target=svc.example.com`, `port=8443`, `ipv4hint=192.0.2.1`.
  - AliasMode priority `0`, target `alias.example.com`.
  - Malformed cases.
- Assert parser return codes and selected endpoint fields directly.
- For live-ish IPv4 resolution, use numeric `127.0.0.1` first. Keep `localhost` coverage only where existing tests already tolerate host file differences.

`zhttp` test design:
- Prefer testing a factored helper that accepts a fake resolver callback/result provider. This keeps DNS and QUIC out of the deterministic unit path.
- Continue using existing interop tests for actual HTTP/3 transport behavior; do not add live DNS dependencies there.
- For CLI behavior, if needed, add a narrow subprocess test using a fake resolver hook compiled into the test binary, not environment-dependent public DNS.

Manual smoke tests after implementation:
- `./zi/test/ZiResolverTest`
- `./zi/test/ZiIPTest`
- `./zhttp/test/ZhttpFallbackTest`
- `./zhttp/test/Zhttp3InteropTest` when curl/caddy/http3 dependencies are available
- `zhttp --http3 https://<known HTTPS-RR h3 host>/`
- `zhttp --http3-only https://<known no-HTTPS-RR host>/` should fail before blind QUIC

## Acceptance Criteria

- `ZiIP` no longer owns address-resolution implementation logic.
- `ZiResolver` resolves IPv4 addresses on Linux and Windows.
- `ZiResolver` can query HTTPS/SVCB records on Linux and Windows where platform APIs are available.
- DNS wire parser rejects malformed compression, truncated messages, duplicate/unsorted SvcParam keys, and invalid RFC 9460 parameter values.
- HTTP/3 endpoint selector emits only `h3`-capable DNS-advertised candidates under `DNSOnly`.
- `--http3` remains fallback-capable, while `--http3-only` fails clearly when no DNS HTTPS/SVCB record advertises H3.
- Alt-Svc H3 upgrade continues to work and resolves alternate endpoints through `ZiResolver`.
- IPv6 remains unsupported and documented as a non-goal.
- Existing IPv4 TCP/TLS/QUIC behavior remains functional.
- Static parser and selector tests do not depend on live DNS.

## Non-goals

- IPv6 support.
- DNS-over-HTTPS or DNS-over-TLS.
- Full recursive resolver implementation.
- ECH parsing beyond ignoring or rejecting unknown mandatory keys as required.
- Generic DNS record support beyond A and HTTPS/SVCB needed for this feature.
- Asynchronous resolver integration with `ZiMultiplex`.
- Long-term compatibility wrappers for the old `Zi::resolve()` API.
- Threading explicit selected IP candidates into every transport connection path if policy gating is sufficient for the first implementation.

## Options and Open Questions

Resolved decisions:
- Use `res_nquery()` over `res_query()` on Linux when available because the former carries explicit resolver state and avoids global-state resolver hazards.
- Use `DnsQueryEx` synchronously on Windows, with `DNS_QUERY_PARSE_ALL_RECORDS` for structured `DNS_SVCB_DATA` where available.
- Keep parser code local and hand-coded. Adding c-ares or another DNS dependency is unnecessary for the current scope and would be disproportionate in this codebase.
- Keep HTTPS/SVCB tests static and deterministic.
- Keep IPv6 out of the public endpoint result for now, but use neutral names like `H3Endpoint` rather than `H3IPv4Endpoint`.

Implementation options:
- Windows SVCB parsing:
  - Preferred: consume `DNS_SVCB_DATA` when headers/runtime support it.
  - Fallback: use raw DNS record data and common parser when MinGW headers are incomplete.
- zhttp selected-IP use:
  - Minimal first pass: `ZiResolver::http3()` decides whether H3 should be attempted, while existing QUIC connect code resolves the host normally.
  - Deeper later pass: thread explicit `H3Endpoint.ip` candidates into QUIC connection setup to fully honor `ipv4hint` ordering.
- Linux response sizing:
  - Initial: bounded `DNSMsgMax` stack/local buffer with clear failure on truncation.
  - Later: retry with larger heap-backed `ZtArray` or TCP fallback if real deployments need it.

No blocking open questions remain for the first implementation. The only substantial complexity is Windows SDK variance around structured SVCB records; the raw-parser fallback makes this tractable without changing the public API.
