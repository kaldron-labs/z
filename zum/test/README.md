# Zum unit tests

Build the default targets in `zum/src` before this directory, using the existing
configured toolchain and `make -j2`. Run `make test` here after building.

`ZumSecretTest` exercises the private server AES-256-GCM secret envelope and
offline rekey primitive: round trips, fresh nonces, wrong keys, associated-data
substitution, tampering, rekey retries and empty plaintext. Keys are generated
inside the test; it needs no database or environment secrets and prints no key
material. This is not the offline maintenance command or its persistence test.

`ZumTest`, `ZumClientTest` and `ZumOIDCHTTPTest` cover server records/protocols,
service-client behavior and OIDC HTTP transport respectively. `ZumScopeTest`
checks direct role-name resolution, application boundaries, disabled roles, and
identity-only scopes. SQLite restart,
full HTTP/service flows and clustered activation tests live in `../itest`.
