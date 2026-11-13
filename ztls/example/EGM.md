# Ztls examples

## Overview

Non-installed TLS client/server programs demonstrate allocator setup, isolated I/O/TLS threads, and repeated connections.

## Interface

- [ztlsclient.cc](./ztlsclient.cc): `ztlsclient SERVER PORT [CA] [REPEAT]`; a sole numeric third argument means repeat count. ZTLS_PAYLOAD adds patterned request bytes.
- [ztlsserver.cc](./ztlsserver.cc): `ztlsserver SERVER PORT CERT KEY [REPEAT]`; nonzero ZTLS_PAYLOAD selects that many X bytes as response body, otherwise HTML.
- [Makefile.am](./Makefile.am) builds both programs.

## Invariants

Both programs use http/1.1 ALPN and four isolated I/O Rx/Tx and TLS Rx/Tx threads. Port zero is rejected; repeat zero becomes one. The client requires PSK resumption after its first round and disabled early data. Its response parsing supports limited header/body forms, including the first chunk of a chunked response. The server sends a complete response per nonempty receive iteration without parsing HTTP requests; completion counts disconnections. See [client](./ztlsclient.cc) and [server](./ztlsserver.cc).

## Side Effects

Both programs open sockets, start worker threads, and log. The client reconnects to its round target and overwrites index.hdr and index.html in the current directory each round. The server reads its certificate/private key and listens until completion. See [client](./ztlsclient.cc) and [server](./ztlsserver.cc).

## Verification

From a configured repository root, run `make -j8 && make -C ztls/test test`. Manual examples require suitable endpoints/certificates and create the client output files described above. Commands were not run for this documentation change.
