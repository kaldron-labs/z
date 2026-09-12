# Zum unit tests

Build the default targets in `zum/src` before this directory, using the existing
clang-debug configuration and `make -j3`. Run `make test` here after building.

`ZumSecretTest` exercises the private server AES-256-GCM secret envelope and
offline rekey primitive: round trips, fresh nonces, wrong keys, associated-data
substitution, tampering, rekey retries and empty plaintext. Keys are generated
inside the test; it needs no database or environment secrets and prints no key
material. This is not the offline maintenance command or its persistence test.

`ZumTest`, `ZumClientTest` and `ZumUpstreamTest` cover server records/protocols,
service-client behavior and upstream transport respectively. SQLite restart,
full HTTP/service flows and clustered activation tests live in `../itest`.
