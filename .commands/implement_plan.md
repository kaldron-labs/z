# Implement New Feature

Execute the design and implementation plan in `plan.new.md`.

IMPORTANT
- READ `plan.new.md` FULLY
- FOLLOW the "Detailed Design and Implementation Plan" section in `plan.new.md`
  - execute the plan phase by phase
- Think deeply when executing the plan
- Read `AGENTS.md` and `CODEBASE.md` to understand the existing codebase
- Conform to existing coding style, conventions and naming conventions
  - PREFER succinct expressive names to verbose names
  - PREFER succinct expressive code to verbose code
  - PREFER modern natural code as used by programmers who are fluent in the language
  - USE trailing underscores to avoid naming collisions with inner-scoped locals
  - in Javascript catch `error`, not `err` or `e`
  - do not wrap single-line if statements in a { } block
- Conform to existing frameworks and libraries including test frameworks
- Re-use existing utilities, implementations, libraries and code modules as needed
  - Search the codebase for relevant implementations and patterns
  - Examine existing similar features and related code
  - Prefer re-use and enhancement to duplicative new code and bloat
  - Enhancing and refactor existing code as described in the plan
- IMPORTANT - include new tests as specified in the plan
- CRITICAL - DO NOT deviate from the plan in `plan.new.md`
- If uncertain, stop and output open questions so the user can amend the plan with clarifications
