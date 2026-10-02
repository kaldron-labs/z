# Dashboard integration tests

`make test` starts a loopback OAuth fixture, seeds a disposable Vault, and runs
the real `ZDashOAuth::run` flow across successive processes. Two-second token
lifetimes exercise repeated renewal deadlines and refresh-token rotation.
It checks that every rotation is persisted, renewal rejection is reported,
and the next process reuses the last saved credential without opening a browser.

With `ZDB_MODULE` set to the SQLite backend, `make test` also runs
`zdashlivetest.py` using Xvfb and a disposable IAM database. Build `zdash/test`
and `ztc/itest` first. This starts actual publishers, collectors and the hub,
then keeps one authenticated dashboard session open while a new heap appears,
an existing heap changes RAG, another publisher arrives, and the collector
restarts with a new generation. Existing rows, selection and expansion survive
the transport replacement; a subsequent publisher shutdown removes its rows
and clears its selected details. Model checks and expansion run on GTK; incoming
records use the production subscriptions and Rx handoff. The underlying hub
fixture also checks heap snapshots, continuing EOS, two concurrent consumers,
rejections, overflow and restart. `ZDB_CONNECT` is replaced with a fresh fixture
path; no existing database is used.

The dashboard module observes its outgoing request frames: expansion and EOS
leave the original seven detail requests in place, a late publisher adds seven,
and collector replacement establishes fourteen new ones without sending
unsubscribe requests for routes already removed by agent disconnection.
Controlled publishers supply connections, links, pools and two databases with
hosts and tables, so every tree branch is populated through the real transport.
A wildcard heap snapshot must include both publishers before aggregate EOS;
retiring one publisher must leave the other wildcard leg delivering snapshots.
An unsolicited empty snapshot EOS must leave publisher discovery and delivery
working. A snapshot requested after all publishers have stopped reports
`SnapshotFailed`, rather than manufacturing EOS for the failure.
Inventory overflow is exercised separately with two cached publishers and a
one-frame queue; it must report a failure while another frontend continues
receiving its App snapshots.
