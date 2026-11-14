# SDLC Prompt Library

Use the installed Elephant-Goldfish Model (EGM) plugin for requirements, design,
planning, and document review. The Elephant keeps the project context and drafts;
Goldfish reviewers receive the document and selected evidence in fresh conversation
contexts. The author resolves trade-offs and approves the plan. This library adds
Z-specific constraints and the detailed implementation review below; use EGM's
workflows, templates, and review standards for the rest.

Invoke skills in Codex with `$egm-...` as shown below (or select the corresponding
skill in the skill picker). These are chat invocations, not shell commands.
Read the installed skill's `SKILL.md` and its `docs/codex.md` integration guidance;
discover their locations from the skill inventory rather than hard-coding a cache
version. Preserve EGM's readiness, section approval, and resolution gates.

Goldfish reviews must use fresh subagents: in Codex, set `fork_turns: "none"`
(or `fork_context: false` where that is the available schema). Give reviewers
explicit persona, standard, target, and supporting-source paths. Do not supply
the drafting conversation or simulate independent reviewers in the parent context.
Wait for completed reviews; report unavailable isolation or incomplete reviews.

Paths below are relative to the repository root. Read `AGENTS.md` and the relevant
repository and module guidance before work. Planning is research and document
editing only; implementation begins after the plan is approved.

