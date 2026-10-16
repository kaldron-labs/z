# `ztcagent` Vault adoption plan

## Proposed answers and boundary

`ztcagent` should use `Ztls::Vault` directly for its confidential device-client
secret. Neither existing Zum adapter fits: `ZumVaultClient` models an
authorization-code client with refresh tokens, while the current
`ZumVaultService::Credential` requires a callback authorization value that an
agent does not have. Do not fake `callbackAuth`, store a bearer in
`clientSecret`, or broaden that adapter only to force reuse.

Resolve the design questions as follows:

1. Persist the long-lived confidential client secret, **not** the short-lived
   `ZTC_ACCESS_TOKEN`. Obtain a fresh `client_credentials` access token from
   Zum for `scope=Agent` before a WSS upgrade and again before reconnecting.
   There is no refresh-token grant in this flow. Never save access tokens in
   Vault. A stale persisted bearer cannot make a later run reliable.
2. `ZTC_CLIENT_SECRET` provisions/replaces the secret when present; otherwise
   load it. An explicit value takes precedence. Persist a newly provided
   secret only after a successful token exchange, so a rejected secret cannot
   replace a working stored one. Remove the mandatory
   `ZTC_CREDENTIAL_STORE` and `ZTC_ACCESS_TOKEN` bootstrap requirements after
   tests and documentation migrate; do not retain two competing token sources.
3. Set `VaultConfig.program = "ztcagent"`, `account` to the exact
   `issuerURL + '\n' + clientID` identity already used by Zum's Vault
   utility, and name the raw secret `global/clientSecret`. The normal
   `ZTCAGENT_HOME` override isolates multiple deployments. `deviceID` remains
   the verified service principal and normally equals `clientID`; it is not
   a second credential store key. Open Vault only for brief load/save calls.
4. Require an explicit confidential backend (`KeyRing` or a secure `Module`)
   in production, with `Direct` mode. `Auto` can fall back to plaintext
   `File`; `Indirect + File` stores its decryption passphrase in that file.
   Neither is an acceptable silent fallback for a service client secret.
   Test `Ephemeral`/`File` only with disposable fixtures. Expose backend and
   module selection as non-secret configuration using `Ztls::VaultConfig`,
   not a new credential-store abstraction.
5. Preserve HTTPS certificate and hostname verification for metadata/token
   calls, using the existing `caPath` and loopback-test exception. The
   discovered issuer and token endpoint must match the configured issuer
   contract; use the existing Zum/OAuth URI and JSON adapters and
   `ZuBase64` for Basic authorization. Do not construct JSON, percent
   encoding, or Base64 by hand, and do not log the secret or bearer.

## Implementation

1. In `ztcagent.cc`, replace `ZTC_ACCESS_TOKEN`/`ZTC_CREDENTIAL_STORE`
   ingestion with `ZTC_CLIENT_SECRET` or a short `Ztls::Vault` load. Preserve
   the other environment inputs (`ZTC_ISSUER`, `ZTC_CLIENT_ID`,
   `ZTC_DEVICE_ID`, `ZTC_WSS_URL`, CA/ring/PID settings). Return the original
   `VaultResult` error at the boundary and emit a safe diagnostic. Keep the
   secret in owned, clearable agent state only as long as token acquisition
   requires it; clear it and all token buffers at teardown.
2. Add an agent-owned asynchronous OAuth path: fetch the application metadata
   for the exact issuer, POST `grant_type=client_credentials` and
   `scope=Agent` to its discovered token endpoint with Basic client
   authentication, then parse `access_token`, `token_type`, and expiry using
   `ZfJSON`/`ZfStruct`. Reuse the `Zhttp` request/response idioms already in
   `zum/src/ZumService.cc` and `ztc/example/ztchub_client.cc`; factor only
   code that is genuinely shared. No blocking HTTP call, `ZmSemaphore` wait,
   or Vault access on an Rx/Tx callback.
3. Do not create/reuse a WSS `Link` carrying an expired Authorization header.
   Token acquisition must complete before the first upgrade and before each
   reconnect attempt; construct/update the link's authorization on the owner
   shard using a newly acquired token. Fence old link callbacks and route
   generations before replacement. Use the existing reconnect backoff for
   token/transport failures and cancel pending HTTP and timer work during
   stop/final. A token rejection is not permission to retry with a cached
   stale bearer. Keep only one in-flight token request per agent.
4. After the first successful exchange using an explicit environment secret,
   save that secret to Vault, close Vault, then continue startup. A Vault-save
   failure fails provisioning and leaves the previous stored secret intact.
   Later starts can omit the environment value. A configured secret rotation
   is effected by supplying a new value and restarting; subsequent token
   requests use the newly validated in-memory value.
5. Update `ztc/README.md`, `ztc/example/README.md` as needed, and the local
   dashboard walkthrough. The deployment adapter is no longer responsible
   for token acquisition; it may still provision `ZTC_CLIENT_SECRET` through
   the environment. Remove obsolete `ZTC_CREDENTIAL_STORE` and
   `ZTC_ACCESS_TOKEN` instructions for the agent (the wire-test fixture may
   continue to accept a supplied bearer independently).

## Verification and scope

- Update `ztc/itest/ztchubtest.py` to provision a confidential device secret
  and run the agent without a preissued bearer. Exercise first provision,
  restart without the secret environment value, invalid replacement that
  leaves the old secret usable, token endpoint outage/backoff, bearer expiry
  and reconnect with a new token, rejected/invalid token response, stop during
  in-flight HTTP, and secure-backend failure. Test issuer/CA checks with the
  existing local TLS fixture. Keep the wire-test publisher's own bearer
  injection separate from the production agent contract.
- Follow `GUIDELINES.md` sharding/ownership, repair, allocation, and naming
  rules. Snapshot only fixed metadata across shards; keep variable-size
  secret/token buffers owner-held and move handles through continuations.
  Use `ZtScratch` for bounded encoding scratch, `ZiIOBuf` for I/O bodies,
  `ZuClear` for secret-bearing storage, and `ZiFile` for filesystem work.
  Do not add a new polling thread, lock, key store, or long-lived Vault
  session.
- Retain the current Linux/clang/debug configuration. Batch edits; rebuild
  incrementally with existing artifacts only at major milestones and defer
  dependent rebuilding to the end. Other compilers, flags, and platforms are
  out of scope. Use Valgrind sparingly, as a final leak/corruption gate or to
  diagnose a demonstrated memory fault.
