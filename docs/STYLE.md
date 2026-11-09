# Code layout and naming

Part of the repository guidance mapped in [AGENTS.md](AGENTS.md).

## Code style
### Header layout
- Library headers must follow this skeleton (`[header]`: `ZuString`, `ZhttpQPack`; `[component]`: `Zu`, `Zhttp`):
  ```
  //  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
  //  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

  // (c) Copyright [year] [author]
  // This code is licensed by the MIT license (see LICENSE for details)

  // [title and brief description of header]

  #ifndef [header]_HH
  #define [header]_HH

  #ifndef [component]Lib_HH
  #include <zlib/[component]Lib.hh>
  #endif

  [lowest-level includes, e.g. system library includes]

  [next-lowest-level includes, e.g. Zu includes]

  [next-lowest-level includes, e.g. Zt includes]

  [same-level component includes]

  [body of header]
  ```
- Headers must include all direct dependencies.
- Headers must not include indirect dependencies unless they use related code/definitions.

### Indentation
- Match prevailing style in `{zu,zm,zt,ze,zi}/src/*.{hh,cc}`; when these rules, editor `cino`, and local precedent disagree, code precedent wins.
- Use hard tabs for indentation (`noet`, `ts=8`) and a 2-column logical C++ indent (`sw=2`); do not replace leading tabs with spaces.
- Indent class/struct/function bodies, control-flow bodies, and lambda bodies one logical level.
- Namespaces:
  - Large namespace bodies (>20 lines):
    - not indented
    - closing braces are commented with `// [namespace]`
  - Small namespace bodies (<= 20 lines):
    - indented
    - closing braces are uncommented
- Keep `public:`, `protected:`, and `private:` flush with the class declaration body; indent following members one logical level.
- Keep `friend` declarations flush with the class declaration body, at the top in the default private section.
- Follow local switch style for `case`/`default`; indent statements one logical level from the label.
- For a braced `case`/`default` body, place its terminating `break` after the closing brace on the same line (`} break;`), not inside the braces on a separate line.

### Multiline layout
- Wrap source code at 98 columns, counting hard tabs at tab stops of 8.
- Prefer existing visual alignment over mechanical fixed-width continuation indents.
- Split long template parameter lists, inheritance lists, base constructor calls, and function argument lists after natural delimiters.
- Continue nested template/base/member-initializer expressions one logical level deeper than their containing line.
- Align continuation lines with the open construct when nearby code does.
- Function multiline style, with `{` on its own line:
  ```
  template <typename Client>
  int run(
    ZiMultiplex &mx, const Options &options, URL url,
    RequestResult *result = nullptr)
  {
    Client client;
  ```
- Constructor multiline style, with `:` and `{` on their own lines:
  ```
  StoreTbl(
    Store *store, IDString id, unsigned nShards,
    ZfVFieldArray fields, ZfVKeyFieldArray keyFields,
    const reflection::Schema *schema, IOBufAllocFn bufAllocFn)
  :
    m_store{store}, m_id{ZuMv(id)},
    m_fields{ZuMv(fields)}, m_keyFields{ZuMv(keyFields)},
    m_bufAllocFn{ZuMv(bufAllocFn)}
  {
    // introspect fields and flatbuffers reflection data, building
  ```
- Do not deeply indent lambdas; large multiline captures/parameters use this shape:
  ```
  auto lambda = [
    ... /* multiline captures */
  ](
    ... /* multiline parameters */)
  {
    ... /* body */
  };
  ```

### Compact layout and alignment
- Keep short, simple functions and statements on one line when surrounding code does; split only when line length or expression shape makes it clearer.
- Preserve tabular alignment for data members, macro bodies, and compact tables already using tabs to align names, initializers, or comments.
- Vertically align `ZfStruct` field typecodes with hard tabs;
  wrap long properties at natural delimiters to keep declarations within 98 columns.
- Vertically align data member names with hard tabs.
- Do not let a verbose member type defeat tabular alignment. Give a complex or long member type a short local `using` alias when that keeps the member declaration aligned; place that alias immediately before the data declarations that use it.
- `*` and `&` go with the member, not the type: `void<TAB>*m_`, not `void *<TAB>m_`.

### Class and struct shape
- `struct`s are all-public data: members are not prefixed with `m_` and appear at the top, before constructors and other function members; required local `using` declarations may precede them.
- Put directly shared, unprefixed data at the top of its composite type before member functions; required local `using` declarations may precede it.
- `class`es have all-private data: members are prefixed with `m_` and appear in a trailing `private:` section at the bottom before closing `};`.
- Do not create hybrid `class` or `struct` types with mixed public/private data members.
- `m_` marks state owned and encapsulated by its class. Only the owning class's implementation accesses it directly; friendship does not relax this rule. Direct base/derived implementation access is the narrow exception.
- Model state shared at field granularity as an unprefixed member of a `struct`, not as a friend's view into a class's `m_` state. If a type needs both shared data and encapsulated state, put the former in a `...Data` struct or expose the latter through member functions.

## Naming
### Length and abbreviations
- Names must be concise; 28 bytes is the hard upper limit.
- Use industry standard in-code abbreviations for long names:
  - `receive` -> `rx`
  - `transmit` -> `tx`
  - `reserve` -> `res`
  - `recovery` -> `rec`
  - `request` -> `req`
  - `receive` -> `recv`
  - `event` -> `evt`
  - `client` -> `cli`
  - `server` -> `srv`
  - `connection` -> `cxn`
  - `packet` -> `pkt`
  - `protection` -> `prot`
  - `security` -> `sec`
  - `generation` -> `gen`
  - `version` -> `ver`
  - `original` -> `orig`
  - `previous` -> `prev`
  - `parameter` -> `param`
  - `interface` -> `if`
  - `implementation` -> `impl`
  - ... and so on (this is not an exhaustive list)