Sources: the installed EGM skill instructions and
[upstream EGM](https://github.com/google/elephant-goldfish-model), especially its
[Codex integration](https://github.com/google/elephant-goldfish-model/blob/main/docs/codex.md).

## Discovery and research

Research the existing code and dependencies before making claims about them.
Document what exists, how components interact, and actual file/line references;
keep observed behavior separate from proposed changes. Refresh evidence instead
of relying on remembered code or stale documentation. Exclude ignored files.

When directory documentation needs to be established, use:

```text
$egm-bootstrap <target-subtree>
```

Use EGM's directory contract workflow with `target_filename=EGM.md` to preserve
existing READMEs and documentation. Consult the relevant EGM contracts and verify
them against current source code.

## Requirements analysis

```text
$egm-brainstorm Use goal.md as the goal. Apply devlayer/planning.md's project constraints and record the agreed requirements in concepts_brief.md.
```

Read `goal.md` fully. Bring existing `requirements.md`, `requirements.new.md`, and
`requirements.feedback.md` into the brief when present, preserving decisions and
important detail. Use EGM for clarification, alternatives, feasibility, scope,
and concept review; do not maintain a second requirements workflow alongside it.
Research comparable implementations and underlying API behavior as needed.

Keep numbered questions stable: append new numbers, record answers against their
original numbers, and never renumber resolved questions. Carry these records
forward into the design. Raise highly complex or infeasible requirements with
their technical reasons so the author can resolve them.

## Design and planning

### EGM drafting, review, and resolution

```text
$egm-draft concepts_brief.md
```

Use EGM's design template and section-by-section drafting workflow. Supply the
project constraints below, `goal.md`, relevant code, and any existing plan and
feedback as context. EGM normally writes `design_draft.md`. Once drafted, move
that document to `plan.md` before review and keep `plan.md` as the single working
design and implementation plan thereafter. When revising an existing `plan.md`,
target it explicitly rather than creating a competing draft. Do not overwrite an
existing plan without incorporating its decisions and details.

```text
$egm-review plan.md
$egm-resolve
```

Run EGM's software design review: initial zoom-out, comprehension, design critique,
intermediate zoom-out, and implementation readiness. Resolve the resulting
reports in the drafting conversation with `$egm-resolve`, identifying `plan.md`
and the actual report paths. Apply the author's resolutions, then re-review as
needed. Read `plan.feedback.md` if present. Preserve stable question numbers and
resolved answers through every revision.

For a focused review against selected standards, use:

```text
$egm-review-loop Review plan.md against the applicable repository guidelines and the project constraints in devlayer/planning.md; select the relevant reviewers and standards.
```

Use the installed skill's convergence and approval rules. Document review does
not replace the implementation audit below. Before implementation, obtain approval
of `plan.md`, with requirements, design, phased work, tests, acceptance criteria,
and resolved blocking questions all recorded in the document.

### Project constraints for EGM

Carry these constraints into the brief, draft, and reviewer reading lists:

- Explain how existing behavior will change. Identify impacted files, APIs,
  components, data flows, threads, timers, network behavior, and storage as relevant.
- Verify the actual behavior of every newly depended-on API against its intended
  use. Reuse and enhance existing Z utilities and test frameworks; factor shared
  requirements into common code. Use succinct names consistent with the repository.
- Sequence vertically sliced features and sub-features by their dependencies.
  Complete each sub-feature end to end within a phase rather than sequencing
  strictly by architectural layer. Identify any skeletons needed by later slices.
- Include implementation details, necessary code snippets, refactoring, new and
  modified files, and planned tests. Each slice must have handoff acceptance
  criteria, including guideline audits and repair of findings before proceeding.
- Include the build and test constraints from Verification and testing in the plan.
  Align verification milestones with slice handoffs and defer dependent rebuilding
  to the final milestone.
- Intentional breaking changes propagate throughout dependent code, tests, and
  documentation. Do not add compatibility shims or retain legacy APIs to reduce
  the change's reach. Carry this explicit non-goal into EGM's migration review.

### Planning and clarification workflow

`plan.md`: iterate and append numbered open questions and/or ambiguities to clarify requirements; question numbering is final - no renumbering when the document is further edited and questions are subsequently resolved

Resolve the numbered open questions with the user; retain their numbers and record the answers.

`plan.md`: update to prescribe implementation

`plan.md`: iterate and append numbered open questions and/or ambiguities to clarify implementation; question numbering is final - no renumbering when the document is further edited and questions are subsequently resolved

Resolve the numbered open questions with the user; retain their numbers and record the answers.

`plan.md`: act as a skeptical principal software engineer; scrutinize the intended implementation against the original `goal.md`; your goal is to simplify and delete redundant functionality, data structures, modules, algorithms and validations:
1. pressure test the need for every element
  - delete unnecessary elements
  - simplify the design as much as possible
  - do not overengineer
  - do not redundantly over-validate
2. align each implementation element with `docs/GUIDELINES.md`
  - align all names to naming rules
  - check intended implementation against all audit flags
  - add references to specific guidelines that apply in each case
  - prescribe correct use of Z framework, e.g. `ZtScratch`
3. do not hand-code or reimplement Z framework capabilities, particularly:
  - do not hand-code formatting or parsing (e.g. JSON, numbers)
  - do not redundantly cast between convertible types
  - do not heap-allocate unnecessarily (use stack-allocated heap-fallback scratch capabilities)

`plan.md`: update for sliced phases, with interim acceptance criteria between each phase

`plan.md`: perform final review and update for internal consistency

`plan.md`: intentional breaking change:
- propagate to all dependents
- do not retain legacy compatibility

### Protocol requirements and validation

`plan.md`: apply Postel's Law:
- negative protocol standards conformance is a non-goal (testing for failures and diagnostics on invalid inputs)
  - invalid input processing and validation:
    - invalid inputs should be handled gracefully
    - invalid inputs should not crash the receiver or present a security risk (buffer overruns, etc.)
      - completely corrupt inputs should result in connection/session/stream force-close with
        no diagnostic send to peer (DoS protection)
    - valid but unsupported inputs should be handled conformantly
- positive protocol interoperability and standards conformance is a goal
  - testing for correct outputs that are consumable by other implementations
  - correctly ingesting inputs from other implementations
- scrutinize all validation requirements and **delete unnecessary validations**
  - explicitly prohibit unnecessary validations so the ensuing implementation does not inadvertently implement them

## Implementation

Work unrestricted and autonomously within the authorized task.

### Implement New Feature

#### Act as a principal software engineer who is the leading global expert in performance-oriented C++ systems and network programming

Task: Execute the design and implementation plan in `plan.md`.

IMPORTANT
- READ `plan.md` FULLY
- FOLLOW the "Detailed Design and Implementation Plan" section in `plan.md`
  - execute the plan phase by phase
- Think deeply when executing the plan
- Read `AGENTS.md`, `docs/GUIDELINES.md` and relevant EGM contracts to understand the existing codebase
- Align with `AGENTS.md` and `docs/GUIDELINES.md`
- This is an intentional breaking change: do not retain legacy compatibility
  - propagate API changes to dependent code
- Re-use existing utilities, implementations, libraries and code modules as needed
  - Search the codebase for relevant implementations and patterns
  - Examine existing similar features and related code
  - Prefer re-use and enhancement of common code to duplicative new code and bloat
  - Prefer cascading breaking API changes throughout the codebase to shims and backwards compatibility
  - Enhancing and refactor existing code as described in the plan
- IMPORTANT - include new tests as specified in the plan
- CRITICAL - DO NOT deviate from the plan in `plan.md`
- If uncertain, stop and output open questions so the user can amend the plan with clarifications

## Verification and testing

### Build and test constraints

Apply these constraints during implementation and verification. Include them in the plan.

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

### Optimize and Correct Design and Implementation

#### Act as a principal software engineer who is the leading global expert in the field

Task Steps:
1. Read `plan.md`
2. Use version control to understand the code changes associated with implementing `plan.md`
3. SCRUTINIZE the modified modules, functions and logic
4. AUDIT code against `docs/GUIDELINES.md`, checking all audit flags: repair any findings
5. UNIFY repeated code/logic by factoring out common code:
  - move common code to the most appropriate source file based on the structure and conventions of the existing codebase
6. SIMPLIFY and shorten redundant/complex logic such as:
  - highly nested if statements
  - repeated code blocks that are near-identical
7. DELETE legacy and redundant code that is no longer needed
8. Ensure test coverage of the new code
9. Correct any regressions introduced

### Detailed implementation audit

act as a skeptical principal software engineer
- audit the implementation of `plan.md` against **all** `docs/GUIDELINES.md` flags and guidelines
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
  - follow `docs/GUIDELINES.md` guidance in this regard
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

## Release and deployment

The source library contains no release or deployment prompt. Define project-specific
release instructions when this phase is needed.

## Maintenance

For follow-up corrections and optimization, use the verification and testing audit
prompts against the relevant `plan.md` work. For changed goals, return to discovery,
requirements and planning before implementation.

Use `$egm-drift-report <design-document-directory>` to compare maintained design
documents with current code. Scope it to the relevant directory and resolve
reported differences deliberately. Use `$egm-review plan.md` for revised designs
and the full implementation audit above for code changes.
