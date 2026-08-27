# `zmcp` unit tests

The programs in this directory are isolated TAP tests:

- `ZmcpJSONTest` exercises owned JSON-RPC identifiers and codecs.
- `ZmcpPeerTest` exercises transport-neutral peer defaults and lifecycle.
- `ZmcpSchemaTest` exercises generated JSON Schema.
- `ZmcpToolTest` exercises static tool catalogs and dispatch.
- `ZmcpZrestTest` parses the same intrusive request type through both contracts,
  invokes one handler, and checks declared-header, response, and error parity
  without a library dependency between `zrest` and `zmcp`.

Build and run them with:

```sh
make -C zmcp/src -j8
make -C zmcp/test -j8
make -C zmcp/test test
```
