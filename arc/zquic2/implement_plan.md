# Implement New Feature

## Act as a principal software engineer who is the leading global expert in performance-oriented C++ systems and network programming

Task: Execute the design and implementation plan in `http3_qpack.new.md`.

IMPORTANT
- READ `http3_qpack.new.md` FULLY
- FOLLOW the "Detailed Design and Implementation Plan" section in `http3_qpack.new.md`
  - execute the plan phase by phase
- Think deeply when executing the plan
- Read `AGENTS.md` and `CODEBASE.md` to understand the existing codebase
- Conform to existing coding style, conventions and naming conventions
  - PREFER succinct expressive names to verbose names
  - PREFER succinct expressive code to verbose code
  - PREFER modern natural code as used by programmers who are fluent in the language
  - USE trailing underscores to avoid naming collisions with inner-scoped locals
  - do not wrap single-line if statements in a { } block
  - code for veteran engineers who are expert in the language and steeped in its conventions
  - USE idiomatic, natural and maximally expressive code:
    - PREFER brevity, expressiveness and often-used idiomatic expressions of the programming language to readability
- Conform to existing frameworks and libraries including test frameworks
- Re-use existing utilities, implementations, libraries and code modules as needed
  - Search the codebase for relevant implementations and patterns
  - Examine existing similar features and related code
  - Prefer re-use and enhancement of common code to duplicative new code and bloat
  - Prefer cascading breaking API changes throughout the codebase to shims and backwards compatibility
  - Enhancing and refactor existing code as described in the plan
- IMPORTANT - include new tests as specified in the plan
- CRITICAL - DO NOT deviate from the plan in `http3_qpack.new.md`
- If uncertain, stop and output open questions so the user can amend the plan with clarifications
