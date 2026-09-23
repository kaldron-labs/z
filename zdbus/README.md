# `zdbus`

`libZdbus` is Linux-only. `ZdbusClient` connects to a D-Bus bus, owns message
serials and pending calls, and receives returns, named errors, and signals.
`ZdbusServer` is a service-side bus client with exact-key method routes. Message
bodies use the direct `ZfDBUS` adapter; application routing headers belong to
the `Zdbus_` request/response/error/signal adapters, not the codec.

Include `ZdbusClient.hh` in client applications and `ZdbusServer.hh` in
service applications (both only when an application uses both roles).
`Zdbus.hh` contains their shared types and directly includes `ZfDBUS.hh`;
it does not include either role's API. Both roles build and parse structured
message bodies directly through the `ZfDBUS` save/load path.

Build `zdbus/example` and run `dbus-run-session --
./zdbus/example/zdbusgetid` to print a private session bus ID. It also runs
against an existing `DBUS_SESSION_BUS_ADDRESS`. The example uses the public
typed request/response API and `ZiFile` for output.

## Four-message example

The runnable example is
[`itest/zdbusclienttest.cc`](itest/zdbusclienttest.cc). Build `zdbus/util`
and `zdbus/itest`, then run `make -C zdbus/itest test` in the configured Linux
build tree. It launches a private bus and demonstrates all four wire kinds:

| Kind | Application action | Schema/body handling |
| --- | --- | --- |
| Method call | `ZdbusClient::call<FailReq>` or `send` with `NO_REPLY_EXPECTED` | `ReqBuilder` supplies routing headers and a `ZfDBUS` body; `ZdbusServer::route<EchoReq>` selects path/interface/member and parses it. |
| Method return | `ZdbusServer::send` constructs a fresh-serial return with the incoming `replySerial` | `ResParser` decodes the declared response schema through `ZfDBUS`. |
| Named error | `ZdbusServer::send` constructs an error with `errorName` and the incoming `replySerial` | `ErrParser` decodes the declared alternate error body; undeclared errors remain `ZdbusRemoteError` with retained name and body. |
| Signal | `ZdbusServer::send` emits a fresh-serial signal; `ZdbusClient::subscribe<ChangedSig>` receives it | `SigParser` decodes the signal body; `addMatch`/`removeMatch` manage the bus rule separately from the local subscription. |

The client allocates every outbound serial, including return, error, signal,
and no-reply call serials. A return/error's `replySerial` is the inbound call's
serial, not the serial allocated to that outbound message. The bus match rule
is installed before signal emission. The test also demonstrates bounded
multi-subscriber fan-out, unsubscribe, and stop-time cleanup.

[`interop/zdbusinteroptest.cc`](interop/zdbusinteroptest.cc) runs the same
message flow against an independently built `libdbus-1` peer, including a
nested struct, array, `a{sv}` map, variant, mismatched return signature, and
undeclared named error.
