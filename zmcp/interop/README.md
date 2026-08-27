# `zmcp` interoperability tests

This directory contains the explicitly invoked, pinned
`github.com/mark3labs/mcp-go` v1.0.0-beta.1 client/server interoperability
matrix.  It exercises both peer directions over stdio and Streamable HTTP for
the `2026-07-28` and `2025-11-25` revisions, including structured-only tool
results.  The Go fixture is not a runtime dependency of `libZmcp`.

```sh
make -C zmcp/interop -j8
make -C zmcp/interop test
```
