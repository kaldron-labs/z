# Ztc hub client example

## Overview

An OAuth authorization-code/PKCE client that subscribes to application telemetry through secure WebSocket.

## Interface

[ztchub_client.cc](./ztchub_client.cc) requires configuration, device ID, and WSS URL, with no-browser, CA, and help options. Configuration supplies issuerURL/clientID, scope, callbackPort, loginTimeout, optional CA, and loopbackTest. The declared serviceURL is unused. [Makefile.am](./Makefile.am) builds the non-installed ztchub_client.

## Invariants

- OAuth metadata must match the configured issuer and supported code/refresh/S256 capabilities. HTTPS is required except explicit loopback testing; the telemetry connection always requires WSS. CLI CA overrides configuration CA only for WSS. [ztchub_client.cc](./ztchub_client.cc)
- The loopback callback requires matching state and rejects duplicate recognized fields and replay. Cached credentials must refresh and save successfully before use; otherwise login proceeds interactively. [ztchub_client.cc](./ztchub_client.cc)
- The telemetry request uses subID 1, App group, and 1000-ms interval. Incoming binary frames must match device/subscription and carry a nonzero agent generation; generation continuity is not tracked. First telemetry completes the session; this example has no telemetry-response deadline or reconnect recovery. [ztchub_client.cc](./ztchub_client.cc)
- Application, client, and multiplexer lifetimes cover callbacks; session cleanup unsubscribes, closes, waits for disconnect, stops/finalizes the client, and then stops the multiplexer. [ztchub_client.cc](./ztchub_client.cc)

## Side Effects

[OAuth::run](./ztchub_client.cc) reads configuration, accesses the credential vault, performs discovery/token requests, and can rotate/persist refresh credentials. Interactive login opens a loopback HTTP listener, prints the authorization URL, and on non-Windows invokes xdg-open unless disabled. The WSS session prints device/generation and logs. A new interactive login invokes the telemetry session twice; a successful cached refresh invokes it once.

## Verification

From a configured repository root, run `make -j8 && make -C ztc test`. Manual example runs require an OAuth application, enrolled device/hub, and suitable certificates; they may open a browser and update vault credentials. Commands were not run for this documentation change.
