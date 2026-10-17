# Zrest endpoints

`Zrest::Client<App, Catalog, Profiles>` and `Zrest::Server<App, Catalog, Decoder, Profiles>`
compose the existing REST request and response codecs with Zhttp pools and
server routes. Pass an explicit `Zhttp::Profiles<...>` selection through each
endpoint; the client pool carries that same selection. `MReqBuilder`, `MResParser`, `MReqParser` and `MResBuilder` remain
available for direct catalog use.

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
selects the request overload. Malformed transport, headers or body decoding
terminate the affected stream before application dispatch, without an error
response. `context.ok()` confirms validation of an admitted request. Application
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
reports the same result after owner-shard retirement. `final()` releases the
endpoint after stop, including a partially initialized endpoint. Application
objects and the scheduler remain alive through callbacks and outstanding
external token operations.

The focused loopback test is `zrest/itest/zrestendpoint`; the OAuth transport
and interoperability matrix is described in [itest/README.md](itest/README.md).
