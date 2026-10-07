# `zmcp` integration tests

This directory contains TAP programs for end-to-end stdio, Streamable HTTP and
WebSocket testing. They require the normal repository network and TLS test prerequisites.

```sh
make -C zmcp/itest -j8
make -C zmcp/itest test
./zmcp/itest/zmcphttptest
./zmcp/itest/zmcpstdiotest
./zmcp/itest/zmcpwstest
```

`zmcphttptest` exercises fixed and streamed MCP calls over H1 TCP, H1 TLS, H2
TLS, and H3 QUIC, plus the legacy HTTP lifecycle.  It requires OpenSSL and
available local TCP/UDP ports.

`zmcpstdiotest` exercises dedicated blocking Rx/Tx workers, stdio server
dispatch and cancellation, typed client correlation, framing, and teardown
over owned pipe handles.

`zmcpwstest` exercises MCP over native `Zws` H1 TCP/TLS and H2/H3 extended
CONNECT profiles, using ports 21020–21023. It covers discovery, catalog loading,
ping, typed tool results, progress, logging, cancellation, deferred completion,
aggregate responses, notification-only batches and interrupted-batch teardown.
A legacy-only JSON-RPC fixture exercises discovery fallback, catalog loading and
ordinary/batched legacy tool replies on all four profiles, including suppression
of modern request metadata. Rejection cases cover binary input, malformed JSON,
null MCP request IDs and messages exceeding the configured input limit.
The new binding sources are verified at the final
implementation milestone in `plan.md`, using the current build configuration.
