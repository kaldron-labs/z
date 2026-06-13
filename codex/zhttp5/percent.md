## Summary

Factor percent escaping out of `ZtURI.hh` / `ZtURI.cc` into a new Zu-level
header, `zu/src/ZuPercent.hh`.

The existing percent logic is split across:

- decode:
  - `ZtURI::eoc()` in `zt/src/ZtURI.cc`;
  - `ZtURI::eos()` in `zt/src/ZtURI.cc`;
- encode:
  - `ZtURI::hex()` in `zt/src/ZtURI.hh`;
  - `ZtURI::escaped()` in `zt/src/ZtURI.hh`;
  - `ZtURI::URIQuote<Body>::quote()` in `zt/src/ZtURI.hh`.

The new `ZuPercent.hh` should own the byte-level percent encode/decode
mechanics. `ZtURI` should own only URI/query policy: which bytes require
escaping, which bytes terminate a path component or query token, and whether
`+` decodes to space.

Unlike `ZuBase32`, `ZuBase64`, and `ZuHex`, percent-encoded output length is
data-dependent. The API should therefore follow the `ZuUTF` shape: provide a
small span/length result type plus `span()`/`len()`/`cvt()`-style functions that
make the required sizing pass explicit.

## Current Behavior To Preserve

`ZtURI::eoc()`:

- decodes `%XX` in place;
- rejects malformed percent escapes;
- does not translate `+`;
- terminates on `/`, `?`, or `ZtURI::escaped(c)`;
- writes `0` over the terminator in the fast path;
- zero-fills the reduced tail in the slow path;
- returns `{output, input, terminator}`;
- returns `{-1}` on malformed input.

`ZtURI::eos()`:

- decodes `%XX` in place;
- translates `+` to space;
- rejects malformed percent escapes;
- terminates on `ZtURI::escaped(c)`;
- writes `0` over the terminator in the fast path;
- zero-fills the reduced tail in the slow path;
- returns `{output, input, terminator}`;
- returns `{-1}` on malformed input.

`ZtURI::URIQuote<Body>::quote()`:

- for body/query form mode, emits `+` for space;
- otherwise emits `%XX` for `escaped(c)`;
- otherwise emits the byte unchanged;
- uses uppercase hex digits.

The current URI escape policy escapes:

- ASCII control bytes;
- bytes >= 128;
- space, `"`, `#`, `%`, `&`, `'`, `<`, `=`, `>`.

This plan does not change that policy. If path escaping should later escape
`/` and `?`, that should be a separate behavior change with tests.

## Proposed API

Add `zu/src/ZuPercent.hh` as a header-only Zu utility.

Use short names consistent with existing Zu headers and avoid STL.

```c++
namespace ZuPercent {

ZuInline constexpr int hex(char c);
ZuInline constexpr char digit(unsigned n);

struct Span {
  Span() = default;
  Span(uint64_t inLen_, uint64_t outLen_) : inLen{inLen_}, outLen{outLen_} { }

  uint64_t inLen = 0;
  uint64_t outLen = 0;

  bool operator !() const { return !inLen && !outLen; }
  ZuOpBool
};

struct Scan {
  int  out = -1;       // decoded/encoded output length
  int  in = -1;        // consumed input length
  char term = 0;       // terminator, if any

  bool operator !() const { return out < 0; }
  ZuOpBool
};

template <typename Policy>
struct Codec {
  static Span span(ZuBSpan src);
  static uint64_t len(ZuBSpan src);
  static uint64_t encode(ZuSpan<uint8_t> dst, ZuBSpan src);
  static ZuTuple<uint64_t, bool> encode_overflow(
    ZuSpan<uint8_t> dst, ZuBSpan src);
  template <typename S> static void print(S &s, ZuBSpan src);
  static Scan decode(ZuSpan<char> src);
};

} // namespace ZuPercent
```

The exact names can be tightened during implementation, but the important shape
is:

