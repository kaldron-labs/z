# Z telemetry collector

`ztcagent` owns the shared telemetry-ring reader and is intended to run inside
a deployment-owned image. It keeps enrollment material and operational mTLS
credentials in memory; the only file it creates is the coordination PID file
`ztcagent.pid` beneath the configured temporary registry directory.

At startup the agent creates and attaches the shared telemetry ring before it
scans publisher PID files. Publishers open that ring with size zero and do not
create or resize it. The fixed `ztcagent.pid` name excludes a second agent in
the same registry while remaining distinct from publisher PID files.

Configuration tuning is loaded from `ztcagent.conf` (or `--config`). Runtime
identity is supplied separately through the environment:

- `ZTC_ENROLL_TOKEN` is required and must be injected as a secret.
- `ZTC_RING` selects the shared ring and defaults to `ztc`.
- `ZTC_DIR` selects the relative PID registry beneath the system temporary
  directory and defaults to `ztc`.
- `ZTC_DEVICE_ID` is optional enrollment metadata.
- `ZTC_ENROLL_URL` is a test/development override for the authenticated HTTPS
  enrollment endpoint; production defaults to
  `https://enroll.devices.kaldron.io:443/v1/enroll`.

Enrollment sends the bootstrap token only in the HTTPS Authorization header.
The generated Ed25519 key, CSR transaction, issued client chain, assigned hub,
and supplemental hub trust remain in memory and are discarded on process
exit. An indeterminate enrollment is retried with the identical transaction;
a definitive protocol, authentication, or validation rejection fails startup.
After enrollment, the token is erased and the agent connects to the assigned
host using the issued mTLS identity. Hub reconnects reuse that identity with
bounded exponential backoff and never reuse or forward the bootstrap token.

Both telemetry-to-routing and hub-to-routing handoffs have independent frame
and byte limits. Overflow fences the current hub generation, reports bounded
loss counters, and reconnects so the hub can reissue subscriptions. The agent
never holds a shared-ring record while routing or waiting on the network, so a
slow hub cannot pin local telemetry producers. Publisher control frames use
the ring's normal stalled-reader eviction policy.

The deployment must provide shared-memory access, the PID registry directory,
runtime libraries, CA trust, log collection, secret rotation, and watchdog
policy. Logs must never contain the enrollment token, private key, CSR body, or
returned credentials. Shutdown prevents new ingress, cancels retry/reconnect
timers, drains routing, stops both clients, detaches the reader, and releases
the fixed PID-file exclusion last. Container layout, watchdogs, cluster
topology, and secret-manager integration remain deployment concerns.
