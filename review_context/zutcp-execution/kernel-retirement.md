# Kernel queue retirement

`ZxdpRetire.hh/.cc` implements a private observation step for pending kernel
packet holds. Nif close now uses this step through its Rx/Tx continuations.
Physical-NIC DMA acceptance and delayed completion timing remain open.

## Source basis

The inspected Linux v6.12 source is pinned to commit
`adc218676eef25575469234709c2d87185ca223a`:

* [net/xdp/xsk.c](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/net/xdp/xsk.c),
  SHA-256 `c9485689704d77cf0199eb87f5e71d97cdb0eef78f247b8891eac42196ab6381`.
* [net/xdp/xsk_buff_pool.c](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/net/xdp/xsk_buff_pool.c),
  SHA-256 `f89acfeacbafc0619eb5410192822936d73c95e269e573bb03575cffac127007`.

Socket unbinding removes socket users and synchronizes network readers. Final
pool release is deferred. Under RTNL, `xp_clear_dev` invokes driver zero-copy
teardown before clearing the pool's queue association. `xp_assign_dev` also runs
under RTNL and refuses a queue with an existing pool. Therefore, an independent
successful bind to that same queue observes the preceding pool's teardown. This
inference relies on the driver's normal teardown contract. Merely closing the
user FD or observing a consumed TX cursor is not the observation used here.

## Implementation sequence

1. The caller stops admissions/publication, quiesces endpoint selection, drains
   Rx work and hands teardown to Tx. Ring reservations must have completed or
   been cancelled. Queue service must remain quiescent through retirement.
2. On Tx, prepare one empty observer socket over the same arena with private
   two-entry rings. Compare both sockets' `SO_NETNS_COOKIE` identities before
   touching the original binding. Preparation or namespace failure leaves the
   original socket, mappings and packet holds unchanged.
3. Save the original ring identities and bind generation, then close its FD and
   ring mappings. Packet associations remain held independently.
4. Attempt an ordinary bind of the observer to the saved interface/queue.
   EBUSY returns Pending. The caller retries through owner continuations/timers;
   the helper contains no wait or retry loop. Other errors preserve the
   unresolved operation and all packet holds for explicit retry.
5. Successful bind establishes Done. The observer has never published FILL or TX
   entries and can close immediately. Its deferred cleanup has no packet use.
6. Tx releases pending Tx associations; a further Rx continuation releases
   pending FILL associations. Each release checks the matching ring identity,
   closed original socket and original bind generation. Lookup clears before
   original atomic buffer references return backing to the free slist.
7. Callback/timer consumers drain before owner destruction. Endpoint exclusion
   remains until the caller's explicit final membership retirement. Independent
   application references can continue to hold their original packet backing.

The observer reuses the arena, without creating payload storage or rebuilding
free lists. Its temporary registration adds kernel pin/registration accounting
during teardown; setup failure is reported before closing the original binding.
Its rings remain empty throughout. Failed observation never authorizes a forced
timeout release. The observer and original socket objects remain alive until the
operation completes; a pending observer cannot be abandoned through destruction.

## Verification and remaining integration

The privileged lifecycle test exercises three pending FILL frames and two pending
Tx associations, including a frame held only by Tx and one also held by the
application. It checks NET_RAW denial, restoration/retry, namespace mismatch,
retention until Done, Rx/Tx owner release, final application release, exact pool
capacity recovery and endpoint-fence lifetime. EBUSY retries execute on mx Tx;
the main test waits through a semaphore. Whole-mx stop drains the fixture's
timers and raw callbacks before stack destruction.

`kernel-retirement.txt` records eighteen checks in that subtest and all ten
lifecycle subtests passing. `kernel-retirement-memcheck.txt` records zero errors
and lost allocations, including child processes. Veth reports copy mode.
The normalized Nif test separately verifies per-interface close/final callback
draining, same-layout restart and a UDP packet pair. It drops interface references
before late application-buffer release while mx remains running. Hardware DMA,
delayed completion timing remains open. The standalone packet utility now exercises
retirement during whole-mux plugin stop; bounded owner continuations avoid relying
on the mux control/timer thread while that thread waits for stop completion. The packet
plugin fixture additionally verifies Rx finite/indefinite waiting and low-latency
polling through mx's existing wake dispatch, including final teardown in each mode.
