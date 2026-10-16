# `zum/src` guideline audit

Scope: all repository-owned `*.cc` and `*.hh` files under `zum/src`, checked
against `GUIDELINES.md`. Generated FlatBuffers headers, generated dependency
files, objects, and libtool artifacts were excluded. This is an audit only;
no source changes were made.

## Findings

### 1. Amber: request and database limits are duplicated as unexplained literals

The HTTP parser types embed separate compile-time limits in
`zum/src/zumd_http.hh:202,224,249,276,337,360,386,430,455` and
`zum/src/zumd_daemon.hh:125,212-213`, while the service layer exposes the
tunable `ServerLimits` policy in `zum/src/zumd_server.hh:75-87` and validates
against it in `zum/src/zumd_server.cc`. Raising a configured limit above the
parser literal cannot work; lowering it leaves two independent bounds to keep
consistent. Additional unlabelled bounds occur in
`zum/src/zumd_db.cc:256,317` (`129`/`65` plus overflow checks),
`zum/src/zumd_daemon_admin.cc:2411` (`ScanSize = 1001`), and
`zum/src/zumd_daemon_admin.cc:2593` (`64U<<10`).

These are the fixed-capacity/workload amber flags. Consolidate policy limits
where they are configurable, or explicitly name and document immutable
protocol/overflow ceilings. Keep the one-extra-row semantics visible in the
constant name or comment.

### 2. Red: explicit iterator survives beyond its logical traversal

`zum/src/zumd_oidc.cc:717` declares `auto i = self->m_pending.iter()` and
then continues with `m_http` and `m_clock` mutations after the traversal.
The iterator's scope therefore extends beyond the operation that needs it;
depending on the container implementation it may retain a lock or pin state
while unrelated cleanup runs. Put the traversal in its own block, or otherwise
end the iterator before the following state changes.

### 3. Red: repeated invariant array access in authentication checks

`zum/src/ZumJWTVerify.cc:161` and `zum/src/zumd_jwt.cc:267,338` read
`claims.amr[0]` twice in the same predicate. Cache the method once and compare
the cached value. `zum/src/zumd_core.cc:20` similarly reads `name[begin]`
twice in one loop condition; cache the character. The slash-trimming loops
at `zum/src/zumd_discovery.cc:73,277` also call `length()` repeatedly while
testing and indexing the same current string; cache the current length for
each iteration.

### 4. Red: named enum declarations do not use the project enum idiom

Named `enum T` declarations remain at:

- `zum/src/zumd_oauth.hh:71,104,127` (`Field`);
- `zum/src/zumd_daemon_admin.cc:571` (`QueryInput::Field`);
- `zum/src/ZumService.cc:234` (`ServiceState::State`); and
- `zum/src/zumd_sagas.hh:1972,2073` (`RoleEdit::Kind`, `ScopeEdit::Kind`).

`GUIDELINES.md` explicitly forbids `enum T`. Use the repository form
`struct E { enum { ... }; };` when a scoped type-like grouping is needed, or
an appropriately named namespace for constants, then update uses consistently.

### 5. Low: compile-time assertion uses the non-project assertion form

`zum/src/ZumMgmt.cc:117` uses `static_assert(routes.length() == MgmtOp::N)`.
The guideline assertion convention requires `ZuAssert` for compile-time
assertions. Replace it with `ZuAssert(...)`.

### 6. Amber: enum-to-route metadata is a fixed lookup table

`zum/src/ZumMgmt.cc:21-50` stores the complete enum-to-method/path mapping in
a fixed `ZuArray`, then indexes it at line 236. This is the fixed lookup-table
flag and its capacity is tied directly to `MgmtOp::N`. Prefer the prescribed
`ZuSwitch`/compile-time mapping, or document why this immutable route metadata
table is the required exception and keep the enum/table relationship checked.

### 7. Low: `SagaImage` relies on transitive includes for direct types

`zum/src/zumd_saga_image.hh:14-25` directly uses `Zfb::IOBuilder`,
`ZfbStruct`, and `ZiIOBufAlloc`, but includes only `ZumLib.hh` and
`zumd.hh`; the latter supplies the FlatBuffers and I/O-buffer definitions
indirectly. Add the defining headers directly (and remove the broad
`zumd.hh` include if no domain type requires it). This keeps the header DAG
idempotent and prevents unrelated changes from breaking the template.

## Areas checked and not found

- No C++ concepts/`requires`, `enum class`, anonymous namespaces, STL
  containers, `FILE`/`syslog`, `printf`, hand-rolled hash functions, polling,
  manual sleeps, or raw `malloc`-family allocation were found.
- Allocations in the library paths use `ZumObject`/`ZmVHeap`, `ZmHeap`-backed
  Z JSON nodes, Zdb rows, or pooled I/O buffers; no untracked heap allocation
  or buffer/queue management bypass was identified.
- No GC scans, cache-line-sharing declarations, dead compatibility blocks,
  broad `ZiLOG` reference captures, scheduler `run`/`invoke` blocking
  assumptions, or obvious cross-shard ownership violations were found.
- Reference-counted asynchronous operation state is retained deliberately by
  self-captures; callbacks use copied values for cross-shard work. The
  reference captures inspected did not form an owner cycle.
- Fixed-size cryptographic buffers and FlatBuffers builder storage were
  treated as protocol/integration storage, not unexplained workload arrays.
- Structs, naming, hard-tab formatting, and header skeletons are generally
  consistent. `git diff --check` passes for the current tree.

## Verification

`make -C zum/src -j8` completed with no work required in the existing build
tree. A forced `make -B -C zum/src -j8` did not reach source compilation: the
parallel recursive `am--refresh`/configure step raced and failed with
`configure: cannot run C++ compiled programs`. A full clean rebuild and test
run were not performed by this audit.
