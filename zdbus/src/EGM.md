# Zdbus library

## Overview

Linux D-Bus client/server transport, Unix address parsing, EXTERNAL authentication, message envelopes, and typed request/reply adapters. Typed body codecs come from ZfDBUS.

## Interface

- `ZdbusConnection` provides init/start/ready/send/stop/final with scheduler, Rx/Tx shards, address, budgets, and callbacks. `ZdbusClient` adds Hello, call/cancel/send, bus matches, and local subscriptions. [ZdbusConnection.hh:Connection] [ZdbusClient.hh:Client]
- `ZdbusServer` supplies exact-key raw/typed routes and delegates outbound calls, sends, matches, and subscriptions to its client. [ZdbusServer.hh:Server]
- `Address` parses/selects Unix addresses and converts socket addresses; session/system helpers read environment settings. `Auth` incrementally handles EXTERNAL authentication. [ZdbusAddress.hh:Address] [ZdbusAuth.hh:Auth]
- `frame`, `FrameInfo`, `HeaderView`, and `FrameResult` expose envelope validation and borrowed frame views. `HeadSpec`, `HeadPlan`, `planHead`, `writeHead`, and `message` assemble outgoing envelopes around ZfDBUS values. [ZdbusEnvelope.hh:frame] [ZdbusMessage.hh:message] [ZdbusMessage.cc:planHead]
- Request/response/error/signal builder/parser aliases adapt typed schemas. `ZdbusParsed`, `ZdbusRemoteError`, and `ZdbusTypedFault` distinguish parsed replies, retained remote errors, and local typed faults; `dispatchReply` selects declared response/error alternatives. [ZdbusAdapter.hh:BuildAdapter] [ZdbusAdapter.hh:ParseAdapter] [ZdbusCatalog.hh:dispatchReply]
- `libZdbus.la` and public headers build only on Linux. Name, serial, stop, and value helpers remain private headers. [Makefile.am:lib_LTLIBRARIES] [Makefile.am:noinst_HEADERS] [ZdbusLib.hh:__linux__]

## Invariants

- Initialization requires unused objects, a scheduler, nonzero Rx/Tx IDs, and a valid connection address. Stop completion precedes finalization, which precedes destruction. Client stop drains transport, subscriptions, pending calls, and queued timer work. [ZdbusConnection.cc:Connection::init] [ZdbusConnection.cc:Connection::final] [ZdbusClient.cc:Client::stop_1] [ZdbusClient.cc:Client::stop_4]
- Socket I/O and frame/signal/method delivery belong to Rx; builders, correlation, call/send/cancel completions, and client readiness belong to Tx. Subscription completion belongs to Rx; stop completion belongs to Tx. Failure callbacks can originate on either shard. [ZdbusConnection.cc:Connection::rxRead] [ZdbusClient.cc:Client::txCall_] [ZdbusClient.cc:Client::rxSubscribe_] [ZdbusClient.cc:Client::failed_]
- Frame/header/body spans borrow backing: retain the companion buffer reference. Delivered frames use `FrameInfo` views because the shared buffer range may advance; unfinished input is detached before receiving into retained backing. [ZdbusEnvelope.hh:FrameInfo] [ZdbusConnection.cc:splitFrame] [ZdbusConnection.cc:detachInput]
- Application calls require Ready after Hello. Serial allocation skips zero and pending IDs; nonzero call timeouts are scheduler absolute times. Pending entries are removed before reply/cancel/timeout callbacks. `CallResult` boolean success tests local error only, so remote Error replies need separate interpretation. [ZdbusClient.cc:Client::txHello_1] [ZdbusClient.cc:Client::txCall_] [ZdbusClient.cc:Client::txReply] [ZdbusSerial.hh:serialProbe] [Zdbus.hh:CallResult]
- Work, frame, queue, pending-call, and subscription budgets bound operation; zero work/client limits normalize to one. [ZdbusConnection.hh:CxnDefault] [ZdbusConnection.cc:Connection::init] [Zdbus.hh:ClientDefault] [ZdbusClient.cc:Client::init]
- Address selection accepts the first valid Unix candidate, validates decoded path/abstract capacity, and requires absolute non-NUL pathname addresses. Auth accepts OK with exactly 32 hexadecimal GUID characters and CRLF. [ZdbusAddress.cc:Address::parse] [ZdbusAddress.cc:Address::socketAddr] [ZdbusAuth.cc:Auth::reply_]
- Incoming envelopes enforce byte order, version, nonzero type/serial, limits, padding, known-header types, and required fields; nonzero UnixFDs are rejected. Incoming unknown nonzero message types can pass envelope validation, while outgoing planning permits only the four known types. [ZdbusEnvelope.cc:frame] [ZdbusEnvelope.cc:header] [ZdbusMessage.cc:planHead]
- Builders borrow initialized objects. Typed reply/error catalogs enforce unique signatures/names. Server routes require valid exact keys, capacity, no duplicates, and registration after init but before start. [ZdbusAdapter.hh:BuildAdapter::build] [ZdbusCatalog.hh:dispatchReply] [ZdbusServer.cc:Server::route]

## Side Effects

- Opens nonblocking close-on-exec Unix sockets, registers with ZiEventLoop, reads effective UID, performs authentication/Hello, and reads/writes frames. Sends use MSG_NOSIGNAL. Closing unregisters and closes the socket. [ZdbusConnection.cc:Connection::connect_] [ZdbusConnection.cc:Connection::connected_] [ZdbusConnection.cc:Connection::rxWrite] [ZdbusConnection.cc:Connection::close_]
- Allocates buffers and queue/table nodes, posts shard work, and manages timers. Send completion means local socket-write completion, not remote processing. Failure closes transport and fails queued work; shutdown completes pending calls with Disconnect. [ZdbusConnection.cc:Connection::txSend_] [ZdbusConnection.cc:Connection::rxFail_0] [ZdbusClient.cc:Client::stop_3]
- Local subscriptions and bus match rules are separate operations; addMatch/removeMatch send daemon calls. Server startup requests its service name and dispatches incoming methods to registered/fallback callbacks. [ZdbusClient.cc:Client::rxSubscribe_] [ZdbusClient.cc:Client::addMatch] [ZdbusClient.cc:Client::removeMatch] [ZdbusServer.cc:Server::connected_0] [ZdbusServer.cc:Server::method_]
- Message construction allocates and writes a sized buffer. Parsing updates caller objects and invokes header callbacks without rolling back earlier changes on error; typed dispatch invokes its callback synchronously. [ZdbusMessage.hh:message] [ZdbusAdapter.hh:ParseAdapter::load] [ZdbusCatalog.hh:parseTyped]

## Verification

On a configured Linux build, run `make -j8 && make -C zdbus/test test` from the repository root. Module `make -C zdbus test` also invokes integration and interoperability targets. Commands were checked against the manifests but not executed.
