# Generate backlog plans

## Act as a principal software engineer who is the leading global expert in C++ network programming

### Task steps
- Read the `zquic` work backlog in `work.md`, item by item. For each item:
  - Write a draft plan for the work item to `plan_[I].md`, where I is the backlog number, e.g. `plan_1.md` would be the plan to "Replace PTO-only timing with explicit QUIC timers"
  - Each plan can rely on all the previous plans having been implemented
  - IMPORTANT: Each plan must begin with the requirements being met by the work, and end with the acceptance criteria
  - FIRST DRAFT EACH PLAN, THEN IMPROVE IT
    - Improve it by following the "Plan improvement" instructions below
- Acceptance criteria:
  - All 23 plans have been written and improved to 23 separate individual files `plan_*.md`

### Plan improvement (for each plan)
- CRITICAL: Scrutinize `zquic` and ensure that the requirements of this plan are not already met by the current implementation
  - IMPORTANT: Elide any redundant work and delete any requirements already met
- Identify preconditions - preceding work that is depended on for this plan
- Align the plan with `AGENTS.md` and `GUIDELINES.md`
- Dependent compatibility is a non-goal
- Think hard about the implementation:
  - Re-use existing utilities, implementations, libraries and code modules as needed
    - Search the codebase for relevant implementations and patterns
    - Examine existing similar features and related code
    - Prefer re-use and enhancement to duplicative new code and bloat
    - Propose enhancing or refactoring existing framework or core code if it lacks features that would facilitate the work
  - Review and SCRUTINIZE the planned use of all newly depended APIs
    - Review the detailed behavior of each newly depended API and ensure that the actual behavior of the API aligns with the intended uses
- Identify technical constraints and opportunities
- Be pragmatic - don't hold out for an ideal or perfect plan
- Summarize key findings and technical decisions
- When uncertain, describe the options for design and implementation
- Include any open questions needed to resolve uncertainty or ambiguity

### Plan structure
    ```markdown
    ## Summary
    [Preconditions - preceding work that must have been completed]
    [Recap of the goal and new requirements]
    [High-level description of the proposed design and implementation]

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
    [Non-goals such as retaining dependent compatibility]

    ## Options and Open Questions
    [Major options]
    [Ambiguities]
    [Requirements that are insurmountably complex]
    [Requirements that are probably infeasible with the current technology stack]
    ```
