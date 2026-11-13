# Zum authorization and user management

## Overview

Zum groups the service-side authentication library, persistent authorization daemon, management CLI, and a native-client/resource-server example. It builds on Zhttp/Zrest, Ztls, Zdb, Vault and the framework scheduler; daemon telemetry uses Ztc. The existing [README.md](./README.md) describes deployment and provisioning.

## Interface

- [src](./src/EGM.md): installed `libZum`, public JWT/service/management models, `zum` CLI, and `zumd` daemon with browser/passkey/OIDC authentication, catalogs, delegation, persistence and shared signals.
- [example](./example/EGM.md): `zumping` native OAuth client, `zumpingd` protected resource server, and retained asynchronous HTTP adapter.
- [Makefile.am](./Makefile.am) traverses source, unit tests, integration tests, examples and interoperability checks. Test implementation directories are excluded from this contract bootstrap.

## Invariants

- Keep scheduler, database, transport and callback dependencies alive until their owning service has sealed admission and drained outstanding work. Daemon activation/bootstrap, normal request admission, and offline rekey are distinct lifecycle phases.
- Persisted table identities, saga versions and replay inputs are compatibility boundaries. Authorization depends on current application/principal/delegation state plus captured token/grant authority; public JWT header extraction alone supplies no authentication.
- Configure actual issuer/application identities and credentials before running examples. Offline database-key rotation requires other writers stopped; completed cleanup deletions are intentionally not rolled back.

## Side Effects

The executables open listeners and outbound connections, persist identity/authorization and saga records, access Vault credentials, emit audits and maintenance logs, and schedule expiry, key/registration renewal and shared-signal delivery. Bootstrap writes an enrollment-capability file; native login can launch a browser. Child contracts specify the local ownership and failure boundaries.

## Verification

From a configured repository root, run `make -j8 && make -C zum test`; the module target invokes unit, integration and interoperability checks. Use the [example contract](./example/EGM.md) for manual integration prerequisites. No build or test commands were executed for this documentation change.
