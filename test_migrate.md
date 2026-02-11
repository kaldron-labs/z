# Migrate Legacy Tests to `ZuTest`/`ZuTestUtil`

Use this document as an implementation playbook. Apply every rule in imperative form. Do not skip checklist items.

## 1. Adopt the Harness Entry Point

1. Include `ZuTestUtil` in migrated test files.

```c++
#include <zlib/ZuTestUtil.hh>
```

2. Add `ZuTestMain();` at the start of `main()` before emitting checks.
3. Keep `main()` minimal and deterministic.
4. Prefer `using namespace ZuTestUtil;` when using `parse`, `log`, `verbose`, or `ZuCHECK`.

## 2. Parse CLI Options Correctly

1. Use `parse(argc, argv);` when the test accepts quiet/verbose behavior through the shared utility.
2. Treat `-q` as the quiet flag.
3. Respect `HARNESS_ACTIVE` defaults (`verbose = false` under harness).
4. If the file already has custom options, integrate `-q` into the custom parser and preserve existing options.
5. Keep usage text aligned with `-q` semantics.
6. If a tiny test has no runtime logging/CLI needs, `int main()` + `ZuTestMain();` is valid.

Canonical utility-driven `main`:

```c++
int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ...
}
```

Canonical custom-parser shape:

```c++
int main(int argc, char **argv)
{
  verbose = !::getenv("HARNESS_ACTIVE");
  // parse file-specific options + -q
  ZuTestMain();
  ...
}
```

## 3. Replace Legacy Pass/Fail Plumbing

1. Replace custom `CHECK`/`CHECK2`/`out`/`fail`/manual `OK/NOK` output with `ZuCheck` or `ZuCHECK`.
2. Remove assertion pathways that intentionally crash/abort the test flow.
3. Keep fail-fast helpers only when they are required for procedural integration flow; still emit a top-level `ZuCheck` result in TAP.
4. Convert `assert(...)`-based runtime checks in test flow to `ZuCheck(...)`.
5. Prefer `ZuCheck(expr)` when expression text is enough.
6. Use `ZuCheck(expr, diagnostics)` when failure needs context.
7. Use `ZuCHECK(expr, ...)` for concise failure-only diagnostics assembled from variadic arguments.

Use the utility macro when concise failure diagnostics are needed:

```c++
ZuCHECK(expr, "k=", key, " n=", n);
```

## 4. Use Structured Subtests

1. Add `ZuTestScope(...)` to helper functions that represent logical test units.
2. Call helper test units via `ZuTestCall(...)` instead of direct calls.
3. Wrap template call targets with parentheses in `ZuTestCall` when required.
4. Use `ZuTestScope_("name")` when a string label is clearer than a function identifier.
5. Use `ZuTestCall_("name", fn, ...)` when default call labels are not descriptive.
6. Use macro wrappers (`TEST`, `TEST_`, etc.) only when they expand to `ZuTestCall`/`ZuTestCall_`.
7. When macro arguments are expressions, generate stable readable test names

Examples:

```c++
void enc(...) {
  ZuTestScope(enc);
  ZuCheck(...);
}

ZuTestCall(enc, src, dst, msg);
ZuTestCall((test<int, ZuFmt::Default>), "int", 42, "42");
ZuTestCall_("float decode", decode, value, expected);
```

Alternative static-scope entry pattern (use when invoking a top-level test function directly from `main`):

```c++
template <auto &ZuTest_scope>
void test()
{
  ...
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  test<ZuTest_scope>();
}
```

## 5. Use Runtime (`RT`) Macros Only for Runtime-Variable Counts

1. Use `ZuTestScopeRT`, `ZuTestCallRT`, and `ZuCheckRT` only when subtest/check counts depend on runtime values.
2. Use static macros (`ZuTestScope`, `ZuTestCall`, `ZuCheck`) when counts are fixed.
3. Use `ZuTestCallRT_` when runtime calls also need custom names.

Static and runtime inline subtest forms are both valid:

```c++
{ ZuTest(empty); }
{ ZuTestRepeat(case_name, 5); for (...) ZuCheck(...); }
```

## 6. Convert Output-Only Validation into Assertions

1. Replace print-only validation with explicit checks.
2. Keep logs as optional diagnostics, not as pass/fail signals.
3. Move heavy trace output behind `if (verbose)` or `log(...)`.
4. Emit diagnostics to `std::cerr` in failure callbacks.
5. Prefer `log(...)`/`log_(...)` from `ZuTestUtil` instead of manual `std::cout` status output.
6. Preserve essential debug probes, but make assertions authoritative.

Failure-only diagnostic pattern:

