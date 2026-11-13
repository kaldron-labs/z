# Zrest

## Overview

Typed REST catalogs and client/server lifecycle over Zhttp, with bounded body/SSE handling and native OAuth client/server examples.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): generic request/response codecs, completion tokens, bounded admission, cancellation and SSE production.
- [Examples](./example/EGM.md): concrete OAuth discovery/authorization/token/revocation/keys and authenticated ping routes.

## Interface

The library exports `libZrest.la` and six public headers. Example executables `zrest` and `zrestd` provide their own schemas/catalogs and are not installed. [Library manifest](./src/Makefile.am), [example manifest](./example/Makefile.am).

## Invariants

`Limits` must be nonzero; body decoders enforce configured bounds. Completion tokens admit one completion on a live route, and shutdown drains work before finalization. Concrete authentication and token invariants belong to the example contracts. [Zrest.hh](./src/Zrest.hh), [ZrestServer.hh](./src/ZrestServer.hh).

## Side Effects

Library endpoints schedule HTTP requests, callbacks, streaming output and retirement. Examples add network listeners, browser subprocesses, token state, logging and optional daemon configuration. See child contracts.

## Verification

From a configured repository root, run `make -j8 && make -C zrest/test test`. Module `make test` additionally runs integration tests. Test/interoperability sources are outside this index. No builds, tests or examples were executed for this documentation change.
