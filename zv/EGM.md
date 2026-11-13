# Zv

## Overview

Configuration file I/O and runtime parameter adaptation.

## Subdirectories & Boundaries

- [src](./src/EGM.md): library contracts.

## Interface

Load/save Cf, YAML, and TOML files and configure multiplexer/thread/ring parameters. [src/ZvCf.hh:load] [src/ZvMxParams.hh:ZvMxParams]

## Invariants

File loads enforce the size limit and include-cycle checks; parameter metadata constrains values. [src/ZvCf.cc:Reader::map] [src/ZvCf.cc:Reader::pct] [src/ZvRingParams.hh:ZfStruct]

## Side Effects

Maps input files, updates Cf definitions, invokes directive callbacks, and writes serialized output. [src/ZvCf.cc:Reader::load] [src/ZvCf.cc:Reader::pct] [src/ZvCf.hh:save]

## Verification

From the configured repository root, run `make -j8 && make -C zv/test test`. Commands were not executed for this documentation change.
