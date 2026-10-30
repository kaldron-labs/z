# Typed FIX client and server

`zfix.cc` and `zfixd.cc` are separate, small applications using the native managed
FIX client/server APIs. `zfix.hh` contains their shared FIX 4.4 message schemas
and catalog. Neither application uses the load tools or regression fixtures.

Build with the repository's configured dependencies:

```sh
make -C zfix/example -j8
```

Start the one-session server, then run the client in another terminal:

```sh
./zfix/example/zfixd --port 16404
./zfix/example/zfix --host localhost --port 16404 --symbol ZFIX --qty 10 --price 123.45
```

The server prints its actual listening port, receives a typed NewOrderSingle and
uses `reply<NewOrder, Ack>` and `reply<NewOrder, Fill>` to send two typed
ExecutionReports. Both sides map the Parties repeating group: the request names
two parties and both replies echo them. The client owns the correlation key and
the two ExecIDs. Its call completes only after both reports arrive. The server
also sends an unsolicited, application-defined `U1` tick through `send<Tick>`;
the client waits for this separate event before requesting Logout. Both
applications wait for the managed stopped callback and transport drain before
teardown. The client additionally drains DNS continuations before stopping its
multiplex. Only main waits on semaphores; the owner callbacks remain asynchronous.
Each link explicitly accepts its business sends into the core's bounded
operational queue. This single-call example keeps no resend history and adds no
external persistence barrier. Every resend asks `fixRetrieveTx_`, which diagnoses
the requested context and returns no content; the engine gap-fills that assigned
sequence. There is no core replay store. Peer rejects report their referenced
outbound sequence, independently
of the business correlation key. The richer regression fixtures demonstrate
external history retention and reconnect policy.

Ordinary Ready follows Logon and actual directional recovery, without another
TestRequest/Heartbeat exchange. The current-build TCP and TLS example pairs
passed with successful client/server exits, expected reports and event, and
empty sanitizer stderr; see [STATUS.md](../STATUS.md) for evidence. Installation
has not been repeated for this repair.

The server's `fixReplyRx_` checks that a request context belongs to its current
session and epoch. Its `fixAdmissionRx_` accepts Logon without FIX credentials;
an application can validate its own credentials there. This example imposes no
FIX authentication scheme.

TCP is the default. TLS uses the existing transport certificate configuration:

```sh
./zfix/example/zfixd --tls --cert server.pem --key server.key --port 16404
./zfix/example/zfix --tls --ca ca.pem --host localhost --port 16404
```

The server certificate must match the client host name and trusted CA. A server
port of zero selects an ephemeral port and prints it as `listening port=N tls=0`
(or `tls=1`). `--timeout` bounds the application's waits; `--help` lists options.

The richer application-policy regression model lives in `test`; its separate
TCP/TLS scenario driver and process checks live in `itest`. Example sources and
this directory's Makefile contain no test harness or regression targets.
