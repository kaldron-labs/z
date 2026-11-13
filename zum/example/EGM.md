# Zum ping integration example

## Overview

A native OAuth client (`zumping`) and protected loopback resource server (`zumpingd`) demonstrate authorization-code login, service catalog publication, token verification, and shared-signal revocation delivery. [Makefile.am](./Makefile.am) builds both as uninstalled examples; [sample client](./zumping.cf) and [sample server](./zumpingd.cf) require deployment-specific application IDs and enrolled credentials.

## Interface

- [zumping.cc](./zumping.cc) uses [native.hh](./native.hh) for configuration, discovery, PKCE login, Vault credentials, and bearer-authenticated `GET /ping`. Configuration requires issuer/client identity and service origin; `--no-browser` leaves browser navigation to the user.
- [zumpingd.cc](./zumpingd.cc) exposes `GET /ping`, `GET /health/ready`, and `POST /ssf`. It publishes revision 1 of the ping action/role/native-client catalog. Ping returns 200 for a verified principal with the ping action, 403 without the action, 401 for invalid credentials, and 503 for unavailable verification.
- [pinghttp.hh](./pinghttp.hh) and [pinghttp.cc](./pinghttp.cc) adapt `Zum::ServiceHTTPFn` to Zhttp/Zrest, carrying GET/POST requests and PUT catalog manifests with authorization, conditional, and idempotency headers.

## Invariants

- Native discovery requires an exact issuer, supported authorization-code/refresh grants and S256, and valid advertised endpoint URLs. HTTPS is required except explicit loopback test mode. The callback accepts the first correctly bound state with either a code or an error, rejects duplicate recognized fields, and limits query input to 16 KiB. [native.hh](./native.hh), [zumping.cc](./zumping.cc)
- Vault credentials are scoped to issuer, service, and client. Loaded credentials are refreshed and saved before use; failure falls back to interactive login. Fresh login performs a resource call, refreshes when possible, and calls again; the resource path has no explicit 401 retry. [native.hh](./native.hh)
- The server listener is loopback-only; deployment TLS termination is external. Service startup and catalog publication precede listener admission. Environment-provided client secret and SSF authorization must both be present, or service credentials are loaded from Vault. [zumpingd.cc](./zumpingd.cc)
- Start the HTTP adapter before its consumers and stop consumers before the adapter. Adapter shutdown seals admission, joins child clients, and drains outstanding credits before finalization; its destructor does not perform stop. Completions are guarded once, normally posted to RX; closed admission completes inline with an empty response. [pinghttp.hh](./pinghttp.hh), [pinghttp.cc](./pinghttp.cc)

## Side Effects

- Login reads configuration with `NoFollow`, opens loopback and outbound HTTP sockets, prints an authorization URL, optionally launches `xdg-open`, and reads/writes Vault credentials. Mutable request/token storage is cleared by local cleanup paths. [native.hh](./native.hh)
- The server verifies tokens, publishes its catalog, registers SSF delivery, logs revocation callbacks, and can save supplied service credentials. Replies use no-store; 401 responses include a Bearer challenge. Shutdown stops the listener before draining Service and its transport. [zumpingd.cc](./zumpingd.cc)
- The adapter permits up to eight HTTPS or exact-loopback HTTP origins, retains at most 64 KiB response bodies, and configures 15-second request timeouts with one transport retry. It allocates retained call state and schedules RX/TX work; unsupported methods complete with status zero. [pinghttp.cc](./pinghttp.cc)

## Verification

From a configured repository root, run `make -j8` to build the examples. Follow [README.md](./README.md) for provisioning and manual invocation. These require a configured authorization service and credentials; no build or integration commands were executed for this documentation change.