- `span()` performs the sizing pass and returns input consumed plus output
  length, like `ZuUTF::span()`;
- `len()` returns only encoded output length for callers that already know they
  will consume the whole span;
- `encode()` writes to a caller-provided buffer and returns bytes written;
- `encode_overflow()` mirrors `ZuUTF::cvt_overflow()` by returning bytes
  written plus whether output capacity prevented complete encoding;
- `print()` preserves the current stream-oriented `URIQuote` usage without
  forcing allocation;
- `decode()` performs the current in-place overwrite decode and returns the
  `eoc()`/`eos()`-style scan result.

## Policy Model

Keep the generic codec policy-based so Zu does not become URI-specific.

Policy callbacks:

```c++
struct Policy {
  static bool esc(uint8_t c);        // true => encode as %XX
  static bool term(uint8_t c);       // true => decode terminator
  static bool plus();                // true => + decodes to space
  static bool spacePlus();           // true => space encodes as +
};
```

`ZuPercent::Codec<Policy>` uses these callbacks:

- encode:
  - if `Policy::spacePlus()` and byte is space, emit `+`;
  - else if `Policy::esc(c)`, emit `%` and uppercase hex digits;
  - else emit byte unchanged.
- decode:
  - if `%`, decode two hex digits or fail;
  - if `Policy::plus()` and byte is `+`, write space;
  - if `Policy::term(c)`, terminate and return;
  - otherwise copy byte through.

Add reusable policy helpers in `ZuPercent.hh` only if they are genuinely generic:

```c++
struct NoTerm {
  static constexpr bool term(uint8_t) { return false; }
};

struct NoPlus {
  static constexpr bool plus() { return false; }
  static constexpr bool spacePlus() { return false; }
};
```

Keep the ZtURI-specific policy in `ZtURI.hh`, for example:

```c++
namespace ZtURI {

struct PercentEsc {
  static constexpr bool esc(uint8_t c) { return escaped(c); }
};

struct PercentPath : public PercentEsc {
  static constexpr bool term(uint8_t c) {
    return c == '/' || c == '?' || escaped(c);
  }
  static constexpr bool plus() { return false; }
  static constexpr bool spacePlus() { return false; }
};

struct PercentQuery : public PercentEsc {
  static constexpr bool term(uint8_t c) { return escaped(c); }
  static constexpr bool plus() { return true; }
  static constexpr bool spacePlus() { return false; }
};

template <bool Body>
struct PercentQuote : public PercentEsc {
  static constexpr bool term(uint8_t) { return false; }
  static constexpr bool plus() { return Body; }
  static constexpr bool spacePlus() { return Body; }
};

} // namespace ZtURI
```

If `plus()` and `spacePlus()` are too verbose, collapse them into one policy
constant. Keep decode and encode behavior explicit enough that `eoc()` and
`eos()` remain obviously different.

## Detailed Implementation Plan

### Phase 1: Add `ZuPercent.hh` With Generic Mechanics

- Add `zu/src/ZuPercent.hh`.
- Include direct dependencies:
  - `ZuLib.hh`;
  - `ZuSpan.hh`;
  - `ZuTuple.hh` if the result uses tuples, or avoid it if using `Scan`;
  - `ZuStream.hh` only if needed for print helpers.
- Move `hex()` mechanics from `ZtURI` into `ZuPercent::hex()`.
- Add uppercase `digit()` for encoding.
- Implement encode sizing:

```c++
static Span span(ZuBSpan src) {
  uint64_t out = 0;
  for (unsigned i = 0, n = src.length(); i < n; ++i) {
    uint8_t c = src[i];
    out += (Policy::spacePlus() && c == ' ') ? 1 :
      Policy::esc(c) ? 3 : 1;
  }
  return {src.length(), out};
}
```

- Implement `len()` as `span(src).outLen`.
- Implement `encode(dst, src)` with silent truncation semantics matching
  `ZuUTF::cvt()`:
  - stop before writing a partial `%XX`;
  - return bytes written.
