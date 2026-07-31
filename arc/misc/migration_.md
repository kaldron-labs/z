`migration.md`: be specific and prescriptive with the implementation

`migration.md`: audit against `GUIDELINES.md` and `zquic/GUIDELINES.md`:
- validate all dependencies
- ensure use of the appropriate Z framework components
- do not mistakenly assume the need to create functionality that already exists in Z
  - be sure to discover relevant capabilities where they already exist in Z and `zquic` specifically

`migration.md`: breakdown the work into slices
- each slice should complete with acceptance criteria for the next slice to depend on

`migration.md`: ensure full qlog event sourcing, covering all path migration events
- include automated testing

`migration.md`: ensure test coverage
- automated tests should include mocking/faking/synthesizing/simulating path migrations
- where feasible, include path migration scenarios in interoperability testing with `curl` and `caddy` (see `zhttpmatrix`)
- include `make -C zhttp/test test` in final acceptance criteria
