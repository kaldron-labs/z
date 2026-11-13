# Zf

## Overview

Reflection-backed field metadata and codecs for JSON, CBOR, D-Bus, ASN.1, CLI, URI, CSV, Cf, TOML and YAML, with shared owned configuration trees.

## Subdirectories & Boundaries

- [src](./src/EGM.md): installed field metadata, codecs and configuration tree interfaces.

## Interface

- Reflected fields, facets and filters drive typed serialization/construction/update. Codec handlers define format-specific shapes, errors and ownership; configuration formats share ZfTree and ZfTreeLoad. [src/ZfStruct.hh:ZfFieldFilter] [src/ZfJSON.hh:AsObject] [src/ZfTree.hh:AnyNode] [src/ZfTreeLoad.hh:Object]

## Invariants

- Runtime reflection trusts declared type codes; borrowed spans/nodes require retained backing. Mutable-input codecs can overwrite escapes/delimiters, while owned configuration trees copy parsed values. Format-specific validation and default behavior differ. [src/ZfStruct.hh:VGet] [src/ZfJSON.hh:AnyNode] [src/ZfURI.cc:eos] [src/ZfCf.cc:eos]

## Side Effects

- Parsing/serialization allocates trees or scratch, appends output and mutates caller objects/input where specified. Configuration directives can read environment values and invoke callbacks. Loads/updates and output are generally incremental, with no universal rollback. [src/ZfJSON.hh:loadValue_] [src/ZfTreeLoad.hh:Map] [src/ZfCf.cc:eod] [src/ZfTOML.hh:saveEntry]

## Verification

From the configured repository root, run `make -j8 && make -C zf test`. Commands were not executed for this documentation change. [Makefile.am:test]
