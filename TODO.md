# TODO

- `ZiTx` needs `Ztc::Queue` telemetry

- fix `ZmList<X, ZmListNode<X>>` to be `ZmList<X, ZmListNode<>>`
  - needlessly repeating X is elided
  - apply this to all the other containers with this pattern (`ZmHash`, etc.)

- `Zsbe`: SBE (simple binary encoding)

## zdash

- get running, retest

## Zum

- check implementation
  - need administration of all tables for built-in use
  - need to prove OIDC integration

### Test Coverage

- 128bit print/scan tests
- vector print/scan tests
- vector ZfCf and ZvCSV tests

## Packaging, Dependencies, CI/CD, Benchmarking, Containerization

- `packaging_plan.md`
- CI/CD
- Nanobench
- Containerize?

## Zdf

- permit app to specify dataframe and/or series epoch, so
  time-series with time values from the past can be handled
- need single call to load cudf column from zdf reader
- need single call to load cudf table from zdf dataframe
- cudf, dlpack integration (in that priority order)
- TA_Lib (https://ta-lib.org/) integration

## MxMD

- reimplement mxmd on zdf

## Zrest

- codegen tool?
- REST interfaces (can be codegen) (3x - core/cli/srv)
  - `xxx{,_cli,_srv}.{hh,cc}` - interface files

## zdb_pq

- postgresql extension productization
- register at rds-postgres-extensions-request@amazon.com

## Mx Work

- Binance feed handler

## Documentation

### Latency-optimized load balancing

- the key innovation in legacy `ZvEngine`, beyond the `Hub`/`Link`/`Pool`/`Cxn` model was:
  - predictive flow control for pooled links, optimized for minimum latency
  - `Link` is a `Tx`
  - `Pool` is also a `Tx`
  - permits "pools of pools"
  - `pool->ready(Tx *tx, ZuTime t)`:
    - informs `pool` that `tx` will be ready to send at future time `t`
    - if `tx` was previously forecast to be ready at a time `q`,
      it is removed from the pool and added back at new time `t`
    - if `t` is `0`, it is immediately available
    - if `t` is `null`, it is unavailable
  - sending is always to the earliest available `tx`, i.e. `minimum`
  - the protocol implementation can use reinforcement learning to predict when
    it will become available for sending based on the peer's observed behavior

- `zhttp` implements something like the above, reconcile?
  - original motivation for this was to elide the thread-contention on pulling from a
    shared pool queue, but this is no longer true for a sharded architecture

### Documentation organization

- internals docs
- doxygen + htags
- shields.io badges (see README.md for reflect-cpp)

## devlayer

L-sized work:
- initial plan
- vertically slice plan
- split out slices
- phase slices independently
- iterate slices individually
  - acceptance criteria from each phase to the next, and at end
- rework slice 2 to align with completion of slice 1, 3 with cumulative 1+2, 4 with cumulative 1+2+3, etc.
- split out phases from slices
  - ... then each phase within each slice is a bite-size incremental piece of work with acceptance criteria

- start with a new working branch
  - commit after each phase

## Integrations
- python
- node.js / v8

# Z Deferred Work

# Notes
- https://verdagon.dev/blog/when-to-use-memory-safe-part-2

## I/O
https://www.youtube.com/watch?v=w61NXrYIx6Y

---

## Market/participant failure and recovery for order/execution:

market failover:

market failover should close the market for all participants and re-open it in phases:
1] resend to participant all recovered messages, including fills
2] cancel all recovered open orders, send canceled execution reports to participant, including cumulative quantity of partial fills
3] respond to all incoming _resent_ open orders from participant with rejects
4] all incoming new (not resent) open orders should be queued until market opens, and at open the expiry time should be observed (i.e. some may subsequently be rejected)
5] the market should open once all resend/recovery is complete on both sides, and queued new orders should be processed in sequence

market -> 1, 2, 3 -> participant
FAIL
recover 1 for resend; 2, 3 are lost
participant requests 1-3 resend
market resends 1 with PossDupFlag, gap fills 2-3
1 was part of a complete recovered saga
2 was part of an incomplete recovered saga
3 was part of a lost saga that was not replicated
2 is recovered, resent as 4 with PossResend

market -> 1, 2, 3 -> participant
FAIL
market doesn't recover any of 1-3 (all are part of lost unreplicated sagas)
participant processed 1-3
market gap fills 1-3
Note: the risk here is if 1-3 included fills, anything else is immaterial.
On restart, the market will recover the orders that previously resulted in
the fills, since they will be resent by the participant if completely
forgotten - the participant needs to resend stale orders that were partially
or completely filled.
All open orders will be canceled - cancelled execution reports together with
cumulative quantity will be sent to the participant.
The participant must recon and bust any trades surplus to the cumulative
quantity in the received canceled execution reports from the market,
i.e. the market status is authoritative and overrides previous fills from
the failed instance.

participant failover is simpler, market resends as required to satisfy
participant should resend cancels
participant should not resend stale new/modify for which it does
not have fills (locally canceling them for potential subsequent re-price
and resend if appropriate), but it should recover/resend new orders that
have been partially filled, as the market may reverse one or more of the fills;
participant should expect all resent new orders to be canceled until
market re-opens; if expiry time is reliable then local cancelation need
not occur
