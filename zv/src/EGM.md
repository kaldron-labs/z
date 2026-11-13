# Zv configuration I/O

## Overview

Filesystem wrappers for configuration, YAML, and TOML serialization, plus configuration-to-runtime-parameter adapters.

## Interface

- `ZvCf::load` accepts paths, optional directive callbacks, and shared definitions; YAML/TOML loads accept parser limits. Save helpers select format facets and Save/Upd/Del field filters. [ZvCf.hh:load] [ZvYAML.hh:load] [ZvTOML.hh:save]
- `ZvMxParams`, `ZvThreadParams`, `ZvRingParams`, and `ZvStackParams` adapt configuration nodes into runtime parameters. [ZvMxParams.hh:ZvMxParams] [ZvThreadParams.hh:ZvThreadParams] [ZvRingParams.hh:ZvRingParams] [ZvStackParams.hh:ZvStackParams]
- Builds `libZv.la` and installs the public headers enumerated by the manifest. [Makefile.am:libZv_la_SOURCES] [Makefile.am:pkginclude_HEADERS]

## Invariants

- Files must be smaller than 1 MiB; mappings remain alive through scanning, while empty files use a zero-length static span. [ZvCf.cc:Reader::map] [ZvYAML.cc:map] [ZvTOML.cc:map]
- Cf includes require one nonempty argument, resolve relative to the containing file, reject canonical-path cycles, and restore CURDIR after expansion or exceptions. [ZvCf.cc:Reader::pct]
- Multicast entries require interfaces and valid multicast addresses; scheduler thread keys must resolve to configured SIDs. Null configuration preserves supplied defaults. [ZvMxParams.hh:ZvCxnOptions] [ZvMxParams.hh:ZvMxParams] [ZvStackParams.hh:ZvStackParams]

## Side Effects

- Loading canonicalizes, stats, and maps files and reports path-aware errors. Cf loading changes TOPDIR/CURDIR definitions and can call application directive handlers. [ZvCf.cc:Reader::load] [ZvCf.cc:setDefine] [ZvCf.cc:Reader::pct]
- Saving opens output with Write|GC, serializes through `ZiFileTxStream`, explicitly flushes, and throws on output failure. [ZvCf.hh:save] [ZvYAML.hh:save] [ZvTOML.hh:save]

## Verification

Run `make -j8 && make -C zv/test test` from the configured repository root. The test manifest runs ZvCfTest, ZvYAMLTest, and ZvTOMLTest through prove. Commands were not executed.
