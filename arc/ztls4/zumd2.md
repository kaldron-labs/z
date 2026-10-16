# `zumd` Vault adoption plan

## Proposed answers and boundary

`zumd` should use `Ztls::Vault` directly. Neither existing Zum adapter fits:
`ZumVaultClient` stores authorization-code access/refresh tokens; the present
`ZumVaultService::Credential` requires one OAuth client secret and one SSF
callback authorization value. `zumd` is the issuer, holds a database master key,
and can transmit to any number of SSF receivers. Do not reinterpret
`clientSecret` as `dbKey`, make `callbackAuth` a catch-all, or expand that fixed
pair into an unrelated generic schema solely for this daemon.

Vault identity has four levels: `program` → `account` → `scope` → `name`.
`Ztls::Vault` defaults an omitted program to `ZiLog::program()` and an omitted
account to `<Zi::username()>@localhost`. Those
defaults identify the application/store; neither is the credential name. The
Zum client helper always uses the purpose-specific logical key `global/oauth`;
the service helper uses `global/serviceCredentials`. Their callers pass only
`Credential` by default, with optional account/program overrides. Vault keys
have no leading `/`.
`zumd` uses neither helper and supplies its own names below.

Resolve the design questions as follows:

1. On a normal start, `ZUM_DB_KEY` provisions the current key when present;
   otherwise load it from Vault. An explicit environment value wins; do not
   silently substitute a stored key if the supplied key fails database
   activation. Presence, not nonempty content, selects the environment path:
   a set-but-empty value is malformed and must not fall back. Save a newly
   supplied current key only after successful activation/bootstrap, not merely
   after Base64 decoding. Later normal starts may omit `ZUM_DB_KEY`.
2. Under `--rekey`, load the **old** key from Vault and require `ZUM_DB_KEY`
   to supply the **new** key. Do not accept an environment old key in this mode:
   the Vault record is the recovery anchor. `ZUM_DB_NEW_KEY` is unnecessary
   and should be removed from the CLI, code, documentation, and affected test
   fixtures. After `serverRekey`, `Zdb::stop()` must report durable completion
   before replacing the stored current key. A Vault-save failure after durable
   rekey is a failed command: retain the supplied new key and retry the same
   `--rekey` invocation. `serverRekey` already recognizes the committed-new
   key check and can finish that retry; do not roll the database back or claim
   success before Vault publication. Test interruption at this boundary.
3. Use the already-renamed SSF field `secretName` to name its own credential,
   not as a second field of the database-key record. A set environment value
   takes precedence and provisions/replaces that named Vault value at startup;
   otherwise load it. Require every configured receiver's secret before
   enabling delivery. The field's value remains the environment-variable name
   used for provisioning and the Vault credential name; only the field label
   changes. The configuration, C++ members, FlatBuffers field, tests, and
   documentation have been renamed together. No schema migration or
   compatibility alias is required. Never put the callback value in the node
   file or database.
4. Do not open Vault from the SSF delivery callback on a scheduler/Rx/Tx
   shard. Resolve configured secrets once on the startup owner thread into a
   small, owned in-memory collection; the callback only selects a loaded
   value by `secretName`. The former implementation reread environment
   variables at delivery time. Changing them in a running process will no
   longer rotate a secret; use an explicit daemon restart for rotation, and
   document this behavior change. Avoid a second hash/index unless receiver
   counts justify it; a linear scan over the configured receivers is enough.
5. Set `VaultConfig.program = "zumd"`, use an issuer-specific `account` (the
   exact configured issuer URL), and retain the normal `ZUMD_HOME` override.
   These explicit values are `zumd`'s program/account identity, not credential
   names. Use `global/dbKey` for the raw 32-byte key and
   `env/ssf/<secretName>` for callback authorization bytes. Pass the purpose
   name `"dbKey"` or configured `secretName` separately from `Scope`; Vault
   composes the logical key. Do not prefix it with `/`, pass a slash inside a
   name, or encode the name yourself. Reject configured `secretName` values
   containing `/` with `receiver.secretName.find<"/">()` before Vault calls.
