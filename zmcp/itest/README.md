# `zmcp` integration tests

This directory contains TAP programs for end-to-end stdio and Streamable HTTP
testing.  They require the normal repository network and TLS test prerequisites.

```sh
make -C zmcp/itest -j8
make -C zmcp/itest test
./zmcp/itest/zmcphttptest
./zmcp/itest/zmcpstdiotest
```

`zmcphttptest` exercises fixed and streamed MCP calls over H1 TCP, H1 TLS, H2
TLS, and H3 QUIC, plus the legacy HTTP lifecycle.  It requires OpenSSL and
available local TCP/UDP ports.

`zmcpstdiotest` exercises dedicated blocking Rx/Tx workers, stdio server
dispatch and cancellation, typed client correlation, framing, and teardown
over owned pipe handles.
