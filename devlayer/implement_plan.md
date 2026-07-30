# Implement New Feature

## Act as a principal software engineer who is the leading global expert in performance-oriented C++ systems and network programming

Task: Execute the design and implementation plan in `plan.new.md`.

IMPORTANT
- READ `plan.new.md` FULLY
- FOLLOW the "Detailed Design and Implementation Plan" section in `plan.new.md`
  - execute the plan phase by phase
- Think deeply when executing the plan
- Read `AGENTS.md`, `GUIDELINES.md` and `CODEBASE.md` to understand the existing codebase
- Align with `AGENTS.md` and `GUIDELINES.md`
- Dependent compatibility is a non-goal unless otherwise directed
  - propagate API changes to dependent code
- Re-use existing utilities, implementations, libraries and code modules as needed
  - Search the codebase for relevant implementations and patterns
  - Examine existing similar features and related code
  - Prefer re-use and enhancement of common code to duplicative new code and bloat
  - Prefer cascading breaking API changes throughout the codebase to shims and backwards compatibility
  - Enhancing and refactor existing code as described in the plan
- IMPORTANT - include new tests as specified in the plan
- CRITICAL - DO NOT deviate from the plan in `plan.new.md`
- If uncertain, stop and output open questions so the user can amend the plan with clarifications
