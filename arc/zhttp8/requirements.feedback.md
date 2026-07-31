`CliLink` pool for H3:
- `-j` is intended to specify concurrency, not necessarily the number of `CliLink` instances
- it's permissible for the H3 implementation to use multiple QUIC streams over a single `CliLink` if that implementation aligns more smoothly with the overall goal
- the only potential complication here is that a single `CliLink` instance would be accessed from multiple worker threads
`-o` default is `index.html`, but `N > 1` requires explicit `-o`:
- since `zhttp` never writes to stdout but defaults to `index.html`, `N > 1` should no longer require explicit `-o`
