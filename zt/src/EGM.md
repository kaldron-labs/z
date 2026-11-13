# Zt source contract

## Overview

Dynamic arrays and strings, bitmaps and windows, regex and text adapters, and timezone/platform utilities.

## Interface

- `ZtArray<T, NTP>` supplies contiguous arrays, spans/iterators, search, capacity/length management, append, splice, and character conversion. [ZtArray.hh:ZtArray]
- Ordinary const-array/string construction copies; matching string literals and explicit shadow operations retain external storage. Explicit pointer construction can adopt storage. [ZtArray.hh:ctor] [ZtArray.hh:own_] [ZtArray.hh:operator -=]
- `ZtBitWindow` provides a noncopyable sliding window of packed values; `ZtBitmap` supplies dynamically sized bitmap words. [ZtBitWindow.hh:ZtBitWindow] [ZtBitmap.hh:ZtBitmap]
- `ZtBuiltin<Array, N>` embeds initial container storage. `ZtCase` transforms snake/camel ASCII names; `ZtBytesFmt` names byte encodings; `ZtEnum` and `ZtFlags` macros generate enum/flag maps and formatting. [ZtBuiltin.hh:ZtBuiltin] [ZtCase.hh:snakeCamel] [ZtBytesFmt.hh:ZtBytesFmt] [ZtEnum.hh:ZtEnumValues]
- `ZtDemangle::init` installs a transform for framework string template spellings. [ZtDemangle.cc:ZtDemangle::init]
- `ZtFmt`/`ZtVFmt` define scalar, date, flags, and vector formatting policies; `ZtHexDump` and `ZtQuote` adapt binary/text output. [ZtFmt.hh:ZtFmt] [ZtFmt.hh:ZtVFmt] [ZtHexDump.hh:ZtHexDump] [ZtQuote.hh:ZtQuote]
- `ZtIconv` converts named encodings; `ZtScanBool` parses boolean strings, optionally throwing on invalid input. `ZtRegex` wraps PCRE compilation, study, matching, substitution, splitting, and capture storage. [ZtIconv.hh:ZtIconv] [ZtScanBool.hh:ZtScanBool] [ZtRegex.hh:ZtRegex]
- `ZtScratch` supplies scope-local initial array/string storage; platform helpers manage environment variables. [ZtScratch.hh:ZtScratch_] [ZtPlatform.hh:setenv]
- `ZtString` and `ZtWString` provide policy-configurable narrow/wide strings with built-in storage, formatting, append/splice, and conversions. Copies duplicate data; array/literal construction may shadow external storage. [ZtString.hh:String] [ZtString.hh:ctor]
- `ZtWindow<T>` stores a sliding window with borrowed indexed proxies; `Zt::tzOffset` computes DST-adjusted offsets; `Ztc::RAG::Map` supplies Off/Red/Amber/Green names. [ZtWindow.hh:Window] [ZtTimeZone.cc:tzOffset] [ZtcRAGMap.hh:Map]
- `libZt.la` exports the installed headers listed by the manifest and links Zm and Zu. [Makefile.am:lib_LTLIBRARIES] [Makefile.am:pkginclude_HEADERS]

## Invariants

- Shadow storage has capacity equal to length and clears mutable/allocation ownership flags. Secret arrays require primitive elements. [ZtArray.hh:shadow__] [ZtArray.hh:ZtArray]
- Bitmap lengths round up to 64-bit words and growth zeroes new words. Built-in storage is passed to its array as storage that must not be freed by the allocator. [ZtBitmap.hh:Data] [ZtBuiltin.hh:ZtBuiltin]
- Packed bit-window set operations OR values without masking; the 64-bit specialization assigns whole values. [ZtBitWindow.hh:set]
- Regex captures and quote wrappers borrow source bytes; ZtHexDump_ borrows while ZtHexDump copies data. Iconv output resizing must preserve existing bytes and expose mutable storage. Regex split appends to output. [ZtRegex.hh:capture] [ZtRegex.hh:split] [ZtQuote.hh:String] [ZtHexDump.hh:ZtHexDump_] [ZtIconv.hh:ZtIconvFn]
- String null data() returns nullptr while ndata() supplies an empty C string. Shadow/adopt operations trust supplied storage; raw mutable access does not detach shadows. Allocation ownership and mutability are distinct flags. [ZtString.hh:data] [ZtString.hh:ndata] [ZtString.hh:shadow_] [ZtString.hh:own_]
- Window writes before its offset are ignored; callers must provide a nonzero maximum. [ZtWindow.hh:set]

## Side Effects

- Array growth and splice may allocate and move elements; direct data access does not detach shadows. Secret shrinking and owned-storage cleanup clear bytes; release relinquishes ownership without freeing. [ZtArray.hh:ensure] [ZtArray.hh:splice] [ZtArray.hh:data] [ZtArray.hh:length_] [ZtArray.hh:free_] [ZtArray.hh:release]
- Bit-window mutation can allocate/free backing words and advance the window; built-in array resizing can construct/destroy elements. Case conversion callbacks receive temporary scratch storage. Demangler initialization replaces the global transform function. [ZtBitWindow.hh:ensure] [ZtBuiltin.hh:length] [ZtCase.hh:camelSnake] [ZtDemangle.cc:init]
- Regex operations allocate PCRE resources and may throw; substitution mutates its string and match iteration invokes callbacks. Iconv resizes/writes output and can return partial conversion. Platform environment helpers change process environment. [ZtRegex.cc:study] [ZtRegex.hh:s] [ZtIconv.hh:convert] [ZtPlatform.hh:setenv]
- String growth may allocate and invalidate views; ensure detaches shadows. Secret cleanup clears mutable discarded bytes, but not borrowed shadows. Splice/formatting invokes supplied callbacks. [ZtString.hh:ensure] [ZtString.hh:free_] [ZtString.hh:splice]
- Timezone offset calculation serializes through a singleton lock and invokes timezone setup. Its guard changes process TZ temporarily for explicit zones; with a null zone, guard destruction unsets TZ. [ZtTimeZone.cc:Zt_TzGuard]

## Verification

From the configured repository root, run `make -j8 && make -C zt test`. The module test target is defined in `zt/Makefile.am`. Commands were not executed for this documentation change.
