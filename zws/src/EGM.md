# Zws WebSocket library and programs

## Overview

WebSocket codecs, HTTP/1 upgrade and HTTP/2–3 Extended CONNECT adapters, profile-specific client/server hubs, URI handling, and command-line client/echo server.

## Interface

- [Zws.hh](./Zws.hh) includes the public library surface. [ZwsCodec.hh](./ZwsCodec.hh), `Codec`, supplies CRTP receive/message/control callbacks, transmit streams, close/ping and lifecycle operations. [ZwsProtocol.hh](./ZwsProtocol.hh), `Config`, defaults message/input limits to 1 GiB, handshake timeout to 10, close/pong timeout to 5, and ping interval to zero.
- [ZwsH1.hh](./ZwsH1.hh) and [ZwsExtended.hh](./ZwsExtended.hh) provide HTTP/1 GET/101 and Extended CONNECT/200 builders/parsers; rejection responses use 400. [ZwsHub.hh](./ZwsHub.hh), `Binding`, `Client`, and `Server`, select transport profiles and bind codecs to retained HTTP channels.
- [ZwsTx.hh](./ZwsTx.hh), `TxLayer`, supports streamed values, owned buffers, flush and failure. [ZwsURI.hh](./ZwsURI.hh), `URI`, parses and formats scheme/host/port/target. [ZwsHandshake.hh](./ZwsHandshake.hh) exposes nonce, accept and token/subprotocol helpers.
- [zws.cc](./zws.cc), `Options`: `zws URI` accepts CA, message, protocol, message-count, timeout, handshake/close/ping/pong settings, required-pong and verbose flags. Defaults send text `ping` once, wait for one received message, and time out after 15 seconds. `--messages` changes receive completion, not transmission count.
- [zwsd.cc](./zwsd.cc), `Options`: echo server accepts address/port, certificate/key, target, protocol, limits and timing flags; defaults bind `0.0.0.0:9001` with target `/`. These programs select HTTP/1 TCP or TLS. [Makefile.am](./Makefile.am) installs both and exports `libZws.la`.

## Invariants

- `Codec::process` bounds combined lower/wire/application input. `frame_` enforces peer masking by role, final control frames at most 125 bytes, continuation sequencing and aggregate message limits. Negative application processing results terminate reception. [ZwsCodec.hh](./ZwsCodec.hh)
- `HeaderParser::process` rejects reserved bits, unsupported opcodes and noncanonical lengths. `TxLayer` emits continuation frames after the first, forbids writes after finalization, and limits control frames. [ZwsProtocol.hh](./ZwsProtocol.hh), [ZwsTx.hh](./ZwsTx.hh)
- H1 parsers require completed non-HTTP/1.0 bodyless upgrade handshakes, correct tokens/key/accept/version, and matching selected subprotocols; tracked duplicates invalidate. Extended parsers require streamed CONNECT/2xx semantics and negotiated protocol. Server establishment additionally requires application acceptance and any selected subprotocol to be offered. [ZwsH1.hh](./ZwsH1.hh), [ZwsExtended.hh](./ZwsExtended.hh), [ZwsHub.hh](./ZwsHub.hh)
- `URI::parse` accepts ws/wss, validates IPv6/port syntax, rejects userinfo/fragments/control/space characters, and commits output only on success. `subprotocol` matching is case-sensitive and validates every offered token. [ZwsURI.cc](./ZwsURI.cc), [ZwsHandshake.cc](./ZwsHandshake.cc)
- Client `main` requires one URI and nonzero message count. Server `main` requires port 1–65535, slash-prefixed target, and paired certificate/key. Server `App::accept` requires exact target and configured protocol membership. Client required-pong completion waits for an observed pong; the program does not explicitly send a solicited ping. [zws.cc](./zws.cc), [zwsd.cc](./zwsd.cc)

## Side Effects

- `Codec::data_` unmasks server input in place; ping receives automatic pong. Close/error paths clear queues, stop timers, notify callbacks, and end/reset streams as applicable. `TxLayer` destruction flushes; masking XOR-mutates payloads with random keys. [ZwsCodec.hh](./ZwsCodec.hh), [ZwsTx.hh](./ZwsTx.hh)
- `LinkState::establish_` installs stream callbacks; `down_` retains the link through Rx→Tx→Rx cleanup before channel release. Hub initialization initializes RNG and enables Extended CONNECT for H2/H3. Client handshake storage clears authorization on destruction. [ZwsHub.hh](./ZwsHub.hh), [ZwsH1.hh](./ZwsH1.hh)
- `nonce` reads random bytes; `accept` computes Base64 SHA1 of key plus GUID. URI parsing normalizes scheme/host and supplies a slash before a bare query. [ZwsHandshake.cc](./ZwsHandshake.cc), [ZwsURI.cc](./ZwsURI.cc)
- Program `run` functions start scheduler threads, install SIGINT handlers and connect/listen. Client prints received payloads to stdout; server buffers complete messages and echoes their original opcode. Both log to stderr and await asynchronous stop before finalization. [zws.cc](./zws.cc), [zwsd.cc](./zwsd.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zws/test test`. Module `make test` additionally runs interoperability tests. No programs or tests were executed for this documentation change.
