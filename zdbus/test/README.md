# `zdbus` unit tests

Run `make -C zdbus/test test` in the configured build tree. The tests cover
Unix address parsing, bounded EXTERNAL authentication transcripts, D-Bus frame
boundaries, invalid versus extensible message types, required and unknown
headers, direct message build/parse round trips,
typed request/return/error/signal adapters, cataloged return/named-error
dispatch, generic remote errors, and unknown-header delivery.
`ZdbusClientTest` seeds the private client allocator at wraparound and checks
that zero and pending serials are skipped.
`ZdbusServerTest` checks that an intentionally canceled `RequestName` does not
surface as a malformed-reply failure.
Live socket and bus exercises are in `zdbus/itest`.
