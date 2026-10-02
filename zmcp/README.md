# `zmcp`

`zmcp` is the Z Framework MCP tool-provider and tool-caller library.  One set
of reflected application request and response types can be exposed over
newline-delimited stdio or Streamable HTTP without transport-specific handler
code.  The metadata model deliberately coincides with `zrest`, but neither
library depends on the other.

The library is a thin layer over `zhttp`, `ZfStruct`, and `ZfJSON`.  Its
MCP-specific protocol work is limited to the compile-time tools catalog,
pre-modern session awareness, and JSON-RPC wrappers; it does not define a
parallel request/response, reflection, JSON, or HTTP framework.

## Supported protocol

The server and client support:

- JSON-RPC requests, responses, errors, notifications, integer/string IDs,
  cancellation, progress, logging and ping;
- stateless MCP `2026-07-28`, including `server/discover`, per-request `_meta`,
  routing headers, `tools/list` and `tools/call`;
- the `2025-11-25` initialize/initialized lifecycle, optional stateful HTTP
  sessions, `logging/setLevel`, and sequential DELETE termination;
- structured-only tool results with an empty `content` array;
- fixed JSON and SSE responses selected at compile time per operation;
- stdio and Streamable HTTP in both client and server roles.  HTTP is layered
  on the transport-neutral `zhttp` builder/parser interface and works over H1
  TCP, H1 TLS, H2 TLS and H3 QUIC.

Clients probe `server/discover`, prefer `2026-07-28` when it is offered, and
otherwise perform the `2025-11-25` handshake.  Servers advertise only those
two revisions.  The complete static catalog is returned in typelist order and
is cacheable for `Zmcp::CatalogTTL`.  A Z client retains its first complete
catalog indefinitely; the application calls `discardCatalog()` after changing
peer/server identity when it needs a fresh `tools/list` result.

Prompts, resources, sampling, elicitation, task extensions, OAuth,
`2024-11-05` HTTP+SSE compatibility and child-process creation are deferred.
The embedding application launches and supervises a stdio peer.

## Typed tools

Requests and response bodies are ordinary application-owned `ZfStruct` types.
They may be movable values or intrusive `ZmObject` types shared with `zrest`;
clients pass the latter as `ZmRef<T>`, while handlers still receive `const T &`.
An operation supplies a stable MCP `ToolID`, descriptive metadata and all
possible response types:

```cpp
struct AddRequest { int64_t lhs = 0; int64_t rhs = 0; };
ZfStruct((AddRequest, JSON),
  (((lhs), (Ctor<0>, Required)),	Int64),
  (((rhs), (Ctor<1>, Required)),	Int64));

struct AddResult { int64_t value = 0; };
ZfStruct((AddResult, JSON),
  (((value), (Ctor<0>, Required)),	Int64));

struct AddOK : public Zmcp::Response { using Body = AddResult; };

struct Add : public Zmcp::Request {
  using Object = AddRequest;
  using OperationID = ZuStringT<"addNumbers">;
  using ToolID = ZuStringT<"add">;
  using Title = ZuStringT<"Add numbers">;
  using Description = ZuStringT<"Add two signed integers">;
  using Responses = ZuTypeList<AddOK>;
};

using Requests = ZuTypeList<Add>;
```

Set `enum { ResponseBody = Zmcp::BodyPolicy::SSE };` on an operation to select
SSE.  There is no runtime response-mode override.  Input and output schemas
are generated from the reflected types; `params.name` selects the compile-time
argument union and each active member uses its own `ZfJSON` handler.
Use `Zmcp::ToolAnnotations<ReadOnly, Destructive, Idempotent, OpenWorld>` as
the operation's `Annotations` type; its
read-only/idempotent declaration both emits the MCP annotation and controls
whether an indeterminate HTTP attempt may be repeated.  For example:

```cpp
using Annotations = Zmcp::ToolAnnotations<false, true, true>;
static Annotations annotations() { return {}; }
```

Annotate a primitive reflected field with `MCP::Header<"Region">` when its
schema must contain `"x-mcp-header":"Region"`.  Modern HTTP clients derive
the corresponding `Mcp-Param-Region` header from the selected typed request;
stdio and pre-modern HTTP do not emit it.  This is an MCP enrichment of the
ordinary field metadata, not a second request or reflection model.

A server application handles both transports with the same signature:

```cpp
template <typename Req, typename Completion>
void tool(Req *, const AddRequest &request, const auto &headers,
    const Zmcp::Context &context, Completion completion) {
  completion->complete(Zmcp::ToolReply<AddOK>{
    AddResult{request.lhs + request.rhs}});
}
```

Completion is synchronous by default.  The application may retain the handle
and complete asynchronously, emit progress/log records where supported, or
handle `cancelled(Req *, Completion *, ZuCSpan reason)`.  It must preserve
response order for a stream when it retains completion handles; `zmcp` does not
reorder application completions.  Handles are invalidated during disconnect
and shutdown and do not retain their stream/session owner.

