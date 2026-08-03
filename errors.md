# Native test failures observed during the naming migration

These failures were observed in a clean GCC 16 release build configured with
`-O3 -g -DNDEBUG`. They are recorded separately from the layout migration so
they can be reproduced and investigated without obscuring the successful
compile and path/casing audits.

## Resolution

All failures below were reproduced with the existing GCC 16 release build and
are repaired as of 2026-08-03:

- `ZuBitmapCTest`: `zu_bitmap::data[1]` let GCC assume the variable-sized C
  bitmap contained only one word.  The GNU layout now uses a flexible array
  member; MSVC retains its supported one-element tail declaration.
- `ZuTLTest` and `ZfCfTest`: primitive objects are intentionally viewed through
  `ZuBox`, which needs GCC's `may_alias` attribute under strict aliasing.
- `ZuSpanTest`: the test retained a non-owning initializer-list span beyond the
  initializer list's full expression.  It now checks the nested span while the
  backing list is alive.
- `ZuEndianTest`: the x86 80-bit `long double` tests compared indeterminate
  bytes in its 16-byte ABI padding.  They now compare the ten value bytes.
- `ZuCmpTest`: `ZuTuple` class-template deduction retained references to rvalue
  constructor arguments.  Ordinary tuple deduction now owns decayed values;
  `ZuFwdTuple` remains the explicit forwarding/reference form.
- `ZmPolyHashTest`: keyed iterators retained a recursively reference-bearing
  tuple after the key expression expired.  Their stored key is now recursively
  decayed.
- `ZtDemangleTest`: GCC 16 changed the spelling of structural character
  template arguments.  The demangler accepts both the old and new cast forms.
- `ZfURITest`: generated `_0`/`_1` path keys were stored in a non-owning map
  after their temporary buffers expired.  Path nodes now use an indexed array
  under a stable internal key.
- HTTP/2 rotation and the HTTP/1 TLS matrix rows shared a TLS shard-ordering
  race: decrypted application records could reach HTTP before detached Tx
  state installation published `connected()`.  TLS now retains those records
  and drains them on the Rx owner after Tx installation completes.

Verification on the same build, without another clean:

- each of the nine deterministic binaries passed 20 consecutive runs;
- `ZhttpH2EngineTest` and `ZhttpMessageH2Test` each passed 12 consecutive runs;
- `ZtlsBufHookTest` passed all ten outer subtests;
- `zhttpmatrix` passed all 82 rows, with only its two documented prerequisite
  skips.

## Core suites

### `zu/test`

- `ZuBitmapCTest`: subtest `testBitmapCAPI` fails checks 22 and 26:
  `zu_bitmap_get(orv, 83)` and `zu_bitmap_get(xorv, 83)`.
- `ZuTLTest`: TAP test 1 fails:
  `buf == "1:C|2:B|3:A|4:E|5:D|"`.
- `ZuSpanTest`: exits with `SIGSEGV` before emitting any of its eight planned
  checks.
- `ZuEndianTest`: `test<long double>` fails inner check 5 and
  `test<ZuBox<long double>>` fails inner check 2, both checking the reversed
  endpoint byte.
- `ZuCmpTest`: TAP test 126 fails: `x.p<0>() == 42`.

### `zm/test`

- `ZmPolyHashTest`: TAP test 12 fails: `n == 2` after
  `iteration<3>({5})`.

### `zt/test`

- `ZtDemangleTest`: TAP tests 3, 5, 6, and 7 fail. The failed checks are the
  expected demangled string `A<"foobar">`, the escaped-string form
  `A<"a\\"b\\\\c\\n">`, and the two `ZuMatcher` offsets for `A<"foo">`
  and `A<"bar">`.

### `zf/test`

- `ZfURITest`: subtest `roundTrip`, inner check 3 fails: `uri_ == uri2`.
  The loaded value loses the original path string and ID, producing `//?...`
  instead of the original `/hello.../goodbye?...` URI.
- `ZfCfTest`: subtest `loadTypes`, inner check 64 fails:
  `roundTrip.time == value.time`.

## HTTP/2 and TLS exhaustion

The HTTP failures are intermittent. In the full GCC suite run:

- `ZhttpH2EngineTest` failed outer subtest 3, `runSharedTLS`, while rotating
  the HTTP/2 TLS connection after stream-ID exhaustion.
- `ZhttpMessageH2Test` failed its single `run<Zhttp::H2TLS>` outer subtest.
- Re-running both binaries immediately and independently passed every inner
  check, confirming that these two failures are timing/state dependent rather
  than deterministic TAP or path errors.
- `zhttp/itest/zhttpmatrix` failed its single outer matrix subtest after about
  126 seconds; the other five integration-test binaries passed. In the earlier
  detailed matrix run, the failing rows were HTTP/1 TLS stress cases
  `zhttp-zhttpd/h1-tls/j5n10`, `zhttp-zhttpd/h1-tls/j5n1000`,
  `curl-zhttpd/h1-tls/j1n1`, and `curl-zhttpd/h1-tls/j5n10`, with response EOF
  symptoms. That run completed 82 rows with two prerequisite skips.

`zhttp/interop` passed independently. No `zdb_pq` test was run.
