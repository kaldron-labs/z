# Zfix typed order example

## Overview

A one-order client/server demonstration with typed FIX.4.4 dispatch, two execution-report alternatives, and an unsolicited tick.

## Interface

- [zfix.hh](./zfix.hh): message D order with parties, two type-8 responses selected by tag 150, and custom U1 tick.
- [zfix.cc](./zfix.cc): client defaults to TCP localhost:16404, quantity 10, price 123.45 and ten-second timeout; optional TLS requires CA.
- [zfixd.cc](./zfixd.cc): server defaults to TCP 127.0.0.1:16404 and thirty-second timeout; optional TLS requires certificate/key, and port zero allows ephemeral binding.
- [Makefile.am](./Makefile.am) builds non-installed zfix/zfixd.

## Invariants

The client sends one order and validates acknowledgement, fill and tick before logout. The server uses fixed EXAMPLE-S/EXAMPLE-C identities, accepts admission without credential checks, and maintains one logical link. Queue admission is bounded, but these examples provide no durable association/history: history requests gap-fill. Timeout disconnect still waits for the actual stop barrier. See [client](./zfix.cc) and [server](./zfixd.cc).

## Side Effects

Both programs open sockets, start transport workers, print human-readable events to stdout and logs/errors to stderr. TLS mode reads configured credential material. They return 0 for success, 1 for runtime failure, and 2 for usage errors. See [client](./zfix.cc) and [server](./zfixd.cc).

## Verification

From a configured repository root, run `make -j8 && make -C zfix test`. Manual paired examples require a free endpoint and, for TLS, suitable certificates. Commands were not run for this documentation change.
