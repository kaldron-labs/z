`plan.md`: iterate and append numbered open questions and/or ambiguities to clarify requirements

>> resolve open questions

`plan.md`: update to prescribe implementation

`plan.md`: iterate and append numbered open questions and/or ambiguities to clarify implementation

>> resolve open questions

`plan.md`: act as a skeptical principal software engineer; scrutinize the intended implementation against the original `goal.md`:
- align each implementation element with `GUIDELINES.md`
  - align all names to naming rules
  - check intended implementation against all audit flags
  - add references to specific guidelines that apply in each case
- pressure test the need for every element
  - delete unnecessary elements
  - simplify the design as much as possible
- do not overengineer
- do not redundantly re-validate
- do not hand-code or reimplement Z framework capabilities, particularly:
  - do not hand-code formatting or parsing (e.g. JSON, numbers)
  - do not redundantly cast between convertible types
  - do not heap-allocate unnecessarily (use stack-allocated heap-fallback scratch capabilities)

`plan.md`: retain current build configuration:
- rebuild infrequently
  - batch source code changes into large batches
  - do not rebuild or retest until major milestones are reached
- rebuild incrementally, re-using current build artifacts
- other build configurations are out of scope
  - other compilers
  - other release/debug build flags
  - other platforms
- defer dependent rebuilding to the end of the implementation
- use valgrind sparingly:
  - to debug memory corruption as needed
  - as a final memory leak and corruption gate

`plan.md`: update for sliced phases, with interim acceptance criteria between each phase

`plan.md`: perform final review and update for internal consistency

`plan.md`: intentional breaking change:
- propagate to all dependents
- do not retain legacy compatibility

---

work unrestricted and autonomously

---

act as a skeptical principal software engineer
- audit the implementation against all `GUIDELINES.md` flags and guidelines
- scrutinize all new in-memory containers
  - can they be replaced by better code algorithms?
  - can they be made leaner or simpler?
  - can multiple containers be consolidated into fewer?
- scrutinize all copies and heap allocations
  - can the copy be elided?
  - should in-place mutation be used?
  - can the allocation be replaced by on-stack scratch storage with heap fallback, e.g. `ZtScratch`
- repair all findings

find performance impairments
  - unnecessary copying
  - unnecessary use of heap (where stack scratch would be better)
  - unnecessary validation
  - unnecessary hand-rolled code
    - where use of dependency Z framework capabilities would be better
    - particularly format conversions, printing, scanning, byte-swapping, etc.
  - unnecessarily complex algorithms
  - repeated evaluation of stable expressions
