# Zhttp command-line utilities

## Overview

Non-installed HTTP workload client and static-file/JSON-echo server, plus a reusable server utility library.

## Interface

- [zhttp.cc](./zhttp.cc): URL-based GET client or generated JSON PUT workload, with output/discard, concurrency, retry, CA, H2/H3, migration and diagnostic options.
- [zhttpd.hh](./zhttpd.hh), [zhttpd.cc](./zhttpd.cc): root-based server options, StaticPlanner, response plans and Application lifecycle.
- [zhttpput.hh](./zhttpput.hh): typed JSON id/text record used by client and server echo mode. [runtime.hh](./runtime.hh): semaphore-based process completion.
- [Makefile.am](./Makefile.am) builds zhttp, zhttpd and libzhttputil.la; all are non-installed utility targets.

## Invariants

- Client requests own generated JSON records; PUT does not upload a supplied file. Output is PATH for one request or PATH.ID for multiple requests. Responses reset output state per attempt; redirects skip output. Stall/quiet timeout options are parsed but unused by this client implementation. [zhttp.cc](./zhttp.cc)
- StaticPlanner validates target, handles redirects, checks Basic auth, restricts GET/HEAD, then normalizes/traverses paths. Redirects precede authentication. URI decoding rejects NUL/malformed escapes and traversal above root; file traversal is descriptor-relative with NoFollow. Directory listing enumerates by reconstructed pathname and is not bounded by its initial scratch capacity. [zhttpd.hh](./zhttpd.hh)
- PUT bypasses StaticPlanner, including its authentication/root policy, and returns a JSON echo after record/body-account validation; it does not write an uploaded file. The configured retained-body limit is enforced by the server layer, while utility parsing appends delivered body spans. [zhttpd.cc](./zhttpd.cc)
- Request planning/file work runs through a serialized worker queue. File producers retain queued callbacks through cancellation, clear their emitters before later delivery, and post chunks to Tx. Shutdown stops the server and drains worker requests before finalization/multiplexer stop. [zhttpd.cc](./zhttpd.cc)
- The reusable Application::init does not perform the CLI's validate step. Callers must supply valid options and register allocators before multiplexer startup. [zhttpd.hh](./zhttpd.hh), [zhttpd.cc](./zhttpd.cc)

## Side Effects

- zhttp starts workers/resolution/logging, performs network requests, creates/truncates response files unless discarding, and writes diagnostics to stderr. PUT validates echoed JSON and byte accounting. TLS/QUIC settings can enable key logging through their backends. [zhttp.cc](./zhttp.cc)
- zhttpd opens root/file handles, reads MIME configuration, serves files/ranges/listings, starts listeners/workers, and logs access records. Optional daemon/PID behavior delegates to ZiDaemon; a supervision FD receives synchronous one-byte events whose write result is ignored. Response events report submission, not confirmed delivery. [zhttpd.hh](./zhttpd.hh), [zhttpd.cc](./zhttpd.cc)
- File streaming duplicates handles and uses pread on the worker; pending reads may finish after emitter cancellation. [zhttpd.cc](./zhttpd.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zhttp test`. Manual utilities create network traffic and output/log/process artifacts according to their options. Commands were not run for this documentation change.
