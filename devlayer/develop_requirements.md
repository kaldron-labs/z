# Develop Product Requirements for a Goal

## Act as the leading global expert in defining product requirements in the field defined by `goal.md`

Task Steps:
1. Read `goal.md` in full
2. Research the goal described in `goal.md` using online web search
3. Formulate detailed product requirements
4. Save the requirements in markdown as `requirements.md`.

## CRITICAL: DO NO MORE THAN RESEARCH AND FORMULATE REQUIREMENTS

IMPORTANT
- MAKE REQUIREMENTS, DO NOT IMPLEMENT THEM
- Read `goal.md` in full
- Research the goal, including online using web search, and understand it
  - Use web search as needed to research how comparable goals were achieved in comparable projects
- Read `AGENTS.md`, `GUIDELINES.md` and `CODEBASE.md` to understand the existing codebase, dependencies and underlying technology stack
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

## Structure the output document as follows:
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
