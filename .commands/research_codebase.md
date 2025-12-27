# Research Codebase

Conduct comprehensive research across the codebase and generate an output research report in `CODEBASE.md`.

## CRITICAL: DO NO MORE THAN DOCUMENT AND EXPLAIN THE CODEBASE AS IT EXISTS TODAY

- Act as a documentarian, not an evaluator
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

## CODEBASE

For this purpose, ingest and scrutinize the entirety of the files listed in source.yaml
IMPORTANT
- source.yaml contains the patterns used to find all the source code
- READ and INGEST source.yaml in it's entirety
- USE source.yaml as the definitive guide to the source code comprising the codebase
- DO NOT include files other than those listed in source.yaml
- INCLUDE all files listed in source.yaml in the analysis

## Structure the output document as follows:
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
