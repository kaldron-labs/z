# zquic guideline review

Scope: reviewed `zquic/src`, `zquic/test`, `zquic/example`, `zquic/README.md`, `zquic/buffers.md`, and the module `Makefile.am` files against `AGENTS.md` and `GUIDELINES.md`. Generated build outputs, object files, `.libs`, `.deps`, and `Makefile.in` files were ignored.

## Overall alignment

`zquic` is broadly written in the local Z style: the public transport API is CRTP-oriented, core containers are `ZmHash`, `ZmPQueue`, `ZtArray`, `ZtBuiltin`, and `ZiIOBuf`, source files avoid STL in the library, packet/stream buffers use named heaps, and the code is already trying to shard runtime work across Rx/Tx scheduler threads.

The main gaps are not cosmetic. They are mostly places where the implementation keeps fixed runtime caps, uses C++20 `requires`, duplicates client/server paths, or takes hot-path heap/library allocation shortcuts that conflict with the performance and Z-framework guidance.

## Findings

### High: public templates use `requires`, which the guidelines explicitly forbid

`GUIDELINES.md` and `AGENTS.md` say to avoid concepts and `requires`, and to express compile-time constraints with `ZuIfT`/SFINAE detector structs. `zquic/src/Zquic.hh` uses `if constexpr (requires ...)` throughout the public template surface:

- `zquic/src/Zquic.hh:1007`
- `zquic/src/Zquic.hh:1040`
- `zquic/src/Zquic.hh:1104`
- `zquic/src/Zquic.hh:1500`
- `zquic/src/Zquic.hh:1511`
- `zquic/src/Zquic.hh:1797`
- `zquic/src/Zquic.hh:1988`
- `zquic/src/Zquic.hh:2486`
- `zquic/src/Zquic.hh:3103`

This should be converted to detector traits or existing Z ADL/tag idioms so application conformance remains compatible with the repository's compile-time style.

### High: server accepts at most 16 live links

The server path routes datagrams for active `SrvLink`s, but active link retention is a fixed array:

- `zquic/src/Zquic.hh:1121` scans `m_links`
- `zquic/src/Zquic.hh:1159` sets `MaxLinks = 16`
- `zquic/src/Zquic.hh:1162` stores `LinkRef m_links[MaxLinks]`

Once full, `addLink_()` emits `"QUIC server active SrvLink table is full"` and rejects further accepted links. This conflicts with the README's server routing description and with the guideline warning against fixed-size runtime tables. Use a `ZmHash`/intrusive list keyed by link or CID, with a named heap and tunable sizing, rather than a hard cap.

### High: endpoint Tx backlog is a manual fixed ring that silently back-pressures/drops

`Endpoint::Cxn_` uses two fixed arrays plus separately maintained head/tail/count:

- `zquic/src/ZquicEndpoint.cc:21` sets `MaxTxQueue = 32`
- `zquic/src/ZquicEndpoint.cc:51` returns `false` when full
- `zquic/src/ZquicEndpoint.cc:130`
- `zquic/src/ZquicEndpoint.cc:131`
- `zquic/src/ZquicEndpoint.cc:132`

This is exactly the fixed-array pattern called out in `GUIDELINES.md`. It is also on the packet send path and has no explicit diagnostic for queue-full events. Replace it with a Z container or queue node structure using a named heap, and account for queue-full/back-pressure in diagnostics.

### Medium: active connection ID storage is a fixed eight-slot table

Connection ID state is limited by `MaxConnectionIDs = 8` and stored in arrays:

- `zquic/src/Zquic.hh:1685`
- `zquic/src/Zquic.hh:1826`
- `zquic/src/Zquic.hh:1847`
- `zquic/src/Zquic.hh:2626`
- `zquic/src/Zquic.hh:2627`

The comment notes mainstream caps, but the local peer's `active_connection_id_limit` is runtime data and this table is not runtime-sized or tunable. If the cap is intentional, it should be encoded as a transport policy with validation and diagnostics; otherwise this should move to a small `ZmHash`/array container with named allocation.

### Medium: Initial/retry packet crypto allocates OpenSSL contexts per operation

Initial packet protection and retry integrity create/free `EVP_CIPHER_CTX` for each operation:

- `zquic/src/ZquicPacket.cc:198`
- `zquic/src/ZquicPacket.cc:218`
- `zquic/src/ZquicCrypto.cc:274`
- `zquic/src/ZquicCrypto.cc:293`
- `zquic/src/ZquicCrypto.cc:307`
- `zquic/src/ZquicCrypto.cc:324`
- `zquic/src/ZquicCrypto.cc:333`
- `zquic/src/ZquicCrypto.cc:344`

These are heap allocations outside `ZmHeap`/`ZmVHeap` and occur in packet processing. Even though Initial traffic is handshake-scoped, it is still latency-sensitive and exposed to hostile input. Prefer reusable per-link packet-protection state or `Ztls::Pico`/local backend wrappers that avoid untracked allocator churn.

### Medium: frame dispatch is long chained `if` logic where `switch` fits the local rule

`GUIDELINES.md` flags chained `if` statements that should be `switch`. `FrameCodec::parse()` is a long frame-type chain:

- `zquic/src/ZquicFrame.cc:25`
- `zquic/src/ZquicFrame.cc:131`
- `zquic/src/ZquicFrame.cc:142`
- `zquic/src/ZquicFrame.cc:179`
- `zquic/src/ZquicFrame.cc:237`
- `zquic/src/ZquicFrame.cc:263`
- `zquic/src/ZquicFrame.cc:322`
- `zquic/src/ZquicFrame.cc:370`

`Link::consumeProtectedFrames_()` repeats the pattern for parsed frame dispatch at `zquic/src/Zquic.hh:2470` through `zquic/src/Zquic.hh:2498`. A `switch` or `ZuSwitch`-style dispatch would better match the guideline, make protocol coverage easier to audit, and reduce branch-shape noise.

### Medium: client and server link implementations duplicate substantial runtime logic

`CliLink` and `SrvLink` duplicate send, crypto-flight, packet send, ACK flush, datagram receive, and frame consume plumbing:

- client send path starts at `zquic/src/Zquic.hh:2708`
- client crypto/send/ACK/receive helpers span `zquic/src/Zquic.hh:2866` through `zquic/src/Zquic.hh:3088`
- server send path starts at `zquic/src/Zquic.hh:3155`
- server crypto/send/ACK/receive helpers span `zquic/src/Zquic.hh:3266` through `zquic/src/Zquic.hh:3501`

Some role differences are real, but most of the packet-building and dispatch shape is near-identical. This violates the DRY amber flag and raises risk that future recovery, ACK, packet protection, or diagnostic changes will land on one side only.

### Low: retransmit-drop diagnostics are placeholders

The README says diagnostics expose loss, retransmission, and buffer-contract information, but retransmit drop accounting is currently hardcoded:

- `zquic/src/ZquicRecovery.hh:419` returns `0`
- `zquic/src/ZquicRecovery.hh:543` exposes that value through `retransmitDropped()`

If drops cannot happen with the current queue, the API should make that explicit. If drops can happen later due to limits/back-pressure, this needs real accounting before the diagnostics contract is considered aligned.

## Notable non-findings

The fixed byte arrays for protocol-sized values such as connection IDs, reset tokens, keys, IVs, nonces, retry constants, and TLS epoch arrays are generally defensible: they encode RFC or backend limits rather than runtime collection sizing. The concern is with runtime tables and queues that carry separately maintained lengths/counts.

Test and example use of `std::cout`/`std::cerr` is not a library-style issue. The `zquic/src` library itself does not appear to depend on STL containers.