`Context` contains borrowed transport-session, legacy MCP-session and request-
stream application pointers.  Its pointers are valid only during the handler
call and must not be retained.  Applications may provide matching
`open(TransportTag, ...)`, `open(SessionTag, ...)`, `open(StreamTag, ...)` and
`close(tag, object)` overloads; context objects derive from `ZuObject`.

Applications declare HTTP headers at compile time with
`using Headers = ZhttpHeaders(...)`.  The handler receives each declared
header's borrowed value and occurrence count.  Authentication and
authorization remain application-owned.  A client emits declared headers via
`header<Key>(callback)`.

## Transport configuration

`Zmcp::Server<Impl, Requests>` has mutually exclusive `init` overloads:

- HTTP: `init(Zhttp::HubConfig, Zmcp::ServerConfig, Impl *)`;
- stdio: `init(ZiMultiplex *, Zmcp::StdioConfig, Impl *)`.

The client types are `Zmcp::HTTPClient<Impl, Requests>` and
`Zmcp::Client<Impl, Requests>` for stdio.  HTTP configuration inherits the
normal `zhttp` TCP/TLS/H2/QUIC, timeout and pooling controls.  `endpoint()`
defaults to `/mcp`; `legacySessions(true)` is the default.  A nonzero
`legacyLifetime()` enables automatic legacy-session expiry.  Setting
`legacySessions(false)` on both sides selects an explicitly stateless legacy
deployment: no MCP session ID or session context is created, and each request
uses the canonical protocol-version routing hint.

`StdioConfig` owns its input and output handles.  Server defaults are process
stdin/stdout; callers retaining an original handle must pass a duplicate.  Set
`rxThread()` and `txThread()` to two distinct named isolated scheduler slots,
also distinct from the multiplex network Rx/Tx slots.  Those workers perform
portable blocking `ZiFile` reads/writes, so Windows console, pipe and file
handles require no overlapped-I/O or `ZiEventLoop` adapter.  Stdio and HTTP
serving cannot be enabled simultaneously on one server.

Rx teardown closes the owned input and interrupts only that blocking worker:
`zmcp` uses a non-restarting signal on POSIX and `CancelSynchronousIo()` on
Windows.  This dependent-specific mechanism does not extend `ZmScheduler`.
Tx teardown closes the owned output and signals its queue normally.

Only MCP frames are written to stdio stdout.  The application owns `ZiLog`
initialization and should route diagnostics to stderr or another sink.

## Threading and ownership

HTTP parsing remains on the `zhttp` Rx shard, HTTP production on its Tx shard,
and server registry/session/application dispatch on the configured owner
shard.  Stdio has isolated blocking Rx and Tx workers; framing and protocol
dispatch run on the peer owner shard.  Public client calls post bounded actions
to that owner, so application-owned variable data must be moved into the call
rather than shared by adding locks.

All wire buffers are pooled and moved by reference.  Queue counts/bytes,
message size, SSE event size, pending calls, sessions and work per scheduler
turn are bounded by `Zmcp::Limits`.  Raising `maxJSONBytes` also raises the
worst-case latency of one indivisible JSON turn, including `tools/list`.

## Security and failure behavior

HTTP servers reject an absent `Origin` by default.  Set `absentOrigin(true)`
only for deployments that intentionally allow it, and implement `origin()` to
accept the deployment's exact allowed origins.  Legacy session IDs contain
128 bits from the TLS CSPRNG by default.

Inbound media and routing headers are advisory: usable bounded JSON is not
rejected merely because `Content-Type`, `Accept` or an MCP routing hint is
absent or noncanonical.  Invalid JSON and corrupt/over-limit framing silently
close the affected transport scope without response amplification.  Unknown
methods and tools use the ordinary protocol paths.  Applications remain
responsible for semantic argument validation and authorization.

HTTP retries remain bounded by the ordinary `zhttp` configuration.  An
attempt known not to have been processed may be retried regardless of tool
semantics.  Redirect or fallback after possible processing requires the
operation's trusted compile-time annotation to be read-only or idempotent;
indeterminate non-idempotent tool calls are never replayed automatically.

## Examples and tests

`example/zmcpd` and `example/zmcp` demonstrate HTTP (default), inherited stdio
(`--stdio`), fixed/SSE calls and declared-header bearer authentication.  Stdio
process plumbing is intentionally left to the supervisor.

```sh
make -C zmcp/example -j8
./zmcp/example/zmcpd --port=8080
./zmcp/example/zmcp --port=8080 --lhs=20 --rhs=22

make -C zmcp/test test
make -C zmcp/itest test
make -C zmcp/interop test
```

The interoperability suite pins `github.com/mark3labs/mcp-go`
v1.0.0-beta.1 and exercises both peer directions, both transports and both
supported revisions with structured-only results.
