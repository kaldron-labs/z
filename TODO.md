# TODO

## Zu

replace many occurrences of:
`if constexpr (ZuIsSame<T, ZuStringT<S>>{})` with:
`if constexpr (ZuIsStr<T, S>{})`
using:
```
template <typename T, ZuString S>
using ZuIsStr = ZuIsSame<T, ZuStringT<S>>
```

## Zhttp

add command line option `--frag` and `--yield` to `zhttp` and `zhttpd` to configure `ZiMultiplex` `frag` for I/O fragmentation and `yield` for thread yielding, respectively

## ZiLog standardization

standardize `ZiLog` configuration from command line / environment variables

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

## Zhttp
- add `ZQUIC_LOSS` env var - `N%` to randomly drop N% of QUIC UDP packets
  - read by `zhttp` and `zhttpd` and added as a config to `Zhttp`
- run benchmark with loss at 5%, 10%, 15%, 20% to check it works
- audit against `GUIDELINES.md`
- add a stress test benchmark that runs a local client and server running 1000s of links, connections and streams
  - see `plan.new.md`

## Zrest
- figure out REST Rx -> ZvIOMsg
  - basically the same principle - store it after parse on receive,
    save the object in the DB, queue the buffer for subsequent processing
  - persisted state is Received but not Processed (mirror of Sent but not Ackd)
- both ackd and processed are DB updates
- all messages are idempotent, can be repeated (at least once)
- these DB tables are persisted queues, no more and no less
- actual stateful order, etc. tables are elsewhere and maintained
  via application logic when these messages are applied
- migrate Zrest to ZvEngine
- get zrclient up and running

## Ztls
- review and cleanup
- add a stress test benchmark that runs a local client and server running 1000s of links, connections and streams

## Zquic
- review and cleanup
- add a stress test benchmark that runs a local client and server running 1000s of links, connections and streams

## Ztcp
- review and cleanup
- add a stress test benchmark that runs a local client and server running 1000s of links, connections and streams

## Z Framework
- generate docs

## ZvEngine
- becomes `ZiEngine` / ...
- type-erased telemetry + command/control APIs (not CRTP)
- `Zquic` + `Ztcp` derive from `Zi*`, implement APIs

## build system
- factor out fbs codegen into shell script
  - used repeatedly in multiple Makefile.am

## Zum
- all flatbuffers -> ZtStruct FB
- own protocol

## Zcmd
- remove ZcmdClient, ZcmdServer, OutBufAlloc, etc.
- make zcmd skeleton with no builtins, userDB, telemetry etc. are all
  plugins
- zdash can use same plugins (if desired)
- get rid of Zcmd protocol framework entirely
  - re-dedicate to userDB
  - move into userDB plugin
- use different ports to segregate userDB from telemetry, etc.
- each command group manages it's own client, server links
  - facilitates zdash telemetry fan-in / aggregation etc.
- command groups can be implemented using REST etc.

# Z Deferred Work

## io_uring
- `io_uring_prep_send_zc_fixed`
- need a rx and tx buf allocator in Ztls (and ZiMultiplex)
  - with io_uring, rx is bound to the rx thread io_uring ring, tx likewise
    - but not jumbo, in that case we fallback to non-registered buffers
    - see https://chatgpt.com/share/6959a97f-291c-8001-a5bd-8592c2f3e2e4
- steal from unum.cloud ucall for uring
- https://medium.unum.cloud/pandas-cudf-modin-arrow-spark-and-a-billion-taxi-rides-f85973bfafd5

# Z Candidate Work

## ZtStruct
- yaml: `ZvYAML`
- toml: `ZvTOML`

## Documentation
- doxygen + htags
- shields.io badges (see README.md for reflect-cpp)

## Integrations
- python
- node.js / v8

## Zdf
- permit app to specify dataframe and/or series epoch, so
  time-series with time values from the past can be handled
- cudf, dlpack integration (in that priority order)
- TA_Lib (https://ta-lib.org/) integration
- need single call to load cudf column from zdf reader
- need single call to load cudf table from zdf dataframe

## zdash
- get running, retest

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
- reimplement mxmd on zdf
- Binance feed handler
  - https / websockets - https://libwebsockets.org/ - steal
  - json / REST - steal from libws

### Test Coverage
- 128bit print/scan tests
- vector print/scan tests
- vector ZvCf and ZvCSV tests

# Notes
- https://verdagon.dev/blog/when-to-use-memory-safe-part-2

## I/O
- call sequence to start sending:

  app must call Link::start() via txInvoke() from connected()
  - HTTP keep alives?

  CliLink::ZvLink::ZvIOQueueTx::ZmPQTx::start()
  CliLink::ZvLink::ZvIOQueueTx::ZvTx::scheduleSend()
  CliLink::ZvLink::ZvIOQueueTx::send()
  CliLink::ZvLink::ZvIOQueueTx::ZmPQTx::send()
  CliLink::ZvLink::ZvIOQueueTx::ZvTx::rescheduleSend()

- consider market data vs order/execution in terms of persistency
  of queues (see below)
- there is an underlying requirement here for persistent queuing
  - the old technique of reconstruct the queues on startup by querying
    the table is ok actually, as long as the sequence key is indexed...
  - is Zrest a queue of Zdb-persisted objects, with associated functions
    to load, unload, send, etc?
    - which is a pattern repeated for other protocols (WS, etc.)

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