- Implement `encode_overflow(dst, src)` with `ZuUTF::cvt_overflow()` semantics:
  - return `{bytesWritten, overflow}`;
  - set `overflow` when the destination fills before all input bytes are
    encoded;
  - do not emit partial `%XX` when only one or two destination bytes remain.
- Implement `print(s, src)` as the allocation-free stream path used by
  `URIQuote`.
- Implement `decode(src)` by factoring the common `eoc()` / `eos()` slow-path
  structure:
  - fast path scans until `%`, optional `+`, or terminator;
  - slow path overwrites in place;
  - malformed `%` returns failure;
  - reduction zero-fills the tail exactly as today.

### Phase 2: Replace ZtURI Encode/Decode Internals

- Include `<zlib/ZuPercent.hh>` from `zt/src/ZtURI.hh`.
- Move `ZtURI::hex()` to `ZuPercent::hex()` as the percent decode helper.
  Remove `ZtURI::hex()` and update the local decode sites to use
  `ZuPercent::hex()` through `ZuPercent::Codec`.
- Add and use `ZuPercent::digit()` as the matching encode helper for uppercase
  hex digit emission.
- Keep `ZtURI::escaped()` in `ZtURI.hh`; it is URI policy, not generic percent
  mechanics.
- Replace `URIQuote<Body>::quote()` with:

```c++
template <bool Body = false>
struct URIQuote {
  template <typename S>
  static void quote(S &s, ZuCSpan v) {
    using Policy = PercentQuote<Body>;
    ZuPercent::Codec<Policy>::print(s, ZuBSpan{v});
  }
};
```

- Replace `eoc()` implementation with:

```c++
auto r = ZuPercent::Codec<PercentPath>::decode(span);
return {r.out, r.in, r.term};
```

- Replace `eos()` implementation with:

```c++
auto r = ZuPercent::Codec<PercentQuery>::decode(span);
return {r.out, r.in, r.term};
```

- Preserve exported `ZtURI::eos()` and local `eoc()` behavior during the first
  pass so existing `ZtURI::scan()` and downstream users do not change API.

### Phase 3: Build Metadata

- Add `ZuPercent.hh` to `zu/src/Makefile.am` `pkginclude_HEADERS`.
- Add `ZuPercentTest` to `zu/test/Makefile.am`:
  - `noinst_PROGRAMS`;
  - `ZuPercentTest_SOURCES = ZuPercentTest.cc`.

No `.cc` file is needed if `ZuPercent.hh` is header-only.

### Phase 4: Tests

Add `zu/test/ZuPercentTest.cc`.

Unit cases:

- `hex()`:
  - accepts `0`-`9`, `a`-`f`, `A`-`F`;
  - rejects non-hex bytes.
- `span()` / `len()`:
  - no escaped bytes returns same length;
  - one escaped byte adds 2 output bytes;
  - all escaped bytes size correctly;
  - body form with space plus does not expand spaces.
- `encode()`:
  - `hello` -> `hello`;
  - `a b` -> `a%20b` in non-body mode;
  - `a b` -> `a+b` in body mode;
  - `%` -> `%25`;
  - bytes >= 128 are escaped using uppercase hex;
  - truncation never emits partial `%XX`.
- `encode_overflow()`:
  - exact-size output returns `overflow == false`;
  - short output returns `overflow == true`;
  - short output still never emits partial `%XX`;
  - body-form `+` truncation is reported correctly.
- `print()`:
  - matches `encode()` output for the same policy.
- `decode()`:
  - `foo%20bar&` -> `foo bar` with terminator `&`;
  - `foo+bar&` -> `foo bar` only when plus decode is enabled;
  - `foo+bar/` remains `foo+bar` in path mode;
  - malformed `%`, `%x`, and `%zz` fail;
  - fast path terminates and null-terminates without shifting;
  - slow path zero-fills the reduced tail.

