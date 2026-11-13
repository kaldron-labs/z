# Ze

## Overview

Native error wrappers and structured diagnostic event construction and formatting.

## Subdirectories & Boundaries

- [src](./src/EGM.md): library implementation and installed headers.

## Interface

- [Source contract](./src/EGM.md): native errors, diagnostic events, exceptions, and formatting.

## Invariants

- Event ownership and native-error semantics are specified in the source contract. [src/ZePlatform.hh:ZeEvent]

## Side Effects

- Event formatting invokes caller-supplied message callables; Windows error messages use thread-local buffers. [src/ZePlatform.hh:ZeEvent::print] [src/ZePlatform.cc:Ze::strerror]

## Verification

From the configured repository build root, run `make -j8`, then `make -C ze test`. Commands were not run for this documentation change. [Makefile.am:test]
