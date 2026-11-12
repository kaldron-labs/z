# Zt: Framework types

Zt supplies dynamically sized arrays and strings, scratch storage, regular
expressions, bitmaps/windows, text formatting, time-zone and platform utilities.
It builds on Zu's traits and views and Zm's allocators. Include individual
headers such as `<zlib/ZtString.hh>` and link `libZt` with its dependencies.

## Arrays and strings

`ZtArray<T>` owns a contiguous sequence; `ZtString<>` owns text and supports
Z's stream formatting syntax:

```cpp
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

ZtArray<unsigned> values;
values.push(7);
values.push(11);
ZtString<> message;
message << "first=" << values[0];
```

Use `length()` for the number of elements and `size()` for storage capacity.
Construct or fill elements before reading them: these containers deliberately
avoid blanket initialization of unused storage. Spans into either container
are borrowed and can be invalidated by growth. `ZuMv` transfers ownership;
ordinary copies duplicate owned data.

Named template policies such as `ZtArrayHeapID<"App.Values">`,
`ZtStringHeapID<"App.Text">` and the corresponding `Sharded` policies select
allocation identity and partition behavior. Choose sharded storage only when
allocation/freeing obey the owning partition. See [heap tuning](../zm/README.md)
and the [array](test/ZtArrayTest.cc)/[string](test/ZtStringTest.cc) tests.

## Scratch and built-in storage

`ZtScratch(Type, capacity)` supplies scope-local initial storage for a compatible
array/string type. `ZtBuiltin<Type, N>` embeds a chosen initial capacity in an
object. Both let ordinary container operations use initial storage before
falling back to allocation when growth requires it.

```cpp
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

void format(unsigned value) {
  auto text = ZtScratch(ZtString<>, 64);
  text << "value=" << value;
  // Consume text here; its scratch storage belongs to this scope.
}
```

The capacity here is an example formatting budget, not a framework limit.
Choose it from the workload. Do not return a view into scratch storage or
capture it into asynchronous work. The [scratch tests](test/ZtScratchTest.cc)
cover growth, copy/move behavior and element lifetime.

## Regular expressions and text conversion

`ZtRegex` wraps PCRE. `ZtREGEX` caches a literal expression; `m()` matches,
`s()`/`sg()` substitute once/globally, and `split()` separates text.

```cpp
#include <zlib/ZtRegex.hh>
#include <zlib/ZtString.hh>

ZtString<> path{"/foo/bar"};
const auto &separator = ZtREGEX("/");
separator.sg(path, ":"); // path is now ":foo:bar"
```

Use `ZtRegexCaptures` or `ZtRegexSplitCaptures` for capture storage; named groups
can be located with `index(name)`. Capture spans borrow the matched input, so
consume them before mutating or releasing it. See [regex tests](test/ZtRegexTest.cc).
For fixed compile-time keyword sets, consider `ZuMatcher` instead.

`ZtIconv` converts character encodings, `ZtCase` supports case-related text
operations, and `ZtQuote`, `ZtHexDump`, `ZtBytesFmt` and `ZtFmt` provide output
adapters. Use [iconv](test/ZtIconvTest.cc), [quoting](test/ZtQuoteTest.cc) and
[formatting](test/ZtFmtTest.cc) examples for the destination and adapter APIs.
`ZtScanBool` parses boolean text; `ZtEnum` macros define enum names and maps
used by configuration and diagnostics.

## Bitmaps and moving windows

`ZtBitmap` provides dynamically allocated bit storage. `ZtWindow` is a
fixed-capacity sliding window backed by dynamically allocated storage;
its indexed proxy can extend the active window when assigned. `ZtBitWindow`
packs small values into a moving window (supported widths are listed in its
header). These are useful for sequence tracking and bounded recent history.
Use the [window](test/ZtWindowTest.cc), [bit-window](test/ZtBitWindowTest.cc)
and [bitmap](test/ZtBitmapTest.cc) tests when choosing indexing and reset behavior.

## Time zones and platform facilities

`Zt::tzOffset(dateTime, zone)` returns the time-zone offset for a `ZuDateTime`.
An explicit zone serializes access while temporarily changing `TZ` and invoking
platform time-zone setup; avoid calling that path repeatedly in a hot loop.
See [ZtTimeZone.hh](src/ZtTimeZone.hh) and [date tests](test/ZtDateTest.cc).

`ZtPlatform` provides platform/environment utilities, including `Zt::setenv`
and `Zt::unsetenv`. `ZtDemangle` formats C++ names. `ZtcRAGMap` provides reflected names for
Off/Red/Amber/Green states for telemetry presentation; it is a vocabulary facility
here, while collection and distribution live in Ztc.

## Examples and tests

Run `make -C zt test` from a configured build tree. The [test directory](test/)
contains executable API examples and boundary cases. See the top-level
[testing guide](docs/TESTING.md) for environment, diagnostics and port allocations,
and [dependencies](docs/DEPENDENCIES.md) for build prerequisites.
