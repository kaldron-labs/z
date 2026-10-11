# Telemetry unit tests

`make test` runs the standalone TAP binaries.

`ZtcHubRxTest` includes the production hub entry translation unit with its
entry point renamed for the test. It invokes the actual receive and
authorization callbacks with a single-threaded transport stub and the real
hub/verifier. It covers pending frame and byte limits, authorization during
a partial frame, failed authorization/cleanup, and receive-buffer reuse.
Input rejection cannot subsequently authorize the link, and malformed queued
payloads still pass through the production verifier. Timers and transmission
are inert; network behavior is covered separately by
`ztc/itest/ztchubtest.py` and `zdash/itest/zdashlivetest.py`.
