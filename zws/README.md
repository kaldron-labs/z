# Zws: WebSocket clients, servers and codecs

Zws implements WebSocket handshakes, framing and message lifecycle over Zhttp
channels. It supports HTTP/1 Upgrade and HTTP/2/3 Extended CONNECT bindings,
with masking, fragmented messages, ping/pong and close handling. Include
`<zlib/Zws.hh>` and link `libZws` with its framework dependencies. HTTP session
and transport ownership remain in [Zhttp](../zhttp/README.md).

## Clients and servers

`Zws::Client<App, Profile>` and `Zws::Server<App, Profile>` bind an application
to a concrete HTTP profile. H1 profiles include `Zhttp::H1TCP` and
`Zhttp::H1TLS`; the extended binding uses multiplexed HTTP channels. Explicit
profile selection determines the transport, rather than a WebSocket frame
codec creating its own socket.

An HTTP/3 binding uses dedicated QUIC receive and transmit packet allocators
registered with the multiplexer by its hub. See [Zhttp's setup](../zhttp/README.md).

The [client](src/zws.cc) and [echo server](src/zwsd.cc) show complete H1
applications, including multiplexer setup, trust configuration, signals and
shutdown. Run them in separate terminals:

```sh
./zws/src/zwsd --address=127.0.0.1 --port=9001
./zws/src/zws --message=hello ws://127.0.0.1:9001/
```

For `wss`, supply `--cert` and `--key` to the server and the test CA with
`--ca` on the client. These command-line programs select H1; they are not a
CLI for exercising every library profile.

The client initializes with a `Zhttp::HubConfig`, profile configuration,
`Zhttp::Destination` and `Zws::Config`, then starts. Construct its `Link` with
the parsed URI and optional subprotocol, retain it with `ZmRef`, and call
`connect()`. The server's `accept()` callback validates the target and offered
subprotocols and writes its selection into `Zws::HandshakeString`.

## Message callbacks and streaming output

Application callbacks receive the link so state can remain per connection:

| Callback | Use |
| --- | --- |
| `connected(link, info)` | Opening handshake completed; start application exchange |
| `messageStart(link, opcode)` | Begin a text/binary message |
| `process(link, rx)` | Consume available message bytes, which may arrive in pieces |
| `messageEnd(link)` | Finish the message |
| `pong(link, payload)` | Observe a pong |
| `closed(link, code, reason)` | Observe peer close metadata |
| `error(link, failure)` | Handle protocol/lifecycle failure |
| `disconnected(link, peer)` | Retire application connection state |

`Zhttp::bodyEach(rx, callback)` consumes available spans; the command-line
client uses it to stream output without assembling a whole message. Define an
`App::LinkState` when you need per-link application data and access it with
`link.state()`. The echo server buffers a complete message there; that is its
application policy, not a requirement of the codec.

For example, from `connected()`:

```cpp
link.txStream([](auto &tx) {
  tx << "hello";
  tx.flush();
}, Zws::Opcode::Text);
```

Keep borrowed receive spans within their valid callback/buffer lifetime.
Thread handoff must carry owned data, not views into consumed input. Consult
[ZwsCodec.hh](src/ZwsCodec.hh) and [ZwsTx.hh](src/ZwsTx.hh) for lower-level
streaming and fragmentation interfaces.

## Protocol control and bounds

`link.ping(payload)` sends a ping and `link.close(code, reason)` starts the
close exchange. `Zws::Config` controls handshake/close deadlines and heartbeat
policy, including `pingInterval` and `pongTimeout`. Message/input limits bound
admission; choose them to match application payloads and consumption rates.
Use the current [protocol definitions](src/ZwsProtocol.hh) for all knobs and
failure codes. The selected handshake subprotocol is available from
`link.protocol()`.

H1 handshake logic is in [ZwsH1.hh](src/ZwsH1.hh); Extended CONNECT logic is
in [ZwsExtended.hh](src/ZwsExtended.hh). Generic handshake validation and URI
parsing are exposed by `ZwsHandshake.hh` and `ZwsURI.hh`. Use these separately
when integrating the codec with an existing HTTP application.

## Shutdown and interoperability

Closing a WebSocket and stopping its owning hub are asynchronous. Request
`stop(callback)`, wait for completion from a non-owner thread, release link
references, call `final()`, then stop the multiplexer. The client example shows
the timeout and close-deadline paths as well as normal shutdown.

[Protocol tests](test/ZwsProtocolTest.cc) cover wire behavior, and
[hub tests](test/ZwsH1HubTest.cc) exercise channel integration.
[Autobahn](interop/autobahn/README.md) supplies opt-in external testing;
`bench/` contains workload drivers.

## Examples and tests

Run `make -C zws test` from a configured build tree. The [test directory](test/)
contains executable API examples and boundary cases. See the top-level
[testing guide](docs/TESTING.md) for environment, diagnostics and port allocations,
and [dependencies](docs/DEPENDENCIES.md) for build prerequisites.
