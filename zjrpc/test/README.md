# JSON-RPC tests

Run the standalone programs through the normal build-tree wrappers, or use
`make -C zjrpc/test test` after the default build has completed.

`ZjrpcDispatchTest` covers dispatch, duplicate IDs, bounded batch continuation,
cancellation, retained input and response lifetime. Its
`batchResponseLifetimeTest<false>` and `<true>` cases require an abandoned or
serialized batch to release its captured result models when the final batch
reference is dropped. Serialization alone must keep those models alive.

`BatchData` explicitly releases its writer captures because the array's
`ZmFn` element type uses POD array operations. The original implementation
failed both response-lifetime assertions and leaked the captured models.

Optimized Clang code for integer-ID hash lookup can hoist a string-data
selection before dispatching on the union discriminator. Memcheck can report
that selection's conditional on inactive string-representation bytes. This
diagnostic is distinct from the batch-capture leak. The reviewed instruction
paths select equivalent integer-lookup loops without dereferencing the inactive
string value. An isolated integer-only reproduction and initialized inactive-byte
patterns preserve the lookup results; an unoptimized build removes the
diagnostic.

The migration's review is specific to the actual optimized binaries. It checks
the complete normalized instruction graph and branch position against the
reproduction, observes every suspect branch with read-only GDB to verify the
integer variant is active, and checks current executable/provider identities.
It preserves raw Memcheck exit codes and reports. It accepts neither another
uninitialized branch nor any lost allocation or invalid access. An `-O0` run
does not replace optimized Release acceptance.

See [the migration record](../../uring.md) for current acceptance and evidence.