```c++
inline void decOut_(const char *msg, ZuBSpan actual) {
  std::cerr << msg << '\n';
  std::cerr << "  " << actual << '\n';
}

ZuCheck(decoded == expected, decOut_(msg, decoded));
```

## 7. Expand Deterministic Coverage Where Migration Did So

1. Split large path-heavy suites into named top-level test functions that contain sub-tests.
2. Dispatch those top-level test functions from `main()` via `ZuTestCall(...)`.
3. Add explicit state/invariant checks that replace implicit visual inspection.
4. For compile-time behavior, keep/add `constexpr` helpers and `static_assert(...)`.
5. Keep matching runtime checks for equivalent behavior where practical.
6. Extract unrelated concerns into dedicated test files when splitting oversized suites.

## 8. Keep TAP Tests Focused

1. Remove or isolate benchmark/performance loops from TAP unit tests.
2. Keep TAP tests deterministic and fast enough for harness runs.
3. For long-running async/integration tests, it is acceptable to keep internal procedural guards, but end with TAP-visible success/failure (`ZuCheck(run())`, `ZuCheck(ok)`, or equivalent).

## 9. Comprehensive Mandatory Checklist

Mark every item as done before considering migration complete.

- [ ] Include `<zlib/ZuTestUtil.hh>` in every migrated test file.
- [ ] Add `ZuTestMain();` before the first `ZuCheck`/`ZuCHECK`/`ZuTestCall`.
- [ ] Use `parse(argc, argv);` when the test uses utility logging/quiet behavior, unless the test consumes additional command line options or arguments.
- [ ] Treat `-q` as quiet mode. Do not introduce `-v` as the migration default.
- [ ] Respect `HARNESS_ACTIVE` in option handling (`verbose` defaulting).
- [ ] Integrate `-q` into custom parsers for files with existing CLI options.
- [ ] Keep usage text and CLI behavior synchronized with actual options.
- [ ] Replace legacy `CHECK`/`CHECK2`/`out`/`fail`/`OK/NOK` pipelines with `ZuCheck`/`ZuCHECK`.
- [ ] Remove runtime test assertions that intentionally fail/abort on the normal path.
- [ ] Replace `assert(...)` runtime test checks with `ZuCheck(...)`.
- [ ] Add `ZuTestScope(...)` to helper functions that represent test units.
- [ ] Route helper invocations through `ZuTestCall(...)` instead of direct calls.
- [ ] Wrap template callables in parentheses in `ZuTestCall(...)` where required.
- [ ] Use `ZuTestScope("name")`/`ZuTestCall_(...)` when custom naming improves TAP output clarity.
- [ ] Use `ZuTestScopeRT`/`ZuTestCallRT`/`ZuCheckRT` only for runtime-variable test counts.
- [ ] Use static macros for fixed-count tests.
- [ ] Use `ZuTest(...)` and `ZuTestRepeat(...)` for inline block subtests/repeats where appropriate.
- [ ] Use the `template <auto &ZuTest_scope> ... test<ZuTest_scope>()` pattern when a direct top-level function invocation is preferred over `ZuTestCall`.
- [ ] Convert print-only validation into explicit assertions.
- [ ] Keep diagnostics failure-only or verbose-gated; do not rely on always-on status chatter.
- [ ] Prefer `log(...)`/`log_(...)` and `std::cerr` diagnostics over `std::cout` pass/fail protocols.
  - Ensure all non-TAP output is `verbose`-gated and to `std::cerr`, either by using `ZuTestUtil::log` or `if (verbose) std::cerr << ...`
  - Ensure all use of `printf`/`puts`/`putchar` etc. is converted to `log(x, y, z)` or `if (verbose) std::cerr << x << y << z ...` as appropriate
- [ ] Ensure every critical state transition now has explicit checks (counts, gaps, tails, refs, ordering, etc.).
- [ ] Add helper invariant functions when needed to replace manual output inspection.
- [ ] Split path-heavy suites into multiple top-level test functions and call them from `main`.
- [ ] Preserve or add compile-time validation with `constexpr` helpers + `static_assert(...)`.
- [ ] Keep runtime checks covering corresponding runtime paths.
- [ ] Extract unrelated coverage into dedicated test files when a monolithic file was split.
- [ ] Remove benchmark/performance sections from TAP test paths or move them out-of-band.
- [ ] For procedural integration tests with internal fail-fast helpers, emit TAP-visible final success/failure via `ZuCheck(...)`.
- [ ] Ensure the test still exits successfully on pass (`return 0;` or equivalent normal fallthrough).

If any checklist item is not satisfied, the migration is incomplete.
