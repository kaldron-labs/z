# Zrest endpoints

`Zrest::Client<App, Catalog, Profiles>` and `Zrest::Server<App, Catalog, Decoder, Profiles>`
compose the existing REST request and response codecs with Zhttp pools and
server routes. Pass an explicit `Zhttp::Profiles<...>` selection through each
endpoint; the client pool carries that same selection. `MReqBuilder`, `MResParser`, `MReqParser` and `MResBuilder` remain
available for direct catalog use.

HTTP/3-capable endpoints bind the multiplexer UDP packet allocator before
startup, as described in [Zhttp's setup](../zhttp/README.md). The REST endpoint
does not allocate or resize socket receive packets itself.

## Calls and replies

`client.call<Req>(pool, requestObject, call)` returns `false` when the call was
not admitted. An admitted call receives exactly one terminal callback at
Zhttp logical-request completion, before retirement: `process(Reply<Res>)` for a
declared typed response, or `failed()`. A call may instead implement
`failed(const Zrest::Failure&)` for the local transport or decode reason.
HTTP status responses declared by `Req::Responses` remain typed replies. Pool
selection, connection limits, retry and transport settings come from Zhttp.
Zhttp retains the request to response association; Zrest does not allocate a
second response lookup table.
Retirement releases the pending-call slot after transport work drains; it does
not delay or repeat the application callback. An admitted call that fails before
HTTP submission reports failure directly without waiting for transport events.

The server application implements `request(Req *, const Req &,
const RequestContext &, ZmRef<Completion<Req, Server>>)`. The first argument
selects the request overload. Transport failures terminate the affected stream
without a response. Completed HTTP requests with invalid headers, authority,
scheme or body decoding receive a 400 response and disconnect before application
dispatch. `context.ok()` confirms validation of an admitted request. Application
validation failures in intact requests can use declared error responses.
A completion token exposes `live()`, `cancelled()` and
`complete(Reply<Res>)`. A response type must be declared in
`Req::Responses`. Off-shard completion posts one owning action to Rx; a late or
duplicate completion is rejected. The optional `cancelled(Req *, Token *,
ZuCSpan)` callback runs on Rx when a route is lost. Application work that runs
off shard copies the request fields it needs before leaving Rx.

For received JSON and raw entities, both fixed-length and streamed HTTP
framing are supported. Each entity is bounded and decoded once after HTTP
completion. A JSON entity contains exactly one top-level document, followed
only by JSON whitespace. URI entities remain fixed-length. JSON, URI and raw
transmission uses the existing fixed-body codecs and Zhttp output accounting.
A nonzero request descriptor `BodyLimit` overrides the REST parser's inherited
input limit and may raise or lower it; zero inherits that limit. Independently
configured HTTP cumulative-body and retention limits still apply and must permit
the intended request size.
A descriptor with `Borrowed = 1` retains parsed body, path, query and header
backing through the selected codec's lifetime. The default `Borrowed = 0`
requires the decoded object to own callback-visible variable data.

## Signing

Mark a JSON or URI signature member with `REST::Signature` in its ordinary
`ZfStruct` declaration. REST builders omit that member from normal query/body
serialization, including when signing is disabled. Rx loads the member normally
using the same declaration. The source object's signature is never cleared or
changed to suppress output. Raw representations are application-written bytes
and have no field filtering.

Enable `SignQuery` or `SignBody` and implement `signQuery(stream, object, bytes)`
or `signBody(stream, object, bytes)` to append the computed signature. The body
hook receives the serialized body excluding bytes written by `prefixBody`;
the query hook receives the serialized path and query. The application chooses
the algorithm, key, encoding and covered bytes. Verification belongs to the
application's receive policy: recompute over the agreed payload and compare
with the received signature before accepting the request or response. Parsing
the signature field alone does not verify it. Preserve original signed bytes
when the signing profile requires exact wire coverage; JSON/URI parsing may
mutate its input.

## SSE

An SSE response is declared with `Body = Zrest::BodyPolicy::SSE`. The server
response derives from `SSEBuilder<Res>`. On Rx, the request token's
`producer()` returns its one intrusive `SSEProducer`; the application may emit
events before completing `Reply<Res>` with that producer. `emit(body, id,
retry, terminal)` copies its callback-scoped arguments into bounded output,
returns `false` on refusal, and does not promise peer delivery. A terminal
event ends the HTTP body after it is written. The response route owns the
producer until local builder retirement.

The client declares a matching SSE response parser and implements
`receiveSSE(Zhttp::SSEEvent, Zrest::SSEMeta<Res>) -> bool`. Complete events
arrive incrementally on Rx; the event body owns its bytes, while its ID and
the selected response metadata are callback-scoped. Returning `false` fails
the call. A clean end calls `process()` at Zhttp request completion, before
retirement; failure calls `failed()` once. Ordinary declared error responses still use
`process(Reply<Res>)`. SSE feeds are finite under the HTTP total-body limit;
there is no automatic replay or reconnection.

## Bounds and lifecycle

Client and Server use a parent `ZmEngine` covering REST work and their child
HTTP transport. `start()`/`stop()` are main-thread synchronous operations;
`start(done)`/`stop(done)` deliver boolean results after the same complete
boundary. Repeated operations and opposite requests during a transition use
the engine's canonical callback handling. Completed stop permits start again
with the retained configuration, without another init. Failed startup rolls
back the HTTP child before exposing Stopped. Server stop includes session
cancellation continuations and SSE producer posts; client stop includes
admission posts and request retirement. The child transport's state alone is
not the REST endpoint's lifecycle result.

`ClientConfig` and `ServerConfig` wrap Zhttp configuration and each carry
`Limits`. `maxPending` separately bounds client calls awaiting Rx handoff
and registered calls, so up to twice that count can be accepted during a
handoff backlog. It also bounds admitted server work. A call accepted into
ingress can fail later if the registered-call limit is full.
For each SSE producer, `maxQueue` and `maxQueueBytes` bound serialized records
including off-shard ingress; `workBatch` limits producer drain turns.
An atomic count also bounds pending SSE emission posts by `maxQueue`, returning
false before posting when full and releasing admission when the callback starts.
This refusal leaves the producer usable; serialized queue/byte failure retains
its existing failure behavior.
`maxSSELineBytes` and `maxSSEEventBytes` apply on reception and emission.
At most `maxPending` live server work records can own producers. Zhttp's
configured retained-body and transport-output limits remain separate.

`stop()` drains the full REST endpoint and Zhttp transport; `stop(done)`
reports the same result after owner-shard retirement. After a successful start,
the application must complete `stop()` or receive its stop callback before
calling `final()`. Shutdown attempts every cleanup step and leaves the endpoint
Stopped even if its boolean result reports a cleanup failure. A later start
performs fresh startup; shutdown never restarts stopped resources. An endpoint
that was never started or whose start failed can be finalized directly, including
after partial initialization. `final()` releases the endpoint without
starting shutdown or waiting; parameterless lifecycle calls may block. The stop
callback can finalize the endpoint or notify its owning thread.
Application objects and the scheduler remain alive through callbacks and
outstanding external token operations.

The focused loopback test is `zrest/itest/zrestendpoint`; the OAuth transport
and interoperability matrix is described in [itest/README.md](itest/README.md).
