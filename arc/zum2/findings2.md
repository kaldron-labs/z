# `zrest/src` pending-diff audit

The pending `zrest/src` diff is not yet principal-engineer clean. The audit
found three high-severity issues and several additional guideline violations.

## Findings

### 1. High: duplicate request routes are accepted and resolved inconsistently

`zrest/src/Zrest.hh:215` builds the exact-route hash without validating
uniqueness per HTTP method. Duplicate exact routes cause the later insertion
to win. Duplicate prefix routes passed to `ZuMatcher` select the first type,
making another request unreachable.

`RootCatalogValid` performs this check for roots; request catalogs need
equivalent validation separately for exact and prefix routes. `ExactReq` at
line 211 is likely intended for precisely this purpose.

### 2. High: invalid response-request indices incorrectly return response index zero

`zrest/src/Zrest.hh:301` calls the two-argument `ZuSwitch::dispatch`. When
`request >= Reqs::N`, it default-constructs the return value, producing `0`,
not `-1`.

This can happen when `MResParser::status()` subtracts one from a null
request-builder union type. The result can incorrectly instantiate the first
response type. `resMatch()` must use the overload with an explicit `-1`
default. `reqMatch()` should do the same for defensive API correctness.

### 3. Medium: root validation admits permanently unreachable routes

`zrest/src/Zrest.hh:350` checks only that the catalog is nonempty and root
types are unique. It accepts:

- an empty root, although `splitRoot()` rejects it;
- roots containing `/`;
- roots containing `?`;
- potentially other characters inconsistent with a single path component.

These should fail at compile time.

### 4. Medium: the hierarchy copies the complete `Zhttp::Target` at every routing level

Copies occur in `zrest/src/ZrestServer.hh:268` and
`zrest/src/ZrestServer.hh:373`. `Target` is a roughly cache-line-plus view
structure containing multiple spans and an `AuthorityView`. A root-routed
request currently copies it twice merely to replace `path`.

This is an amber-flag hot-path copy. Temporarily project and restore
`target.path`, or introduce a path-projection utility that avoids copying the
rest of `Target`.

### 5. High: `MReqRouter` duplicates almost the complete `MReqParser` forwarding layer

`zrest/src/ZrestServer.hh:381` repeats the header, body-info, body, completion,
and reset dispatch implemented at line 280. This violates the
near-identical-block red flag, multiplies template parsing and instantiation
burden, and creates two implementations of the same protocol-facing behavior
that must remain exactly synchronized. The implementations have already
diverged subtly; for example, their `complete()` pointer captures differ.

The common union-forwarding mechanics must be factored into a shared template
or base. This is high severity because it compromises implementation integrity
on the central request path and directly aggravates the compilation-resource
problem that the catalog work is intended to solve.

### 6. Low: unnecessary span conversions violate an explicit guideline

These should pass the spans directly:

- `zrest/src/Zrest.hh:258`: `lookup.findVal(ZuCSpan{path})`
- `zrest/src/Zrest.hh:380`: `lookup.findVal(ZuCSpan{root})`

Z span types are designed to interoperate without these conversions.

### 7. Low: dead declarations remain

- `ExactReq` at `zrest/src/Zrest.hh:211` is unused.
- `Paths` at `zrest/src/Zrest.hh:356` is unused.

`ExactReq` should either participate in catalog validation/table construction
or be removed.

### 8. Low: direct include requirements are not satisfied

The new code directly uses `ZuIntrin::log2` and `ZuUnroll`, but `Zrest.hh`
relies on transitive includes for both. The header should directly include
their defining headers. `ZuSwitch` is also directly used but transitively
included.

### 9. Low: the new compile-time assertion uses the wrong project idiom

`zrest/src/ZrestServer.hh:462` uses `static_assert`; `GUIDELINES.md` requires
`ZuAssert` for compile-time assertions.

## Areas that passed

- Root and exact dispatch use catalog-backed `ZmLHash`, not linear candidate
  comparisons.
- Hash tables use embedded static storage; no request-time heap allocation was
  introduced.
- `splitRoot()` aliases the input and performs no copying or allocation.
- Tagged unions use placement construction and deterministic destruction.
- Static local hashes are immutable after thread-safe initialization.
- No STL containers, concepts, broad lambda captures, polling, logging
  hazards, locks, garbage-collection scans, fixed raw arrays, or sharding
  violations were introduced.
- Struct member ordering is reasonable.
- Names remain within the repository length limit.
- Hard-tab formatting and `git diff --check` pass.
- The existing up-to-date `zrest` tests pass: two files and ten tests. The
  audit did not rebuild them.
