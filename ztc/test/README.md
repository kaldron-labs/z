# Telemetry unit tests

`make test` runs the standalone TAP binaries.

`ZtcAgentTest` includes the production `PingHTTP` dependency. Lifecycle cases
cover never-started final, retained function rejection after final, startup
failure with a stopped borrowed resolver, repeated start/stop, restart and
final from stop completion. These cases create no remote HTTP requests.
Agent cases also cover stopped init/final, startup rollback with a stopped
borrowed resolver, retry after that failure, repeated operations and restart.
Runtime cases use an isolated telemetry ring and loopback OAuth URL; successful
startup does not require authentication or a remote WebSocket peer.

`ZtcHubTest` includes a held listener-deactivation completion: Hubd must remain
Stopping until that completion is released. It also covers repeated lifecycle
operations, restart, never-started final and final from stop completion. This
isolated case does not prove clustered election or live listener rebinding.

`ZtcHubRxTest` includes the production hub entry translation unit with its
entry point renamed for the test. It invokes the actual receive and
authorization callbacks with a single-threaded transport stub and the real
hub/verifier. It covers pending frame and byte limits, authorization during
a partial frame, failed authorization/cleanup, and receive-buffer reuse.
Input rejection cannot subsequently authorize the link, and malformed queued
payloads still pass through the production verifier. Timers and transmission
are inert; network behavior is covered separately by
`ztc/itest/ztchubtest.py` and `zdash/itest/zdashlivetest.py`.

`ZtcHubRoleTest` invokes production role handlers with an isolated internal
routing state and a real running owner/multiplexer. It holds listener shutdown
across down/up/down/up, verifies that old routes remain until completion,
observes partial retirement between bounded owner turns, and verifies that the
new activation sees cleared state. A false listener-stop result still completes
retirement. It does not exercise actual DB election, sockets or rebinding.
