# Zpkt isolated tests

Build the configured dependency source layers, common `zpkt/src`, `zpkt/xdp`
and configured `zpkt/dpdk` before the default `zpkt/test` target, then run
`make -C zpkt/test test`. Follow the scoped [rebuild protocol](../../zpkt_plugin_plan.md#rebuild-protocol-and-scope)
with one eight-job jobserver. These TAP programs need no network privileges:

* `ZpktRingTest`: queried layouts, capacity, descriptor/cursor wrap, reservations,
  cancellation and release/acquire publication between concurrent kernel peers.
* `ZpktPoolTest`: intrusive configuration, count/size layout, independent
  reserves, exhaustion, advanced pointer return, retained mapping lifetime,
  malformed configuration, direct Zi metadata allocation/return over native slots,
  and contended payload-overlay recycling.
* `ZpktOffloadTest`: bounded TCP header classification, VLAN/IP/TCP options,
  padding, malformed input, unaligned metadata placement, generic-netlink scalar
  reply parsing and private arena-prefix geometry including full-page fallback.
* `ZpktInstanceTest`: direct private pool geometry and unbacked allocation
  refusal, without opening a Linux interface.
* `ZpktEnvTest`: actual module loading, missing module/symbol, null/throwing factory
  and construction failure, successful module residency, owned Cf lifetime,
  query substitution surviving init, metadata forwarding, query-error isolation,
  name-keyed idempotence while stopped/running, conflicting multiplexer, failed
  creation without caching, child retirement/recreation, tree reference ownership,
  idempotent finalization and last-reference Plugin destruction. It checks
  original loader/factory event context and multiple retained children, including
  an injected child finalization failure, retry, cache/reference retention and
  child-before-Plugin finalization order. Nested missing-required and malformed
  numeric fields, including a valid map node preceding an invalid node, exercise
  scoped map-construction rollback and successful initialization after failure.
* `ZpktClassifierTest`: unconditional XSK redirect instruction emission,
  and guarded attachment preconditions.

Ring publication tests intentionally run producer/consumer service loops to
exercise their memory ordering. They join both owners before examining results.
Actual sockets and packet exchange belong to [itest](../itest/README.md).
`ZpktEnvTest` links common Zpkt only and supplies a static synthetic query function;
it needs no kernel interfaces. The test-only normal, null-result, throwing-entry,
construction-failure and missing-symbol modules are not installed. Backend-private
pool/ring/offload tests include private headers and link their one backend directly.
`ZpktTestCf.hh` builds owned ZfCf trees shared with native fixtures.

For a native module loading smoke test, set `ZPKT_NATIVE_MODULE` to its explicit
shared-library path and run `ZpktEnvTest` through `prove`. This mode runs only
module initialization, finalization and last-reference release through common
Env, with an empty interface map. Set `ZPKT_NATIVE_DPDK=1` for DPDK; it supplies
regular-memory, no-PCI, no-shared-configuration EAL options. Give that run a
private `RUNTIME_DIRECTORY` and remove its empty runtime directories afterward.
The executable links common Zpkt only, so the same mode checks staged installed
modules and each backend in a directory without the other backend's file.

Run a selected binary in the current build configuration under Memcheck with
the repository wrapper:

```
./libtool exec valgrind --error-exitcode=99 --leak-check=full \
  --errors-for-leak-kinds=definite,indirect ./zpkt/test/ZpktPoolTest
```

## DPDK

The build adds two TAP programs when DPDK is enabled. Use DPDK 25.11 or newer
with the stock `net_ring` driver installed. Run them as an unprivileged user
with regular memory and software ports.

* `ZpktDpdkInstanceTest` runs EAL option cases in fresh processes. It checks
  automatic and explicit lcore affinity, named and raw arguments, native errors
  and one process-wide initialization attempt. It covers native success followed
  by a later initialization error and cleanup. Empty, overlong and embedded-NUL
  configured ports are rejected before EAL consumes its one process-wide attempt;
  a valid empty-interface Plugin can still initialize in the same fresh child.
  Device lifecycle assertions live
  in privileged `../itest/zpktniftest`, using Env and real dummy Linux interfaces.
* `ZpktDpdkPoolTest` checks native mbuf identity, offset lookup, pool geometry,
  exhaustion, caller and device leases, and rollback of provisional allocations.

After a DPDK-enabled build, run both programs from the repository root.

```sh
prove ./zpkt/test/ZpktDpdkInstanceTest ./zpkt/test/ZpktDpdkPoolTest
```
