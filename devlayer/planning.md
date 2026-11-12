# SDLC Prompt Library

Select the phase and task needed for the current work. These are reusable prompts;
reading this library does not invoke every task. Paths in prompts are relative to
the repository root.

This file consolidates the former prompts in this directory. Where they conflicted,
the original `planning.md` took precedence: use `plan.md` throughout design, implementation and
audit; preserve numbered questions and their resolutions; propagate intentional
breaking changes; and retain the current build configuration.

- [Discovery and research](#discovery-and-research)
- [Requirements analysis](#requirements-analysis)
- [Design and planning](#design-and-planning)
- [Implementation](#implementation)
- [Verification and testing](#verification-and-testing)
- [Release and deployment](#release-and-deployment)
- [Maintenance](#maintenance)

The artifact flow is `goal.md` → `requirements.md` → `requirements.new.md` →
`plan.md` → implementation and verification. Refresh `docs/CODEBASE.md` through
discovery as needed. Requirements revisions use `requirements.feedback.md`; plan
revisions use `plan.feedback.md`. Re-read artifacts before revising them.

## Discovery and research

### Research Codebase

#### Act as a highly experienced software documentarian, not an evaluator

Task: Conduct comprehensive research across the codebase and generate an output research report in `docs/CODEBASE.md`.

#### CRITICAL: DO NO MORE THAN DOCUMENT AND EXPLAIN THE CODEBASE AS IT EXISTS TODAY

- Document what IS, not what SHOULD BE
- Always read files FULLY (no limit/offset)
- DO NOT recommend refactoring, optimization, or architectural changes
- DO NOT suggest improvements or changes
- DO NOT perform root cause analysis
- DO NOT propose future enhancements
- DO NOT critique the implementation or identify problems
- ONLY describe what exists, where it exists, how it works, and how components interact
- Analyze the code to determine the coding style, indentation, conventions, naming conventions, etc.
- Create a technical map and documentation of the existing system
- Always run fresh research - do not rely on existing AGENTS.md or CODEBASE.md
- Use actual file paths and line numbers for developer reference
- The research document should be self-contained and include all necessary context
- Include cross-component connections and a description of how sub-systems interact
- Include temporal context (when the research was conducted)

#### CODEBASE

For this purpose, ingest and scrutinize the entirety of the codebase
IMPORTANT
- DO NOT include files excluded by .gitignore

#### Structure the output document as follows:
    ```markdown
    ## Summary
    [High-level documentation of what was found]

    ## Coding style and conventions
    [Detailed description of the coding style, indentation, conventions, naming conventions, etc.]

    ## Detailed Findings

    ### [Component/Area 1]
    - Description of what exists ([file.ext:line](link))
    - How it connects to other components
    - Current implementation details (without evaluation)

    ### [Component/Area 2]
    ...

    ## Code References
    - `path/to/file.py:123` - Description of what's there
    - `another/file.ts:45-67` - Description of the code block

    ## Architecture Documentation
    [Current patterns, conventions, and design implementations found in the codebase]

    ## Open Questions
    [Any areas that need further investigation]
    ```

## Requirements analysis

### Develop Product Requirements for a Goal

#### Act as the leading global expert in defining product requirements in the field defined by `goal.md`

Task Steps:
1. Read `goal.md` in full
2. Formulate detailed product requirements
3. Save the requirements in markdown as `requirements.md`.

#### CRITICAL: DO NO MORE THAN RESEARCH AND FORMULATE REQUIREMENTS

IMPORTANT
- MAKE REQUIREMENTS, DO NOT IMPLEMENT THEM
- Read `goal.md` in full
- Research the goal and understand it
  - Use web search as needed to research how comparable goals were achieved in comparable projects
- Read `AGENTS.md` and `docs/CODEBASE.md` to understand the existing codebase, dependencies and underlying technology stack
- Read and align with `docs/GUIDELINES.md`
- Comprehensively research the implications of the goal for the existing codebase
- Formulate detailed product requirements
  - Think deeply to formulate the requirements
- Conform to existing naming conventions
  - PREFER succinct expressive names to verbose names
- Be pragmatic - don't hold out for an ideal set of requirements
- When uncertain, describe the options
- Summarize key findings
- Include open questions at end to resolve uncertainty or ambiguity
  - Potential non-goals include:
    - API backwards compatibility - it may be acceptable to:
      - fail to maintain compatibility
      - break dependents
    - Blast radius minimization - it may be desirable to:
      - cascade breaking API changes throughout the codebase, tests and documentation and avoid the use of shims or adapters
      - cascade re-naming to align with new/revised conventions

#### Structure the output document as follows:
    ```markdown
    ## Summary
    [Recap summary of the goal]
    [High-level overview of the new product requirements]

    ## Product Requirements

    ### [Requirement 1]
    - Description of the requirement
    - Description of what will be added, modified or removed
    - How it connects to other requirements

    ### [Requirement 2]
    ...

    ## Options and Open Questions
    [Summary of any major options, choices or ambiguities that need resolving]
    ```

### Iterate Product Requirements

#### Act as the leading global expert in defining product requirements in the field defined by `goal.md`

Task Steps:
1. Read `goal.md` in full
2. Read `requirements.md` in full, particularly the embedded answers to open questions at the end
3. Use online web search and codebase research as needed to refine understanding of requirements and their implications
4. Improve `requirements.md`, writing a new version saved as `requirements.new.md`:
  - Read `requirements.feedback.md` if it exists, incorporating this feedback into the revised requirements
  - Review `requirements.md` section by section, one section at a time, improving each section and appending it to `requirements.new.md`

#### CRITICAL: DO NO MORE THAN RESEARCH AND FORMULATE REQUIREMENTS

IMPORTANT
- MAKE REQUIREMENTS, DO NOT IMPLEMENT THEM
- DO NOT RELY ON MEMORY - `goal.md` and `requirements.md` may have been edited outside this session
- RETAIN ALL IMPORTANT DETAIL from `requirements.md`
- `requirements.new.md` must be an improved and clarified version of `requirements.md`, with resolved answers incorporated and all details retained; retain question numbers and resolution records, and append any new questions without renumbering

## Design and planning

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

### Plan the Design and Implementation of a Goal

#### Act as a principal software engineer who is the leading global expert in the field

Task Steps:
1. Read `requirements.new.md` in full
2. Formulate a comprehensive design and implementation plan to meet the requirements and achieve the goal
3. Write the plan in markdown as `plan.md`:
  - Break down the plan into detailed incremental phases
  - Each phase should be a self-contained piece of work
  - Each phase should build on the work of previous phases
  - Each phase should focus on a particular subset of the requirements
  - The plan should culminate with all requirements met

#### CRITICAL: DO NO MORE THAN RESEARCH AND MAKE A PLAN

IMPORTANT
- MAKE A PLAN, DO NOT IMPLEMENT IT
- Read `requirements.new.md` in full
- Use web search as required to further research the requirements
- Document how what IS will change to what WILL BE
- Read `AGENTS.md` and `docs/CODEBASE.md` to understand the existing codebase
- Read and align with `docs/GUIDELINES.md`
- For each requirement, evaluate its complexity and feasibility, specifically:
  - Use web search to research how comparable features were designed and implemented in comparable open source software
  - Evaluate how the requirement depends on the capabilities of the underlying technology stack
  - Comprehensively research the implications of the requirement for the codebase
  - Evaluate the complexity of implementing the requirement with the codebase
  - If a requirement is highly complex or infeasible, ask for a resolution as an open question, including a description of the challenge
- Formulate a detailed design and implementation plan
  - Think deeply to formulate the plan
  - Identify overlapping requirements and factor out common code
  - Review and SCRUTINIZE the planned use of all newly depended APIs
    - Review the detailed behavior of each newly depended API and ensure that the actual behavior of the API aligns with the intended uses
  - Break down the plan into a series of phases
- Conform to existing naming conventions
  - PREFER succinct expressive names to verbose names
- Conform to existing frameworks and libraries including test frameworks
- Propose re-use of existing utilities, implementations, libraries and code modules as needed
  - Search the codebase for relevant implementations and patterns
  - Examine existing similar features and related code
  - Prefer re-use and enhancement to duplicative new code and bloat
  - Consider enhancing or refactoring existing code
- When formulating the design and implementation:
  - Include any new code modules and files
  - Include any potential refactoring
  - Include any potential architectural changes
  - Include adding new tests to the test-suite
  - Scrutinize all dependencies and ensure that actual API behavior aligns with intended uses
- Identify technical constraints and opportunities
- Be pragmatic - don't hold out for an ideal or perfect plan
- When uncertain, describe the options for design and implementation
- Summarize key findings and technical decisions
- Include all code snippets and files to be created/modified
- Include any open questions needed to resolve uncertainty or ambiguity
- When sequencing phases, do not rigidly align phases with architectural layers, and do not work rigidly bottoms-up:
  - PREFER vertically-sliced feature-by-feature sequencing
  - Decompose features into sub-features
  - Incrementally build the implementation by sequencing sub-features according to their inter-dependencies, from most internal dependents to least internal dependents
    - The least interally-dependent features are those that are used directly by external applications
  - Implement indvidual sub-features end-to-end within a single phase
  - Trace sub-features through the layers of the architecture:
    - Wire up sub-features end-to-end within a single phase
    - Implement skeleton or placeholder code in prior phases for subsequent phases to fill out and complete

#### Structure the output document as follows:
    ```markdown
    ## Summary
    [Recap of the goal and new product requirements]
    [High-level documentation of the proposed design and implementation]

    ## Architecture Documentation
    [New or changed components]
    [New or changed processes or threads]
    [New or changed interfaces]
    [New or changed data flows]
    [New or changed event-driven or timer processing]
    [New or changed network programming]
    [New or changed data stores]

    ## Detailed Design and Implementation Plan

    ### [Phase 1]
    - Description of area of focus in this phase
    - Description of what will be added or modified
    - How it connects to other components
    - Design and implementation details

    ### [Phase 2]
    ...

    ## Code References to Impacted Code
    - `path/to/file.js:123` - Description of proposed change
    - `another/file.js:45-67` - Description of proposed change

    ## Detailed Test Plan
    [Design and implementation of all new tests to be added to the test suite]

    ## Acceptance Criteria
    [Acceptance criteria for the implementation]

    ## Non-goals
    [Non-goals such as retaining API compatibility]

    ## Options and Open Questions
    [Major options]
    [Ambiguities]
    [Requirements that are insurmountably complex]
    [Requirements that are probably infeasible with the current technology stack]
    ```

### Iterate Design and Implementation Plan

#### Act as a principal software engineer who is the leading global expert in the field

Task Steps:
1. Read `plan.md` in full, particularly the answers to open questions at the end
2. Improve `plan.md` in place:
  - Read `plan.feedback.md` if it exists, incorporating this feedback into the revised plan
  - Review and SCRUTINIZE the planned use of all newly depended APIs
    - Review the detailed behavior of each newly depended API and ensure that the actual behavior of the API aligns with the intended uses
  - Review `plan.md` phase by phase, one phase at a time, improving each phase in place
  - Carefully evaluate the dependencies of each phase on preceding phases
    - When sequencing phases, do not rigidly align phases with architectural layers bottoms-up - prefer vertically-sliced feature-by-feature sequencing:
      - Decompose features into sub-features
      - Incrementally build the implementation by sequencing sub-features according to their inter-dependencies, from most internal dependents to least internal dependents
        - The least interally-dependent features are those that are used directly by external applications
      - Implement indvidual sub-features end-to-end within a single phase
      - Trace sub-features through the layers of the architecture:
        - Wire up sub-features end-to-end within a single phase
        - Implement skeleton or placeholder code in prior phases for subsequent phases to fill out and complete

#### CRITICAL: DO NO MORE THAN RESEARCH AND MAKE A PLAN

IMPORTANT
- MAKE A PLAN, DO NOT IMPLEMENT IT
- DO NOT RELY ON MEMORY - `plan.md` may have been edited outside this session
- RETAIN ALL IMPORTANT DETAIL from `plan.md`
- The revised `plan.md` must improve and clarify the previous version, with resolved answers incorporated and all details retained; retain question numbers and resolution records, and append any new questions without renumbering
- Read `AGENTS.md` and `docs/CODEBASE.md` to understand the existing codebase
- Read and align with `docs/GUIDELINES.md`
- Be prescriptive and specific about the implementation
- Each intermediate phase/slice should include handoff acceptance criteria to the next:
  - Acceptance for each slice should audit against `docs/GUIDELINES.md`, checking all audit flags and repairing all findings before proceeding to the next
- For each requirement, evaluate its complexity and feasibility, specifically:
  - Evaluate how the requirement depends on the capabilities of the underlying technology stack
  - Comprehensively research the implications of the requirement for the codebase
  - Evaluate the complexity of implementing the requirement with the codebase
  - If a requirement is highly complex or infeasible, ask for a resolution as an open question, including a description of the challenge
- Re-formulate the design and implementation plan
  - Think deeply to re-formulate the plan
  - Identify overlapping requirements and factor out common code
  - Break down the plan into a series of phases
- Conform to existing naming conventions
  - PREFER succinct expressive names to verbose names
- Conform to existing frameworks and libraries including test frameworks
- Propose re-use of existing utilities, implementations, libraries and code modules as needed
  - Search the codebase for relevant implementations and patterns
  - Examine existing similar features and related code
  - Prefer re-use and enhancement to duplicative new code and bloat
  - Consider enhancing or refactoring existing code
- When re-formulating the design and implementation:
  - Include any new code modules and files
  - Include any potential refactoring
  - Include any potential architectural changes
  - Include adding new tests to the test-suite
  - Scrutinize all dependencies and ensure their actual API behavior aligns with intended uses
- Identify technical constraints and opportunities
- Be pragmatic - don't hold out for an ideal or perfect plan
- When uncertain, describe the options for design and implementation
- Summarize key findings and technical decisions
- Include all code snippets and files to be created/modified
- Include any open questions needed to resolve uncertainty or ambiguity

#### Structure the output document as follows:
    ```markdown
    ## Summary
    [Recap of the goal and new product requirements]
    [High-level documentation of the proposed design and implementation]

    ## Architecture Documentation
    [New or changed components]
    [New or changed processes or threads]
    [New or changed interfaces]
    [New or changed data flows]
    [New or changed event-driven or timer processing]
    [New or changed network programming]
    [New or changed data stores]

    ## Detailed Design and Implementation Plan

    ### [Phase 1]
    - Description of area of focus in this phase
    - Description of what will be added or modified
    - How it connects to other components
    - Design and implementation details
    - Acceptance criteria for handoff to next phase

    ### [Phase 2]
    ...

    ## Code References to Impacted Code
    - `path/to/file.js:123` - Description of proposed change
    - `another/file.js:45-67` - Description of proposed change

    ## Detailed Test Plan
    [Design and implementation of all new tests to be added to the test suite]

    ## Acceptance Criteria
    [Acceptance criteria for the implementation]

    ## Non-goals
    [Non-goals such as retaining API compatibility]

    ## Options and Open Questions
    [Major options]
    [Ambiguities]
    [Requirements that are insurmountably complex]
    [Requirements that may be infeasible with the current technology stack]
    ```

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
- Read `AGENTS.md`, `docs/GUIDELINES.md` and `docs/CODEBASE.md` to understand the existing codebase
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
