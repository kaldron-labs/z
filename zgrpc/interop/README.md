# Zgrpc external interoperability

The Go client/server peer uses canonical grpc-go and protobuf generated from
`zgrpc.proto`. It is isolated from the production library and default build:

```
make -C zgrpc/interop go-peer
make -C zgrpc/interop peer-test
make -C zgrpc/interop peer-race
make -C zgrpc/interop go-test
make -C zgrpc/interop test
```

The peers require Go 1.26 or later. Generated sources are checked in. Regeneration
requires protoc, protoc-gen-go v1.36.12 and protoc-gen-go-grpc v1.6.2 on PATH:
`make -C zgrpc/interop/go generate`. Ordinary peer builds need no generators.

`peer-test` tests the fixture over loopback TLS, including unary, server streaming,
client streaming, duplex before request half-close, empty messages/streams,
trailers-only errors, repeated binary metadata, cancellation, deadlines and
concurrent calls. Streaming payloads exceed ordinary HTTP/2 receive windows.
The compression case sends gzip to the canonical peer and verifies its decoded
echo, proving the fixture emits valid compressed gRPC. It tests the Go fixture
itself. `make -C zgrpc/interop test` separately runs the
native `Zgrpc::Client` against the canonical Go TLS server. This native-client
matrix covers all four RPC kinds, large and empty messages/streams, duplex
responses before request half-close, trailers-only errors, repeated binary
metadata, cancellation, deadline expiry and 16 concurrent calls. Both the normal
run and full Valgrind leak checking pass, with zero errors and no definite or
indirect leaks. The fixture declares explicit protobuf zero defaults in its Zpb
field facets to match the proto3 schema.

The native-client harness also requires Python 3 and openssl. It creates an
ephemeral localhost certificate, waits for the Go server readiness signal, runs
the native TAP program with a bounded timeout and tears the server down. For
Valgrind, run from the repository root:

```
python3 zgrpc/interop/run-go.py --peer=zgrpc/interop/go/zgrpcpeer \
  --client=zgrpc/interop/zgrpcgoclient --valgrind
```

The same target runs the Go-client matrix against the complete native server
endpoint (14 H2 cases and 13 H3 cases) and checks native shutdown status. Both directions pass normally
and under Valgrind on H2 and H3, with zero errors and no definite or indirect leaks. Add
`--server=zgrpc/interop/zgrpcgoserver` to the direct Python command above to run
and memory-check both directions. Each transport harness emits one nested TAP stream.

Run a server with an ordinary PEM certificate/key covering localhost:

```
./zgrpc/interop/go/zgrpcpeer --mode=server --listen=127.0.0.1:0 --cert=cert.pem --key=key.pem
```

The server prints `READY host:port` after binding. Pass that address to the client:

```
./zgrpc/interop/go/zgrpcpeer --mode=client --target=127.0.0.1:PORT --ca=cert.pem --server-name=localhost
```

Client output is TAP and failure returns a nonzero exit status. Use `--case=NAME`
to select a case, or `--case=all` (default). Each client case is bounded by
`--timeout` (default 30s); server shutdown handles SIGINT and SIGTERM.

The native-server runs set the Go client's `--native` switch. Its additional
compression case requires prompt rejection of gzip and then a successful plain
RPC. Both Go self-tests instead require a successful compressed echo. This
qualifies unsupported request compression on native H2 and H3 without relying
on fixture-only tests or weakening the identity-only production policy.

`go-test` runs both Go transport matrices without the .NET fixture. The combined
`test` target also runs the canonical .NET H3 matrix.

The canonical grpc-go peer covers HTTP/2. A second peer, `go/zgrpch3peer`, uses
Connect 1.21.1 explicitly in gRPC mode over quic-go 0.64.0 HTTP/3. The test target
runs both native endpoint roles against this independent H3 implementation,
including all four kinds, empty messages/streams, duplex, status-only errors,
binary metadata, cancellation, deadlines and concurrency. Neither Connect nor
quic-go is a production dependency, and the H3 run has no H2 fallback. Its loopback
self-test runs through `peer-test`; `peer-race` runs both Go fixtures with the
race detector.

Run only H3 from the repository root:

```
python3 zgrpc/interop/run-go.py --h3 --peer=zgrpc/interop/go/zgrpch3peer \
  --client=zgrpc/interop/zgrpcgoclient --server=zgrpc/interop/zgrpcgoserver
```

Go supports HTTP/3 through quic-go; canonical grpc-go still requires HTTP/2,
including its ServeHTTP handler. References:
[grpc-go documentation](https://pkg.go.dev/google.golang.org/grpc#Server.ServeHTTP),
[HTTP/3 handling request](https://github.com/grpc/grpc-go/issues/9103),
[quic-go HTTP/3](https://quic-go.net/docs/http3/).

The additional `dotnet` peer uses canonical grpc-dotnet over .NET HTTP/3 and
MsQuic in both endpoint roles. It is part of the explicit interop `test` target,
with dedicated entry points:

```
make -C zgrpc/interop dotnet-peer-test
make -C zgrpc/interop dotnet-test
python3 zgrpc/interop/run-dotnet.py \
  --peer=zgrpc/interop/dotnet/bin/Release/net10.0/Peer \
  --client=zgrpc/interop/zgrpcgoclient --server=zgrpc/interop/zgrpcgoserver --valgrind
```

Install .NET SDK 10 and the platform's MsQuic runtime before invoking those
targets; see [Microsoft's HTTP/3 requirements](https://learn.microsoft.com/en-us/aspnet/core/fundamentals/servers/kestrel/http3?view=aspnetcore-10.0).
NuGet dependencies are pinned in `Peer.csproj` and `packages.lock.toml`. The
peer make target renders `obj/packages.lock.json` for NuGet and restores in locked
mode. They are confined to these explicit peer targets.
The fixture generates protobuf bindings from the shared `zgrpc.proto`.

The peer self-test has 13 cases. Actual native qualification runs both roles
and covers all four RPC forms, large and empty messages/streams, duplex before
request end, trailers-only errors, unknown methods, repeated binary metadata,
cancellation, deadlines and 16 concurrent calls. The .NET client requires HTTP/3
and checks each response's HTTP version; its server listens only on HTTP/3.
The native fixture uses `ZGRPC_GO_H3=1` to require H3, with no H2 fallback.

The server fixture reads its `x-test-bin` echo values from HTTP headers and
decodes comma-separated Base64 values with .NET's `Convert`, then emits them
through canonical gRPC metadata. This isolates grpc-dotnet's request metadata
accessor, which flattens repeated binary values before Base64 decoding; see
[HttpContextServerCallContext](https://github.com/grpc/grpc-dotnet/blob/master/src/Grpc.AspNetCore.Server/Internal/HttpContextServerCallContext.cs).
Framing, protobuf serialization, status, streaming, deadlines and HTTP/3 use the
canonical runtime. This fixture accommodation is separate from native metadata
validation.

Verified functional runs use Linux x86_64, .NET SDK 10.0.112/runtime 10.0.12,
grpc-dotnet/Grpc.Tools 2.84.0, Google.Protobuf 3.36.2 and MsQuic 2.6.3.
Native code uses Clang 21.1.8, GNU C++2b debug. Both native roles pass Valgrind
Memcheck with zero reported errors and zero definitely, indirectly or possibly
lost bytes. The runner sets `ZGRPC_PEER_TIMEOUT=60` for instrumented native
transfers (normally 10 seconds); the explicit 30 ms deadline case is unchanged.
QUIC traces of the original large-transfer deadline failures showed continued
forward progress until expiry. Other platforms and toolchains remain unverified.