6. Do not default to `VaultStore::Auto`: on Linux it can fall back to the
   unencrypted `File` backend. Require an explicitly selected confidential
   backend (`KeyRing` or a deployment-provided secure `Module`) with `Direct`
   mode. Fail closed if it is unavailable. Do not use `Indirect + File` as a
   confidentiality substitute: its passphrase would reside in that same
   unencrypted backend. Permit `Ephemeral`/`File` only in isolated tests.
   Expose the selected store/module through non-secret node configuration or
   CLI using existing `Ztls::VaultConfig`; avoid a second backend system.

## Implementation

1. Add a small `zumd`-local credential loader in `zum/src` (or keep it local to
   `zumd.cc` if it stays small). It initializes, opens, loads/saves, closes,
   and finalizes `Ztls::Vault` on the startup thread. Use its mutable
   `VaultLoadFn` span directly: copy the stored 32-byte raw DB key into the
   destination key buffer without a Base64/JSON round trip. For provisioning,
   decode the existing Base64 environment value once with `ZuBase64`, then
   save the raw bytes. Return the original `VaultResult`/`ZeException`; do not
   collapse backend failures to a boolean. Clear temporary key/secret buffers
   on every path. Do not log their values, account, or Vault payloads.
2. Order startup so the current DB key is available before `db->start()`, but
   no Vault session stays open across DB startup or daemon operation. Stage
   any environment-provided current key for publication only after the daemon
   reaches its existing `active` readiness point (including `--once`). A
   missing key, unavailable secure backend, malformed key, or wrong explicit
   key fails startup without fallback to another source. Preserve the
   existing database/shard lifecycle and `ZmBlock` continuation boundaries.
3. After parsing the `ssf` node, collect its distinct `secretName` values,
   resolving each from the environment or Vault. Keep only one in-memory copy
   per distinct ref. Adapt `SSFSecretFn` to read this owner-prepared collection
   without blocking, filesystem access, or Vault calls on a delivery shard.
   Publish environment-provided values to Vault after successful startup;
   do not publish half-validated configuration. Clear the collection after
   delivery has drained during `daemon.stop()`/`final()`.
4. In `--rekey`, load the old key only from Vault, require and decode
   `ZUM_DB_KEY` as the new key, run the existing offline rekey, wait for the
   existing durable store drain, then save the new raw key at `global/dbKey`.
   Only then print completion. Retain the supplied new key until Vault
   publication succeeds; if publication fails, retry `--rekey` with the same
   value. Do not store `newDBKey` under a second long-lived Vault name.
5. Update `zum/README.md` and integration-test documentation for provisioning,
   restart without environment secrets, secure-backend configuration,
   multiple SSF refs, rotation via restart, and recovery from a post-commit
   Vault publication failure. Retain the CLI/env spelling for `ZUM_DB_KEY`
   and the configured environment-variable values; retain the completed
   `secretName` rename and delete `ZUM_DB_NEW_KEY` references.

## Verification and scope

- Extend `zum/itest` with disposable Vault homes and an explicit test backend:
  first provision, restart without `ZUM_DB_KEY`, explicit-key precedence,
  wrong key, secure-backend failure, two distinct SSF refs, missing ref,
  replacement after restart, `--once`, rekey using Vault-old/environment-new,
  and retry after a post-commit Vault-save failure. Adapt existing Python
  fixtures only where the
  new contract requires it; keep their SQLite/module setup intact. Never point
  a test at a real credential store or home.
- Follow `GUIDELINES.md` repair, sharding, allocation, naming, and secret
  handling rules. Use `ZtScratch` for bounded scratch with heap fallback,
  `ZiFile` for any filesystem work, Z framework parsing/formatting, and
  `ZmBlock` for existing async-to-sync transitions. Do not add locks,
  background polling, a persistent Vault session, or a generic credential
  framework.
- Retain the current Linux/clang/debug configuration. Batch edits; rebuild
  incrementally with existing artifacts only at major milestones; defer
  dependent rebuilding to the end. Other compilers, flags, and platforms are
  out of scope. Use Valgrind only for a suspected memory fault or as the final
  leak/corruption gate.
