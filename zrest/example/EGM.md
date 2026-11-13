# Zrest OAuth examples

## Overview

Native authorization-code/PKCE client and in-memory OAuth server, with authenticated ping workload across HTTP/TLS/QUIC profiles. These are example applications with their own route catalogs and lifecycle.

## Interface

- [zrest.hh](./zrest.hh): URI/JSON schemas for authorization, code exchange, refresh, revocation, discovery, tokens, claims, JWK/JWKS and ping/pong. Issuer paths are `/oauth2/{app}`; endpoints use `/oauth2/{app}/v1/{endpoint}`; discovery uses `/.well-known/oauth-authorization-server/oauth2/{app}`; resource path is `/api/ping`.
- [zrest.cc](./zrest.cc), `Options`: defaults issuer/resource to HTTPS localhost port 8443, application `ping`, client `zrest-native`, scope `ping`, browser `xdg-open`, callback timeout 30 seconds and request timeout 15 seconds. Workload, concurrency, CA, protocol, retry, interval and diagnostic options are declared in its CLI schema; no positional arguments are accepted.
- [zrestd.cc](./zrestd.cc), `Roots`: authorization GET/POST, token/revoke POST, keys/discovery and ping routes. `Options` configures listener/TLS/protocol, credentials, state limits, access/refresh lifetimes, logging and process behavior. `App` manages application activation and suspension; main activates `ping` and `other`.
- [Makefile.am](./Makefile.am): builds non-installed `zrest` and `zrestd` against the framework REST/transport stack.

## Invariants

- Client `validURL` requires HTTPS unless development HTTP explicitly permits literal loopback addresses; issuer/resource share scheme, and discovery endpoints must match issuer origin and expected paths. `loadJSON` requires bounded, complete JSON and the expected content type; discovery/token parsing additionally requires `no-store`. The client validates the keys URL but does not fetch JWKS or verify access-token claims. [zrest.cc](./zrest.cc)
- `CallbackApp::request` rejects unknown/duplicate callback fields and consumes the first syntactically accepted code/error callback; main then verifies state/issuer. PKCE uses independently generated verifier/state material and S256. `tokenResponse` validates Bearer type, token bounds, expiry and scope. [zrest.cc](./zrest.cc)
- Server `AppState::init` retains application state and snapshots active admission. Authorization permits registered explicit-port HTTP loopback `/callback` redirects, supported scopes, and S256. Browser transactions and authorization codes are one-use; POST consumes the transaction before credential/decision checks, and code exchange consumes a valid code before issuance. [zrestd.cc](./zrestd.cc), `App::authorize` and `App::token`.
- Refresh rotation marks the old grant inactive before replacement issuance; replay revokes its family. JWT validation checks canonical segments, claims, issuer/audience/time and ES256 signature; ping requires a live unrevoked access/family record and ping scope. Revocation succeeds for unknown tokens belonging to a known client. [zrestd.cc](./zrestd.cc), `App::token`, `jwtVerify`, `App::ping`, and `App::revoke`.
- Client workload sends bounded waves, refreshes near expiry, and retries a wave's 401 responses once after refresh. Cleanup accepts revocation status zero as well as 200; success still requires all requested pongs and no interrupt. Server completion follows a qualifying revocation response after enough pongs; pongs alone do not terminate it. [zrest.cc](./zrest.cc), `main`; [zrestd.cc](./zrestd.cc), `App::responseComplete`.

## Side Effects

- Client main starts network/scheduler/logging facilities and a loopback callback server. `openBrowser` forks/execs the configured browser with the authorization URL and optional CA environment; browser wait has no timeout or exit-status validation. The client exchanges code/refresh/revocation forms, sends Bearer requests, and clears token/state storage during shutdown. [zrest.cc](./zrest.cc)
- `Application::init` generates P-256 keys and random key IDs. Server handlers allocate/mutate browser/code/refresh/family/access records and ordered expiry indexes; `Application::expire` removes expired entries. Authorization renders HTML and secure HttpOnly SameSite cookies; token issuance signs JWTs; revocation mutates family/access flags. [zrestd.cc](./zrestd.cc)
- Server `prepareProcess` may initialize daemon/PID state. Main opens selected listeners/logs and optional QUIC key logs, installs signal handling and can print diagnostics; event-FD hooks write listener/result bytes. Shutdown waits for server stop and finalizes the multiplexer; exit checks errors and completed pongs. [zrestd.cc](./zrestd.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zrest/test test`. Default build includes the examples. No browser, listener, daemon, OAuth workflow or test was executed for this documentation change.
