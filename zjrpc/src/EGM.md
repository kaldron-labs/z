# Zjrpc JSON-RPC transport and dispatch

## Overview

Typed JSON-RPC 2.0 envelopes and catalogs, bounded request/batch dispatch, completion and pending-call ownership, and stdio/HTTP/WebSocket client/server adapters.

## Interface

- [Zjrpc.hh](./Zjrpc.hh): request/response metadata, owning IDs, errors/replies, parameter decoding, serializers, envelope classification, parsed input and limits. IDs distinguish absent, null, int64 and owning strings.
- [ZjrpcDispatch.hh](./ZjrpcDispatch.hh): unique-method contracts, typed server messages, dispatch policy hooks, bounded batch aggregation and route cancellation. [ZjrpcCompletion.hh](./ZjrpcCompletion.hh): typed completion tokens, slots and sets.
- [ZjrpcPending.hh](./ZjrpcPending.hh): pending correlation, typed calls/notifications, batch construction and aggregate replies. [ZjrpcClient.hh](./ZjrpcClient.hh) and [ZjrpcServer.hh](./ZjrpcServer.hh): stdio, HTTP and WS endpoint lifecycles; WS connections support bidirectional calls.
- [ZjrpcHTTP.hh](./ZjrpcHTTP.hh): POST/fixed JSON and SSE builders/parsers, typed application headers, optional HTTP peer history and configurations. [ZjrpcIO.hh](./ZjrpcIO.hh) supplies bounded pooled input/output; [ZjrpcInbound.hh](./ZjrpcInbound.hh) tracks live/recent request IDs.
- [ZjrpcStdio.hh](./ZjrpcStdio.hh): newline framing and isolated blocking I/O workers. [ZjrpcWS.hh](./ZjrpcWS.hh): bounded text-message assembly and Rx/Tx handoff over Zws.
- [Makefile.am](./Makefile.am): exports libZjrpc.la and twelve headers; the compiled source is ZjrpcLib.cc, with WS/HTTP/QUIC/TLS/TCP and framework dependencies.

## Invariants

- Envelope decode requires version 2.0, valid IDs and consistent request/response members. Methods require object/array params; responses require exactly result or error. Payload nodes borrow parsed storage. Limits are nonzero and bound JSON/line/event bytes, pending work, queues and scheduling batches. [Zjrpc.hh](./Zjrpc.hh)
- Dispatcher reserves root and batch-member capacity before admission, requires unique catalog methods and rejects duplicate live/recent IDs. Borrowed terminal replies retain input through asynchronous output; batch output is ordered by completion/append and bounded including separators. [ZjrpcDispatch.hh](./ZjrpcDispatch.hh), [ZjrpcInbound.hh](./ZjrpcInbound.hh)
- Completion atomically admits one attempt, verifies declared response type and token/generation, and invalidates ownership before callback. The route scheduler must survive off-shard token calls. Pending and inbound hashes require owner-serialized access; pending entries are removed before callbacks. [ZjrpcCompletion.hh](./ZjrpcCompletion.hh), [ZjrpcPending.hh](./ZjrpcPending.hh)
- Call admission registers pending IDs before sending; accepted calls may subsequently fail. Batch ingress counts all members and registration/cleanup runs in bounded turns. Shutdown seals ingress and drains pending/dispatch work before endpoint storage release. [ZjrpcPending.hh](./ZjrpcPending.hh), [ZjrpcClient.hh](./ZjrpcClient.hh), [ZjrpcServer.hh](./ZjrpcServer.hh)
- HTTP queued requests borrow client/endpoint lifetime. HTTP is request/reply without an unsolicited server-call channel; optional peers own shared inbound history. SSE retains semantic messages and admits no further event after terminal enqueue. [ZjrpcHTTP.hh](./ZjrpcHTTP.hh), [ZjrpcServer.hh](./ZjrpcServer.hh)
- Stdio requires distinct valid owner/Rx/Tx threads, isolated blocking workers separate from multiplexer I/O threads, and newline-terminated bounded records. Natural EOF still requires explicit stop before finalization/restart. WS accepts text only and retains count/byte credits until Rx receives handoff acknowledgement. [ZjrpcStdio.hh](./ZjrpcStdio.hh), [ZjrpcWS.hh](./ZjrpcWS.hh)

## Side Effects

- Serialization writes caller/transport storage; pooled input adopts or copies backing before mutation. Dispatch allocates work/tokens, invokes application methods and schedules bounded batch/cancellation continuations. Unknown request methods generate MethodNotFound; notifications suppress error replies. [ZjrpcIO.hh](./ZjrpcIO.hh), [ZjrpcDispatch.hh](./ZjrpcDispatch.hh)
- HTTP adapters submit POSTs, write fixed JSON/SSE, invoke typed header providers and retire reply routes. WS callbacks hand messages across shards with retained links and close on unsupported or overloaded input; Zws owns wire framing/masking/control handling. [ZjrpcHTTP.hh](./ZjrpcHTTP.hh), [ZjrpcWS.hh](./ZjrpcWS.hh)
- Stdio duplicates configured handles, runs blocking read/write workers, queues owner delivery and closes owned handles after both workers/drains finish. Clean EOF flushes accepted output; stopping abandons pending output. [ZjrpcStdio.hh](./ZjrpcStdio.hh)
- POSIX worker registration installs a process SIGURG handler and unblocks it on the worker; interruption sends SIGURG. Windows duplicates thread handles and uses CancelSynchronousIo/CloseHandle. [ZjrpcLib.cc](./ZjrpcLib.cc), Private::stdioThread and Private::interruptStdio.
- Endpoint stop coordinates transport completion and shard barriers, invalidates callbacks/tokens and releases retained link/channel state. [ZjrpcClient.hh](./ZjrpcClient.hh), [ZjrpcServer.hh](./ZjrpcServer.hh)

## Verification

From a configured repository root, run `make -j8 && make -C zjrpc/test test`. Module `make test` also invokes integration tests. No builds, tests or transports were executed for this documentation change.
