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
- simplify the design as much as possible
- delete unnecessary elements

`plan.md`:
- do not rebuild unnecessarily or frequently
- do not reconfigure build: re-use the existing clang debug build configuration
- defer dependent rebuilding to the end of the implementation

`plan.md`: update for sliced phases, with interim acceptance criteria between each phase

`plan.md`: perform final review and update for internal consistency

---

audit the implementation against all `GUIDELINES.md` flags and guidelines; repair all findings
