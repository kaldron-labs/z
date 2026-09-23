# `zdbus` integration tests

Run `make -C zdbus/itest test` in the configured build tree. The current
`zdbusconnectiontest` uses a private abstract AF_UNIX listener to exercise
connection, EXTERNAL authentication, `BEGIN`, bidirectional frames, callback
completion, peer EOF failure, and stop, with three-byte I/O budgets forcing
partial transfers and scheduler continuations. It rejects a frame one byte
over the configured transmit-byte limit, then sends an in-limit frame. It
finalizes the connection
after the first of two stop callbacks, verifying queued callback delivery no
longer retains connection-owned state. It also stops before a queued start and
verifies that the connection never attempts to connect afterward.
Its peer fixture waits through a separate `ZiEventLoop` shard, and a child
process case checks pidfd readiness, timeout escalation to `SIGKILL`, and
reaping without polling.
`zdbusclienttest` uses the `zdbus/util/zdbusbus` fixture
to launch a private `dbus-daemon`, then verifies `Hello`, service-name
acquisition, catalog-dispatched typed return and structured named error,
keyed typed service-route dispatch, and an `AddMatch` signal.
It also tests exact-key typed and dynamic subscriptions, bounded fan-out,
unsubscribe, and batched stop-time subscription cleanup. Bus match rules are built
through `ZdbusClient::addMatch`/`removeMatch` using the client serial allocator
and direct `ZfDBUS` body encoding; local subscriptions are independent of bus
match-rule lifetime.
It finalizes the client from the first of two stop callbacks while a one-message
dispatch budget forces the second callback into a separate scheduler turn.
The client test also sends a `NO_REPLY_EXPECTED` method call with a distinct
serial and observes it on the service without waiting for a reply.
With a one-call pending limit, it then holds one method call unanswered,
checks that the next call reports queue pressure, observes the timeout, and
verifies that a subsequent call can use the released slot.
It also cancels an unanswered call by its client-assigned serial, checks the
single `Cancelled` completion, and reuses the pending slot. A deliberately late
return is then delivered, followed by a Tx continuation barrier; it must not
produce a second completion.
The service return, named error, and signal are checked for distinct outbound
serials, and return/error `replySerial` values are checked against their calls.
It emits a TAP skip only when `dbus-daemon` is absent from `PATH`; a daemon
that is present but fails to start remains a test failure.
