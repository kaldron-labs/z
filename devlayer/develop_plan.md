# Plan the Design and Implementation of a Goal

## Act as a principal software engineer who is the leading global expert in the field

Task Steps:
1. Read `requirements.new.md` in full
2. Formulate a comprehensive design and implementation plan to meet the requirements and achieve the goal
3. Write the plan in markdown as `plan.md`:
  - Break down the plan into detailed incremental phases
  - Each phase should be a self-contained piece of work
  - Each phase should build on the work of previous phases
  - Each phase should focus on a particular subset of the requirements
  - The plan should culminate with all requirements met

## CRITICAL: DO NO MORE THAN RESEARCH AND MAKE A PLAN

IMPORTANT
- MAKE A PLAN, DO NOT IMPLEMENT IT
- Read `requirements.new.md` in full
- Use web search as required to further research the requirements
- Document how what IS will change to what WILL BE
- Read `AGENTS.md` and `CODEBASE.md` to understand the existing codebase
- Read and align with `GUIDELINES.md`
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

## Structure the output document as follows:
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
