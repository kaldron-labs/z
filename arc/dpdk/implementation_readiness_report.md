# DPDK implementation readiness review

**Verdict — resolve three blockers before implementation.** Phase 1 lacks its prescribed public declaration. Phase 2 assumes an obsolete lifecycle and gives no way to guarantee Tx credit recovery for the required software fixture.

Line references use the reviewed revision of [the full execution plan](dpdk_plan.md), lines 1–467, Git blob `57f4357cd21e01de530c306ddb4365f310a9557e`. The implementation-readiness, codebase-details and style reviewers completed one independent round. The review checked the plan against the working tree. Runtime results are outside this document review.

## Blockers

| ID and plan lines | Finding and required decision |
|---|---|
| C-1-1 · 44–67 | Phase 1 mandates the exact EALParams aggregate and enum declarations from a DPDK concepts brief. That file is missing beside the plan. The root concepts_brief.md describes Zutcp and excludes DPDK implementation. The archived transcript contains historical declarations. Restore the adopted version or include it verbatim in the plan, with a stable reference and the argv mapping needed to test it. |
| C-1-2 · 20–22, 163–171, 305–318 | The plan describes stopTx and close as existing virtual operations and directs Zutcp to recreate its backend on restart. Zpkt exposes engine start/stop, final and error. The retrying close and stopTx helpers are private to Xdp. Zutcp retains its backend across stop and calls init then start on that object. Specify how DPDK acquires and releases the port through these callbacks, who schedules pending retirement with no DPDK sender, and how failed start reaches its terminal callback. Distinguish restart of the same object from a new instance acquiring the released port. [Zpkt interface](../../zpkt/src/Zpkt.hh#L70) · [Xdp lifecycle](../../zpkt/src/Zpkt.cc#L142) · [Zutcp start and stop](../../zutcp/src/Zutcp.cc#L989) |
| R-1-1 / C-1-3 · 129–144, 163–170, 349–357 | Every accepted send increments Zutcp's pending count. At its credit limit Zutcp refuses more sends, and during stop it waits for that count to fall before asking the backend to stop. The ring fixture releases its Tx reference on the peer, while a caller can retain another reference. The plan requires exactly-once completion even when PMD cleanup supplies no count, but identifies no observable event in that case. Specify the bounded bookkeeping, how it remains valid through backing return or reuse, and how each supported PMD makes progress. Exercise a final lone packet, a held caller lease and a saturated queue. Verify that credits recover and stop finishes without another send. [Credit accounting](../../zutcp/src/Zutcp.cc#L1277) · [Stop gate](../../zutcp/src/Zutcp.cc#L1060) |

## Major findings

| ID and plan lines | Change needed for implementation or acceptance |
|---|---|
| R-1-2 · 52–60, 106–121, 313–316 | Identify who keeps EAL and each native mempool alive until the final backing return. Define when a stopped instance may reuse or replace a pool. Test stopping with a held buffer, restarting, and returning the old buffer afterward. |
| R-1-3 · 115–126 | Give a descriptor-sizing rule and a structured failure outcome when PMD adjustment makes a queue incompatible with its supplied pool. Cover the smallest supported count and the first rejected configuration. |
| R-1-4 · 95–102 | Define the evidence that lets a PMD report zeroCopy as true and the default for an unknown or mode-dependent driver. Ring and pcap examples alone cannot decide what a newly encountered hardware driver reports. |
| R-1-5 / S-1-1 · 83, 153–161, 187 | Define when the Rx owner probes, parks and consumes a wake under continuous traffic and during wait entry. State the measurable wake/idle bounds and whether batch size and backoff are implementation choices. Write “0 disables parking and keeps polling” in both parameter comments. |
| R-1-6 / C-1-5 · 173–177 | Map the required queue measurements to Ztc fields, including units and how unavailable measurements appear to consumers. The numeric fields in [QueueTelemetry](../../zm/src/ZtcQueue.hh#L41) default to zero and have no individual validity flag. Specify the source used when a ring and its PMD both expose a measurement. |
| R-1-7 · 368, 413, 449–461 | All proposed DPDK drivers are conditional, but disabled-build acceptance requires a structured failure from the DPDK factory. Put that assertion in an always-built target such as ZpktInstanceTest and run it in the disabled configuration. [Existing test target](../../zpkt/test/Makefile.am#L13) |
| R-1-8 / S-1-2 · 106–111, 253–257, 379–386 | Distinguish the provider handles created in Zutcp::Instance::init from the native mempools materialized during backend setup. Give each assertion an explicit method boundary so the test can detect early geometry freezing. |
| S-1-3 · 349–362, 374–386, 449–456 | Assign commands and prerequisites per suite. The plan specifies a Linux route/neighbor namespace for Zutcp and a separate ring/pcap helper for Zpkt. The final instruction refers to one namespace fixture for all programs. |
| C-1-6 · 146–161 | Zutcp can post a wake and schedule another Rx invocation from inside the receive callback. A burst may already have dequeued more mbufs. Specify who owns the undelivered suffix across the yield and during stop. Test a burst larger than the remaining quantum for ordered delivery and final pool return. [Zutcp Rx callback](../../zutcp/src/Zutcp.cc#L1150) |
| C-1-7 · 349–361 | The simulated wire needs the destination native mempool and endpoint rings. The common IOBufPool interface exposes raw-byte allocation and geometry. Specify how the Zpkt and Zutcp fixtures obtain private native handles and when those handles expire during stop or restart. [Pool interface](../../zi/src/ZiIOBuf.hh#L519) |

## Minor findings

- **S-1-4 · lines 95–97.** Name the queue fields required in status output so an operator knows which values to expect.
- **S-1-5 · lines 317–327.** Keep the synchronous route-resolution contract in the paragraph that describes the Linux route and neighbor lookup.
- **S-1-6 · lines 44–47.** Once the adopted EAL declaration is accessible, a single sentence identifying its authority suffices.

Finding IDs identify the codebase (C), readiness (R) and style (S) reviewers, the review round and the item. Combined IDs record independent findings about the same change.
