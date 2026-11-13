# Zfb

## Overview

FlatBuffers wire support integrated with native framework metadata, explicit verification and borrowed access, canonical reflection/schema export, and native C++ schema generation.

## Subdirectories & Boundaries

- [Library and compiler](./src/EGM.md): `Zfb` codecs, `Reflect` metadata/payload operations, BFBS/FBS export, and `zflatc`.

## Interface

The module exports `libZfb.la`, `ZfbLib.hh`, `Zfb.hh`, `ZfbReflect.hh`, and installed `zflatc`. [Build manifest](./src/Makefile.am).

## Invariants

Views and `load` require trusted input and surviving backing; verification is explicit. Builders preserve wire offsets, alignment, field IDs and union/required-field constraints. Reflection handles borrow verified BFBS. [Zfb.hh](./src/Zfb.hh), [ZfbReflect.hh](./src/ZfbReflect.hh).

## Side Effects

Codecs mutate/allocate output and materialized objects; schema export writes streams. `zflatc` reads schema files and publishes generated header/source pairs through staged files and rollback-capable backups. See [source contract](./src/EGM.md).

## Verification

From a configured repository root, run `make -j8 && make -C zfb/test test`. Module `make test` includes integration tests. Test/interoperability sources are excluded from this index. No tests or compiler runs were performed for this documentation change.
