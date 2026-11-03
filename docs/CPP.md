# C++ language and portability

Part of the repository guidance mapped in [AGENTS.md](AGENTS.md).

## Use of C++ language
### Core language
- Compile as GNU C++2b
- DO NOT USE:
  - C++ concepts or `requires`
  - anonymous namespaces in `.cc` files (use file-scope `static`)
  - non-specific capture packs `[&]` or `[=]`
  - `enum class` or `enum T`
  - constant macros like `UINT64_C(x)` (use `uint64_t(x)` instead)
- Use advanced C++ where it is expressive and efficient
  - Where C and C++ offer the same facility, prefer the C form:
    - Example: `#include <string.h>`, not `<cstring>`.
    - Where C99 conflicts with C++, use the GNU C++2b form.
- Do not define pure forwarding functions to bases that do not add any logic, use `using` declarations to hoist the base definition into the derived class
- Use `ZuLib.hh` functions in preference to STL, in particular:
  `ZuAssert`, `ZuMv`, `ZuFwd`, `ZuDecay`, `ZuDeref`, `ZuStrip`, `ZuIfT`, `ZuIsConvertible`, `ZuIsConstructible`, `ZuLaunder`, `ZuPun`, `ZuCanOverlap`

### Primitive integer types
- use `int` and plain unadorned `unsigned` as the primary local variable integers
  - the following are static asserted in the foundational Z header `ZuLib.hh`:
    - `sizeof(int) == sizeof(unsigned)`
    - `sizeof(int) >= sizeof(int32_t)`
    - `sizeof(unsigned) >= sizeof(uint32_t)`
- the principle is that `int` and `unsigned` will naturally match the CPU's registers
- where 64bits is required, use `int64_t` and `uint64_t`
- for types that might be stored in high volumes, i.e. where memory pressure may be significant, use the smallest type that spans the required range of values:
  - enums are often `int8_t` (they must be signed)
- do NOT unnecessarily assert, for example checking that a `uint32_t` is `<= UINT_MAX`

### Static polymorphism and constraints
- Prefer CRTP, templates, and compile-time dispatch over virtual polymorphism.
- Use templates and CRTP to factor common code; keep logic DRY without adding weak abstractions.
- Prefer deduced `auto` return types for functions that merely return a cast, especially CRTP accessors: `auto impl() { return static_cast<Impl *>(this); }` rather than spelling `Impl *`.
- Express constraints with SFINAE and detector traits.
  - Keep function return types free of SFINAE expressions; put the test in a trailing `typename = ...` template parameter to avoid encoding it in every symbol.
  - Consolidate overloads that would otherwise become redeclarations with `if constexpr`, preserving their combined participation constraint and result types.
  - Generic hidden friends from different class specializations need a class-specific template discriminator; defaulted type constraints alone do not distinguish their declarations.
  - Prefer `typename = void` plus `decltype(CODE, void())` specializations to test whether `CODE` is well-formed.
  - Do not replace established `ZuIfT`/detector idioms with concepts.
  - Prefer SFINAE in function template parameters to return types, because templated return types bloat the symbol table and debug information
- For optional CRTP callbacks, prefer side-effect-safe base defaults and direct calls such as `impl()->callback(...)`.
  - Defaults should be harmless: `return true`, `return nullptr`, or no-op.
  - Use `if constexpr` traits only when a safe base default cannot express the behavior.
- Control inherited CRTP interfaces with access-changing `using` declarations.
  - Hide a base function with `private: using Base::X;` when the derived class encapsulates it and dependent callers must not access it directly, or when the derived class intentionally replaces it in the public interface with a new function of the same name.
  - Hoist a base function into the public interface with `public: using Base::X;`.

### ADL tagging
- Use friend functions and ADL to tag types and expose compile-time metadata.
  - Example: `friend ZuStructPrint ZuPrintType(DB *)`; `decltype(ZuPrintType(...))` lets `ZuPrint` choose formatting.
  - Example: `ZuFields_(O *, Facet *)` lets `ZuStruct` discover fields.

### Style
- Write idiomatic, natural, maximally expressive code for veteran C++ engineers.
- Prefer brevity, expert idioms, and precise structure over readability for novices.
- Do not disdain "Hacker's Delight" style when it is clear, correct, and faster.
- Use shifts for integer multiplication and division by power-of-two literals:
  `x<<3` instead of `x*8`, and `x>>3` instead of `x/8`. For signed division,
  shift only when the value is nonnegative, since rounding otherwise differs.
- Do not put spaces on either side of `/` in division expressions: write `x/y`.
- `auto ptr = ...` not `auto *ptr = ...`
- Strongly prefer `T(u)` to `static_cast<T>(u)` in all cases where they're equivalent

### Portability and compiler features
- Target gcc and clang; use `__GNUC__`, intrinsics, `int128_t`, and `uint128_t` where appropriate.
- Target Linux and Windows via msys2/mingw 64-bit; use `_WIN32` for Windows-specific paths.

### Enums
- do not use `enum class` or `enum T`
  - Z **intentionally** uses enums as fully convertible with integers
    - enum values should be directly usable as compile-time array indexes, etc.
    - if a type is integral and size <= `sizeof(int)`, enum values are used in preference to `static constexpr` constants
  - if the enum needs to be type-aliased by dependents, use `struct E { enum { ... } };`
  - otherwise use `namespace E { enum { ... } };` because this permits dependents to `using namespace E`
  - above `zm` use `ZtEnum`:
    - if an external API type
    - if callers need name lookup at run-time

### Constant literals
- when a constant literal is integral or boolean and does not need to be wider than `int`, use `enum { X = 42 };` in favor of `static constexpr T = 42`
- Declare a compile-time string as `constexpr auto name = "text"_z`; do not
  declare a `constexpr ZuSpan`/`ZuCSpan`. `ZuString` preserves the literal's
  extent in its type, whereas a span is a non-owning run-time view. For a
  heterogeneous collection of literals, use a `constexpr` array of C-string
  pointers and construct a span only at the use boundary.

## Use of STL and other dependencies
- minimize use of STL, BUT
- use `iostream`, `std::cout`, `std::cerr`, `std::flush`, etc.
  - `ZuTestUtil` dependents use `log` for test diagnostics
  - do NOT use `std::cerr` for logging errors when `ZiLog` is available
  - do NOT use `Zi::stdErr()` or `Zi::stdOut()` in preference to `std::cout` or `std::cerr`
    - `Zi::std...` are only to be used for redirection when a `ZiFile` type is required
- use `Zu` alternatives to STL: example: `ZuIfT` instead of `enable_if`
- maximally leverage the Z framework foundation libraries:
  - `zu`, `zm`, `zt`, `zf`, `ze`, `zi`

## Windows
- MSVC/Intel compatibility is a non-goal
- Targets are gcc and clang under MinGW / MSYS2, 64bit only
- do not use the `*W()` variant names, the build is always `wchar_t`-enabled:
  - use `GetTempPath`, not `GetTempPathW`
