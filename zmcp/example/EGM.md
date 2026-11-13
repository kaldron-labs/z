# Zmcp arithmetic examples

## Overview

Matching MCP client/server examples over HTTP, WebSocket or inherited stdio, using a typed arithmetic tool catalog.

## Interface

- [zmcpexample.hh](./zmcpexample.hh), ExampleCatalog: tools add/add_stream have operation IDs addNumbers/addNumbersStream, required int64 lhs/rhs, status-200 value response and status-401 unauthorized response. AddStream selects SSE response policy.
- [zmcp.cc](./zmcp.cc), Options: client accepts --stdio, --ws, --host (127.0.0.1), --port/-p (8080), --token, --lhs (20), --rhs (22), --stream and help.
- [zmcpd.cc](./zmcpd.cc), Options: server accepts stdio/WS selection, port, token and help. Network listeners bind loopback; HTTP uses /mcp, and WS requires /mcp with offered mcp subprotocol.
- [Makefile.am](./Makefile.am) builds non-installed zmcp and zmcpd with the shared catalog header.

## Invariants

- Both main functions reject positional arguments, simultaneous stdio/WS selection and invalid ports; client additionally requires host. Stdio mode relies on external duplex wiring of inherited streams. [zmcp.cc](./zmcp.cc), [zmcpd.cc](./zmcpd.cc)
- App::tool checks exactly one Bearer header when transport context exists and a server token is configured; a failed check returns unauthorized. Successful tools directly add lhs/rhs without explicit overflow validation. [zmcpd.cc](./zmcpd.cc)
- App::origin uses textual prefixes for loopback/localhost HTTP(S) origins, not exact parsed-host comparison; HTTP configuration permits absent Origin. [zmcpd.cc](./zmcpd.cc)
- Client run waits for readiness, performs one selected tool call and requires response/stop success. After successful startup, server exit reflects stop completion; failure/closure callbacks wake its wait. [zmcp.cc](./zmcp.cc), [zmcpd.cc](./zmcpd.cc)

## Side Effects

- Programs create four scheduler threads, configure stderr logging, and open the selected transport. Client can send a Bearer header and logs the result. HTTP is unsecured H1TCP; WS uses a ws URI. [zmcp.cc](./zmcp.cc), run and main.
- Server main installs signal trapping; run waits for interruption/closure/failure, then stops/finalizes transport and logging. Cancellation callback is empty. [zmcpd.cc](./zmcpd.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zmcp/test test`. Default build includes these examples. No programs, listeners or tests were executed for this documentation change.
