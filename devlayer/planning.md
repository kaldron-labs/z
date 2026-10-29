`plan.md`: iterate and append numbered open questions and/or ambiguities to clarify requirements; question numbering is final - no renumbering when the document is further edited and questions are subsequently resolved

>> resolve open questions

`plan.md`: update to prescribe implementation

`plan.md`: iterate and append numbered open questions and/or ambiguities to clarify implementation; question numbering is final - no renumbering when the document is further edited and questions are subsequently resolved

>> resolve open questions

`plan.md`: act as a skeptical principal software engineer; scrutinize the intended implementation against the original `goal.md`; your goal is to simplify and delete redundant functionality, data structures, modules, algorithms and validations:
1. pressure test the need for every element
  - delete unnecessary elements
  - simplify the design as much as possible
  - do not overengineer
  - do not redundantly over-validate
2. align each implementation element with `GUIDELINES.md`
  - align all names to naming rules
  - check intended implementation against all audit flags
  - add references to specific guidelines that apply in each case
  - prescribe correct use of Z framework, e.g. `ZtScratch`
3. do not hand-code or reimplement Z framework capabilities, particularly:
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
- audit the implementation of `plan.md` against **all** `GUIDELINES.md` flags and guidelines
- scrutinize all new in-memory containers
  - can they be replaced by better code algorithms?
  - can they be made leaner or simpler?
  - can multiple containers be consolidated into fewer?
- scrutinize all transforming/parsing/building/formatting/marshalling/unmarshalling
  - are any of these operations redundantly repeated?
  - can any operations be elided by retaining and re-using outputs?
  - can mutations be performed in-place (overwriting) to avoid unnecessary copying?
  - do these operations make use of the correct Z framework capabilities?
- scrutinize all copies and heap allocations
  - can the copy be elided?
  - should in-place mutation be used?
  - can the allocation be replaced by on-stack scratch storage with heap fallback, e.g. `ZtScratch`
- repair all findings
- limit scope to `plan.md` work

---

scrutinize link, session and connection state (member data):
- are any counters, values etc. duplicative of state maintained in dependencies or in lower layers?
- can member data be simplified or consolidated?
- member data must be clearly marked as one of:
  - immutable: stable following initialization
  - rx-owned: only mutated by the rx thread once initialized
  - tx-owned: only mutated by the tx thread once initialized
  - shared: contended data protected by a lock or atomic, shared by multiple threads
- sharded data access must be consistently respected and asserted
  - cross-sharded access/mutation of rx- or tx-owned data is a fatal error
  - unclean snapshots are permitted for telemetry, logging, etc.
- evaluate if shared/contended data should be rx- or tx-owned and sharded:
  - shared/contended data should be minimized
  - follow `GUIDELINES.md` guidance in this regard
- rx-owned, tx-owned and shared member data members should be:
  - grouped together largest to smallest
  - cache-line aligned in the usual way (refer to existing examples)
- check inheritance, MI and containing of cache-line-aligned data
  - rx-owned, tx-owned and shared cache-line-aligned member data must NOT be inherited or contained by a derived class that **also** declares its own rx-owned, tx-owned or shared cache-line-aligned groups of member data
  - in such cases, member data groups must be flattened and consolidated using appropriate techniques, e.g. CRTP

find performance impairments
- unnecessary copying
- unnecessary use of heap (where stack scratch would be better)
- unnecessary validation
- unnecessary hand-rolled code
  - where use of dependency Z framework capabilities would be better
  - particularly format conversions, printing, scanning, byte-swapping, etc.
- unnecessarily complex algorithms
- repeated evaluation of stable expressions