- Example: `packet` is fine; `reservePacketProtection` is too long; use `resPktProt`.
- use "ack" and "ackd", not "ackd"; example: `packetAckd`
- use "nak and "nakd", not "nack", "nackd" or "nacked"; example: `packetNakd`
- do not prefix or namespace file-scoped `static` functions in `.cc` files:
  - use short meaningful names, e.g. in `ZiIP.cc`: `pton4` not `ZiIP_pton4`
- do not use ambiguous or obfuscated abbreviations:
  - bad: `bytesInFlight` -> `bif`: `if` is typically read as `interface`
  - bad: `congestionBytes` -> `congBytes`: `cong` is a non-standard and counter-intuitive abbreviation
- elide redundant words in names:
  - `runtimeDiag` -> `diag`: "runtime" is implied, delete it
  - `Engine::stopEngine` -> `stop`: "engine" is implied - this is a member function of `Engine`

### Casing and member prefixes
- Names are generally camel-case, not snake-case, but there are numerous exceptions.
- Repository-owned library/component basenames in `src` and unit-test
  code/data basenames in `test` are CamelCase and use the module prefix, e.g.
  `ZmLib`, `ZmTest`, and `ZhttpParserTest`.
- Programs built in `src` have succinct lowercase command-line names. Their
  program-specific source and resource basenames are also lowercase, e.g.
  `zcmd`, `zdash`, `ztotp`, and `zwsd`.
- C compatibility-layer headers and implementations intended for direct use by
  C applications retain their snake-case C API names, e.g. `zu_bitmap.h` and
  `zu_bitmap.cc`.
- Repository-owned code/data basenames in `util`, `itest`, `example`, `bench`,
  and `interop` are lowercase.
- Flabuffers schema files are snake-case.
- Conventional control filenames such as `Makefile.am`, `README.md`,
  `Dockerfile.*`, and `.gitignore` retain their conventional casing.
- Native names mandated by an external tool or protocol retain their spelling;
  this includes PostgreSQL extension/control/SQL names, generated files,
  imported fixtures, and FlatBuffers schema filenames and contract fields.
  FlatBuffers schemas use their established lowercase/underscore basenames;
  keep the corresponding generation rules and generated-header names aligned.
  Do not apply this exception to ordinary repository-owned C++ files.
- `m_` is reserved for encapsulated private data members of classes; see the class-shape ownership rule above.

### Accessors
- Use overloads for getters/setters; do not invent separate names.
- Example: `int x() const` and `void x(int)`.
- `decltype(auto) x(this &&self) { ... }` is acceptable to preserve move context.
- `int &x()` as a setter is an amber flag; use it only when thread-safety is guaranteed by the calling context. If direct mutation is intended, `int x` should probably be a public data member.
- When an outer fluent parameter type contains or extends another fluent
  parameter type, preserve inline configuration with the
  `ZiMxParams::scheduler()` pattern: provide accessors for the inner/base
  parameters and a same-named lambda overload that applies the lambda and
  returns the outer type as an rvalue. Do not forward every inner/base setter,
  and do not permit an inherited setter's narrower return type to decay an
  inline expression to the base type.

### Trailing underscores
- Use trailing underscores only to distinguish related wrapper and implementation names:
  - generically for wrapped functions and macros; wrapper chains may add one underscore per layer: `x` wraps `x_`, which wraps `x__`.
  - to distinguish internal-only functions/types from external functions, that are primarily wrappers of the internal function.
  - to distinguish thread-dedicated functions/types from those that are callable from any thread, that are primarily wrappers of the thread-dedicated function.
- Example: `send` may `txInvoke` a `send_` call on the Tx thread; `send_` can assume the correct thread and elide the check/context switch.
- Example: `template <typename T> struct Foo : public Foo_ { ... };` where `Foo` is the API and `Foo_` is a `T`-independent common base.
- Use numbered suffixes `_0`, `_1`, ... for multi-step functions and variable-argument macros, e.g. `stop_0`, `stop_1`, ...; do not use repeated trailing underscores to label steps or argument counts.
- When repairing names, remove unjustified trailing underscores and update all affected declarations and uses; do not introduce a wrapper merely to justify an underscore.

### Vocabulary
- Prefer precise project vocabulary: "utilities" not "helpers", "duplex" not "bidi", etc.
- Do not choose imprecise or primitive English for accessibility to non-native, non-technical, or non-veteran readers; accessible naming is a hard non-goal.

### Standard function name vocabulary
- `push()` - appends a value at tail
- `unshift()` - prepends a value at head
- `pop()` - removes a value from tail
- `shift()` - removes a value from head
- `add()` - inserts a value
- `del()` - deletes a value
- `find()` - finds a value
- `iter()` - returns an iterator (`citer()` for const)
- `riter()` - returns a reverse iterator (`criter()` for const)
- `clear()`/`clr()` - clears to a default initial state
  - containers: clears certain contained values
- `clean()` - removes and frees **all** contained data
- `reset()` - both `clear` and `clean`
- `null()` (as a mutable member function) - same as `reset` for objects with a semantic "null" value, with the postcondition that the object's value is "null"
- `init()` - initializes an object post-constructor
- `final()` - finalizes an object pre-destructor
- `start()` - start running (should be idempotent)
- `stop()` - stop running (should be idempotent)
- `operator *()` - test for "null" sentinel (used with non-pointer types)