Keep and extend `zt/test/ZtURITest.cc`:

- existing round-trip tests must still pass;
- malformed `%` tests must still fail;
- add direct cases for:
  - query body space encoding as `+`;
  - query value `%20` decoding to space;
  - path component `%2F` decoding to `/` inside one component;
  - path raw `/` still terminating a component;
  - uppercase `%XX` output from URI save.

Suggested commands:

```sh
make -C zu/test ZuPercentTest
./zu/test/ZuPercentTest
make -C zt/test ZtURITest
./zt/test/ZtURITest
```

## Code References

- `zu/src/ZuUTF.hh:230` - API model: explicit span/length pass plus conversion
  helpers for data-dependent output size.
- `zu/src/ZuBase64.hh:36` - Counterexample: fixed formula `enclen()` works for
  base encodings but not for percent escaping.
- `zt/src/ZtURI.hh:394` - Current `ZtURI::hex()` helper to move or wrap.
- `zt/src/ZtURI.hh:569` - Current URI escape policy; keep policy in `ZtURI`.
- `zt/src/ZtURI.hh:579` - Current `URIQuote<Body>` encode implementation to
  replace with `ZuPercent::Codec<...>::print()`.
- `zt/src/ZtURI.cc:26` - Current path-component decode logic, `eoc()`.
- `zt/src/ZtURI.cc:95` - Current query token decode logic, `eos()`.
- `zt/src/ZtURI.cc:218` - `scan()` path parse calls `eoc()`.
- `zt/src/ZtURI.cc:234` - `scan()` query parse calls `eos()` for keys/values.
- `zt/test/ZtURITest.cc:166` - Existing malformed percent negative tests.
- `zu/src/Makefile.am:14` - Add `ZuPercent.hh` to installed Zu headers.
- `zu/test/Makefile.am:12` - Add `ZuPercentTest`.

## Acceptance Criteria

- `ZuPercent.hh` contains the reusable percent encode/decode mechanics.
- `ZuPercent::hex()` is the decode helper and `ZuPercent::digit()` is the
  matching uppercase encode helper.
- `ZuPercent::Scan` is a named result struct, not a raw tuple at the public
  codec boundary.
- `ZuPercent::Codec<Policy>` provides overflow-reporting encode behavior
  analogous to `ZuUTF::cvt_overflow()`.
- `ZtURI.hh` no longer contains byte-level percent encode logic except URI
  policy wrappers.
- `ZtURI.cc` no longer implements separate hand-written `eoc()` and `eos()`
  decode loops; both delegate to `ZuPercent`.
- Current `ZtURI::scan()` behavior is preserved.
- Current `URIQuote<Body>` output is preserved.
- `ZuPercentTest` passes.
- `ZtURITest` passes.
- No new STL dependency is introduced.
- No heap allocation is required for stream `quote()` / `print()` paths.

## Non-goals

- Changing URI escaping policy.
- Changing `ZtURI::scan()` parse semantics.
- Introducing compatibility shims beyond narrow wrappers needed to keep
  existing `ZtURI` callers compiling during the refactor.
- Adding percent escaping to unrelated modules in the same change.
- Normalizing lowercase percent escapes to uppercase during decode; decode
  should accept either case and preserve decoded bytes.

## Resolved Decisions

- Move `hex()` to `ZuPercent` as the percent decode helper. Do not keep
  `ZtURI::hex()` unless implementation discovers an external compile break that
  requires a temporary narrow wrapper.
- Add matching `ZuPercent::digit()` for uppercase percent encode output.
- Use a named `ZuPercent::Scan` result struct at the public codec boundary,
  because field names make the `eoc()`/`eos()` contract harder to misuse.
- Provide overflow-reporting encode behavior like `ZuUTF::cvt_overflow()`.
  Keep plain `encode()` for callers that want silent truncation, and add
  `encode_overflow()` for callers that need to distinguish complete output from
  capacity exhaustion.
