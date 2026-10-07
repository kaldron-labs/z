# TODO

- `zjrpc.md`
- SBE (simple binary encoding)
- FIX

## zdash

- get running, retest

## Zum

- check implementation
  - need administration of all tables for built-in use
  - need to prove OIDC integration

## Zdf
- permit app to specify dataframe and/or series epoch, so
  time-series with time values from the past can be handled
- need single call to load cudf column from zdf reader
- need single call to load cudf table from zdf dataframe
- cudf, dlpack integration (in that priority order)
- TA_Lib (https://ta-lib.org/) integration

## MxMD

- reimplement mxmd on zdf

## Zdb

- "startup replays recovered incomplete sagas that are crash residue from a previous run"

## Zrest
- codegen tool?
- REST interfaces (can be codegen) (3x - core/cli/srv)
  - `xxx{,_cli,_srv}.{hh,cc}` - interface files

## Build system
- CI/CD
- Nanobench
- Containerize?

## Testing
- catch-2 (compare with `ZuTest`)
- SAST - cppcheck, clang-tidy

## Dependency Management
- conan?

## zdb_pq
- postgresql extension productization
- register at rds-postgres-extensions-request@amazon.com

## Mx Work
- Binance feed handler
  - https / websockets - https://libwebsockets.org/ - steal
  - json / REST - steal from libws

### Test Coverage
- 128bit print/scan tests
- vector print/scan tests
- vector ZfCf and ZvCSV tests

## Documentation

### Z I/O generic object model

- Client-side:
  - `Hub`:
    - owns `Link`s and `Pool`s
    - explicitly configured
    - can be started/stopped under app control
    - long living, typically the same lifetime as the process
  - `Link`:
    - explicitly instantiated by the app
    - used as an explicitly selected outbound route
    - specifies destination
    - long living, typically the same lifetime as the process
    - encapsulates application message processing
    - sometimes termed "line" or "session" in other frameworks
  - `Pool`:
    - load-balancing group of `Link`s
  - `Cxn`:
    - instantiated by the transport when the app starts the `Link`, causing a `connect`
    - owned by a `Link`
    - transient and can be short-lived
    - typically automatically re-instantiated via reconnection under `Link`/`Hub` control
    - encapsulates transport state
- Server-side:
  - `Hub`:
    - keeps track of `Link`s
    - explicitly configured
    - can be started/stopped under app control
    - long living, typically the same lifetime as the process
  - `Pool`: unused on server-side
  - `Link`:
    - instantiated on-demand when incoming connections are accepted
    - encapsulates application message processing
    - is constructed with origin, and often enriched with identity following auth
    - the same lifetime as the incoming connection
    - sometimes termed "line" or "session" in other frameworks
  - `Cxn`:
    - instantiated by the transport once the app calls `listen` and connections are accepted
    - owns the `Link`
    - transient and can be short-lived
    - encapsulates transport state

### Latency-optimized load balancing

- the key innovation in legacy `ZvEngine`, beyond the `Hub`/`Link`/`Pool`/`Cxn` model is:
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

- `zhttp` implements something like the above, reconcile

- original motivation for this was to elide the thread-contention on pulling from a shared queue, but this is no longer true for a sharded architecture

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

## io_uring
- UDP with TOS: `io_uring_prep_sendmsg_zc_fixed` / `io_uring_prep_recvmsg_multishot`
- TCP: `io_uring_prep_send_zc_fixed` / `io_uring_recv_multishot`
- need a rx and tx buf allocator in Ztls (and ZiMultiplex)
  - with io_uring, rx is bound to the rx thread io_uring ring, tx likewise
    - but not jumbo, in that case we fallback to non-registered buffers
    - see https://chatgpt.com/share/6959a97f-291c-8001-a5bd-8592c2f3e2e4
- https://medium.unum.cloud/pandas-cudf-modin-arrow-spark-and-a-billion-taxi-rides-f85973bfafd5

## ZfTOML
- copy of `ZfCf` but TOML format

# Notes
- https://verdagon.dev/blog/when-to-use-memory-safe-part-2

## I/O
https://www.youtube.com/watch?v=w61NXrYIx6Y

## Sagas

Sagas are aggregate intents to complete a number of individual actions,
where each action is typically a write to a dependent database; the action
key can be an unsigned integer ID in the range [0,N) where N is the number of
actions in the saga.

Sagas and actions have a 1:N parent/child relationship.

The saga can be implemented as {RN, bitmap} where bitmap is initialized to a
mask where each set bit corresponds to a pending action - when the action
completes, the corresponding bit in the saga is cleared; when the bitmap
is all zero, the saga is complete

Each completion of an action and/or each subsequent recovery of a
completed action at restart clears the corresponding bit in the parent saga.

Upon restart/activation following failure/failover, recovery of a
pending saga recovers a bitmap with all the action bits set.

When the saga is complete, the zero bitmap is written to the saga database
to elide consideration during recovery - limiting recovery processing
to incomplete or partially complete sagas.

The saga DB must be recovered first, then the individual action DBs.

Use of a bitmap rather than a sequence counter permits (but does not
require) concurrent action processing and out-of-order completion -
any inter-action sequencing dependencies can be enforced by the
application; however a bitmap may limit the number of actions
comprising a saga to a low value (64), while a counter would permit
arbitrarily long sagas at the expense of requiring strict sequencing
of the actions in order to be able to reliably determine the remaining
actions that need to be idempotently re-executed on recovery of a saga.

Actions can be persisted as individual intents - once such an action
intent is successfully persisted, it is guaranteed to be recovered
for idempotent processing independent of recovery of the parent saga,
so the corresponding action bit can be cleared in the saga. From the
saga's POV, the action is completed by persisting its intent.

Actions that are message transmission intents for remote execution
(whether RPCs or asynchronously queued messages) should be
idempotently resent/requeued during recovery.

Such transmission intents should be written prior to actual network
transmissions, but include relevant message IDs
(FIX: session ID + sequence number; Kafka: partition + offset, etc.).

The transmission intent record should include the parent saga key
and the action ID (i.e. the bit number) - although the app may be able to
infer the action ID from the transmission intent being recovered - but for
example if the transmission is a ledger balance update, and there are multiple
ledger updates in the saga, each will have it's own action ID, and
the action ID will need to be stored in the transmission intent.

For resending, there should be a tree lookup from message ID to payload.
A write-through-cached persistent index should be maintained that maps
message IDs to records. A resend can then retrieve the payload either
directly from the transmit DB or reconstruct it from the saga and actionID.

For FIX, resends may occur at any time up to a sequence number reset on
the session, which should be scheduled once every 24 hours at most.

The transmit intent record should remain in place during recovery so that
the resend index can be rebuilt from it. (FIX: resends with the same sequence
number should be PossDupFlag=Y). Once the message is persistently
delivered such that it will no longer be resent with the same message ID,
it may still be resent at the application level (FIX: PossResend=Y).

Transmit intent garbage collection (deletion) is triggered by the
combination of both saga completion and the elimination of any
need to potentially resend the message; the latter may occur on
firm acknowledgement from a receiver that the message has been persisted,
or on expiry of a message retention time window (FIX - typically 24hrs);
garbage collection of transmit intents should be performed incrementally
by a background process.

---

sagas, idempotent reception and transmission
example exchange trade saga involves:

buyer {
  base credit/debit
  quote credit/debit
  commission debit
}
seller {
  base credit/debit
  quote credit/debit
  commission debit
}
buyer { transmit fill }
seller { transmit fill }
broadcast trade
----
buyer {
  referral commission credit
  exchange commission credit
}
seller {
  referral commission credit
  exchange commission credit
}
buyer {
  transmit base balance
  transmit quote balance
  transmit commission balance
  transmit referral commission balance
}
seller {
  transmit base balance
  transmit quote balance
  transmit commission balance
  transmit referral commission balance
}
transmit exchange balance

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
