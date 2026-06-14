## Follow-on: Replace `QPackEncoderTx` With Static QPACK Writers

### Summary
After the H3 session extraction in `plan.new.md`, replace the current virtual
`Zhttp::H3::QPackEncoderTx` write interface with a CRTP/static writer path.

This is intentionally not part of the primary plan. The primary plan first
makes H3 session ownership explicit and removes application-facing
`QPackEncoderTx` wiring. Once all QPACK encoder/decoder stream writes flow
through the reusable H3 session, the virtual boundary can be removed in a
smaller, focused refactor.

### Current Boundary
`Zhttp::H3::QPackEncoderTx` is currently defined as:

```c++
struct QPackEncoderTx {
  virtual ~QPackEncoderTx() = default;
  virtual bool write(ZuBSpan) = 0;
};
```

It is used by:

- `H3::Builder` through `qpackEncoderTx()` to write QPACK encoder-stream
  capacity and insert instructions.
- `H3::Parser` through `qpackDecoderTx()` to write QPACK decoder-stream section
  acknowledgements.
- `zhttpclient`, `Zhttp3InteropTest`, and `ZhttpQPackDynamicTest` through local
  subclasses or capture writers.

### Goal
Remove the virtual write interface from hot protocol code and replace it with
static dispatch through the H3 session or a small writer template.

The replacement should:

- preserve current dynamic QPACK behavior;
- preserve zero-capacity QPACK defaults;
- keep QPACK Tx mutation serialized on the owning transmit path;
- avoid application-visible QPACK writer plumbing;
- avoid C++ concepts and `requires`;
- use CRTP, SFINAE, or simple template parameters following Z Framework style.

### Candidate Design
After session extraction, H3 parsers and builders can depend on a session-owned
writer object instead of a virtual base:

```c++
template <typename Session>
struct QPackTx {
  bool writeEncoder(ZuBSpan span) {
    return static_cast<Session *>(this)->writeQPackEncoder(span);
  }

  bool writeDecoder(ZuBSpan span) {
    return static_cast<Session *>(this)->writeQPackDecoder(span);
  }
};
```

The H3 builder/parser contracts then move from:

```c++
QPackEncoderTx *qpackEncoderTx();
QPackEncoderTx *qpackDecoderTx();
```

to one of:

```c++
bool qpackEncoderWrite(ZuBSpan);
bool qpackDecoderWrite(ZuBSpan);
```

or:

```c++
auto qpackTx();
```

where the returned object is statically typed and exposes encoder/decoder write
methods.

### Implementation Phases

#### Phase 1: Centralize Existing Virtual Use
- Complete the main plan's H3 session extraction first.
- Confirm only H3 session internals and focused QPACK tests still mention
  `QPackEncoderTx`.
- Keep `ZhttpQPackDynamicTest` coverage for write failures and section
  acknowledgements.

#### Phase 2: Add Static Writer Path Beside Virtual Path
- Add static writer hooks to `H3::Builder` and `H3::Parser`.
- Use detector structs with `typename = void` and `decltype(..., void())` if a
  transition period needs both APIs.
- Prefer base defaults over scattered `if constexpr` capability checks where a
  side-effect-safe default is possible.

#### Phase 3: Switch H3 Session To Static Writer
- Update the reusable H3 session to provide static encoder/decoder write hooks.
- Remove the internal `QPackStreamTx : QPackEncoderTx` bridge from session code.
- Ensure writes still occur through the correct `Zquic` Tx path.

#### Phase 4: Remove Virtual API
- Remove `QPackEncoderTx` if no tests or compatibility constraints still need
  it.
- Remove `qpackEncoderTx()` and `qpackDecoderTx()` from H3 parser/builder CRTP
  defaults.
- Update tests to use static writer capture objects.

### Acceptance Criteria
- No production `zhttp` code uses virtual dispatch for QPACK encoder/decoder
  stream writes.
- H3 dynamic QPACK tests still pass, including capacity, insert, section-ack,
  and failure-path coverage.
- H3 interop tests still compile and pass where runtime prerequisites are
  available.
- Application and example code remains free of QPACK writer plumbing.
- QPACK Tx table mutation remains connection-affine and serialized on the
  owning send path.

### Non-goals
- Do not change QPACK compression policy.
- Do not make dynamic QPACK mandatory.
- Do not add runtime protocol type erasure.
- Do not introduce C++ concepts or `requires`.
- Do not combine this with the initial H3 session extraction.
