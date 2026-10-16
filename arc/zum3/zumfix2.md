# Refresh-token separation and bounded retention

## Objective

Make the persisted refresh-token state explicit as `zum.refresh`, add
periodic daemon-owned cleanup, and prevent evidence and consent records from
surviving the application that owns them.

The implementation must preserve the existing fail-closed authorization
semantics, refresh-token replay detection, saga recovery, and shard ownership.
Cleanup is maintenance, not an authorization shortcut: expired rows must not
become valid merely because cleanup has not run yet.

## Target model

```text
zum.grant
  authorization/passkey ceremonies and codes
  short-lived protocol state

zum.refresh
  one persisted row per refresh-token family
  current digest, spent digests, generation, expiry, revocation state
  authorization snapshot needed to revalidate rotation

zum.session
  live browser sessions, removed after their idle/absolute deadlines

zum.evidence
  current external-authority observation, scoped to app/user/provider

zum.consent
  durable user approval, scoped to user/client/app
```

`zum.refresh` must not be interpreted as persisted access-token state. Access
tokens remain signed JWTs. The table stores refresh-token families, including
bounded replay history for spent token digests.

## 1. Split out `zum.refresh`

### Data model and schema

- Define a new `Refresh` record and `RefreshTable`, with a primary key equal to
  the opaque refresh-family ID.
- Move refresh-only fields out of `Grant`: current digest, spent digests,
  generation, refresh-family expiry, and any refresh-specific lifecycle data.
- Retain on `Refresh` the app, client, user, issuer/audience, role/action
  snapshot, auth/membership/policy/evidence versions, and other data required
  to validate a rotation without consulting a mutable or unrelated grant.
- Decide whether the row stores `state` and `version` using the same optimistic
  ownership conventions as other persistent records. It must support atomic
  rotation, revocation, and recovery.
- Add `zum_refresh.fbs`, generated headers/build inputs, table registration in
  `DBContext`, and schema registration as `zum.refresh`.
- Increment the schema/model version as required by the repository’s
  unsupported-old-store policy. Do not silently reinterpret existing
  `zum.grant` rows as the new format.

### Runtime changes

- Change refresh-family creation after authorization-code redemption to insert
  a `Refresh` row rather than a refresh-kind `Grant` row.
- Change refresh-token parsing, lookup, rotation, generation-limit checks,
  spent-digest checks, and expiry checks to use `RefreshTable`.
- Change refresh revocation and SSF refresh-family notifications to identify
  `Refresh` rows.
- Remove refresh-specific branches from grant loading and grant cleanup.
- Ensure refresh rotation remains one atomic owner-checked mutation:
  presented digest becomes spent, generation increments, and the replacement
  digest is committed together.
- Preserve the current behavior for reuse of spent tokens: it must continue to
  revoke or invalidate the family according to the existing replay policy.
- Audit all `GrantKind::Refresh` assumptions and replace them with explicit
  `Refresh` operations rather than retaining a compatibility enum path.
- Update administrative query/revoke behavior. If refresh families remain
  visible through grant management, define an explicit projection; otherwise
  expose them through a clearly named refresh-family operation. Never expose
  plaintext digests or token material.

### Lifecycle

- Refresh rows must be removed after family expiry or explicit revocation once
  the configured retention/replay window has elapsed.
- Bound `spent` history by the existing spent-token limit and document the
  resulting replay-detection tradeoff.
- App disablement must immediately prevent refresh use through normal app and
  version checks; physical cleanup may occur later.

## 2. Add a configurable `zumd` cleanup interval

- Add a daemon configuration property, for example
  `cleanupInterval`, expressed in seconds and documented in the configuration
  reference.
- Choose and document a nonzero production default. Define whether `0` means
  disabled; if so, retain the administrative cleanup operation as the manual
  fallback.
- Validate the value and reject nonsensical intervals that could create a busy
  loop. The interval must not be confused with token/session lifetimes.
- Pass the value through `zumd` startup into `DaemonConfig`.
- Add a daemon-owned repeating timer using the project scheduler/event-loop
  conventions. The timer must:
  - arm only after the daemon and database are ready;
  - never overlap cleanup runs;
  - reschedule after completion, including failure;
  - stop before database/context teardown;
  - avoid retaining database-owned objects after `Daemon::stop()` or
    `Daemon::final()`.
- Keep cleanup work bounded per run with a configurable or documented batch
  limit. A large expired population must be drained over multiple intervals,
  not through one unbounded scan or saga payload.
- Run scans and mutations on the owning database shard. Do not add locks or
  cross-shard reads to make cleanup convenient.
- Emit useful maintenance metrics/logging: started, completed, skipped due to
  an active run, rows selected, rows deleted, and failures.

## 3. Delete stale grants and sessions periodically

### Grants

- Define stale precisely as an unowned, non-pending grant whose expiry is at
  or before the cleanup timestamp. Include expired ceremonies and codes, not
  only authorization codes.
- Exclude active, owned, or otherwise in-flight rows. Cleanup must skip a row
  if ownership or the expiry/version snapshot changed between selection and
  deletion.
- Reuse or refactor `GrantCleanup` so the internal maintenance path can submit
  bounded, recoverable deletion batches without requiring an HTTP admin
  request. Preserve the existing manual `grantCleanup` endpoint.
- After `zum.refresh` is split out, do not delete refresh rows through the
  grant-cleanup saga. Add refresh-family cleanup to the appropriate maintenance
  pass with its own row validation.
- Decide whether expired rows are deleted immediately or retained for a
  bounded audit/replay interval. The choice must be explicit and reflected in
  query/test expectations.

### Sessions

- Define stale as an unowned session with `idleDeadline <= now` or
  `absoluteDeadline <= now`, regardless of whether its state still says
  `Active`.
- Add a bounded, recoverable session-deletion operation. Do not use session
  revocation alone if the requirement is physical deletion.
- Validate the selected session’s deadlines, state, version, and ownership
  before deletion so a concurrently used or renewed session is preserved.
- Ensure session lookup treats an expired row as invalid even if the periodic
  cleanup has not yet run.
- Preserve explicit administrative session revocation as a separate operation.

### Failure and restart behavior

- Cleanup failure must not stop `zumd` or block authorization traffic.
- A partially completed batch must be safely retryable.
- Restart must resume naturally from table scans; no in-memory list of pending
  rows may be required.
- Add backpressure so repeated database failures do not cause an immediate
  retry storm.

## 4. Remove evidence and consent when an app is deleted

### Define deletion semantics

- Clarify whether `Disabled` is logical deletion or whether a new physical
  app-delete operation is required. State transitions must close authorization
  before dependent data is removed.
- Treat app deletion as a coordinated lifecycle operation, not as a direct
  delete of the `App` row followed by best-effort child scans.
- Mark the app inactive and increment its authorization version first. This
  prevents new authorization, refresh, evidence refresh, and consent approval
  while cleanup is in progress.

### Cascade cleanup

- Add app-scoped evidence deletion to the app deletion saga. Select by
  `Evidence.appID`, delete only unowned rows, and use before-image/version
  checks for concurrent safety.
- Add app-scoped consent deletion to the same saga. Select by `Consent.appID`
  and delete all matching user/client consent rows, including revoked rows.
- Process both tables in bounded batches rather than embedding an unbounded
  vector of rows in one saga payload.
- Make deletion idempotent: missing rows count as already cleaned, while
  ownership/version conflicts cause retry or a visible incomplete state.
- Order cleanup so grants, refresh families, sessions, role mappings, and
  other app-owned authorization state cannot retain live references to a
  deleted app. At minimum, explicitly audit those references even though this
  request specifically requires evidence and consent removal.
- Decide whether providers and external identities are app-owned or shared.
  Do not delete shared provider configuration merely because one app is
  deleted; remove only app-scoped evidence, mappings, and relationships.
- Ensure rollback/restart recovery does not restore evidence or consent rows
  after the app deletion has reached its terminal state.

### Query and authorization behavior

- App-scoped evidence and consent queries must return no rows after deletion
  completes.
- Existing grants/sessions must fail authorization immediately from the app’s
  inactive state, even before physical cleanup finishes.
- A stale evidence or consent row must not be resurrected by replayed or
  duplicated deletion requests.

## 5. Tests

Add focused unit, persistence, and integration coverage for:

- refresh-family creation, rotation, spent-token replay, revocation, expiry,
  generation limits, and restart recovery in `zum.refresh`;
- absence of refresh-specific rows in `zum.grant` after the split;
- cleanup interval parsing, default/disabled behavior, timer shutdown, and
  non-overlapping runs;
- deletion of expired grants in bounded batches;
- deletion of idle- and absolute-expired sessions;
- concurrent renewal/revocation versus cleanup, proving live rows survive;
- cleanup retry after database failure and restart during a cleanup saga;
- app deletion removing active and revoked evidence rows;
- app deletion removing active and revoked consent rows;
- app deletion racing with authorization, refresh, evidence refresh, and
  consent approval;
- no deletion of evidence or consent belonging to another app;
- secret redaction and absence of plaintext refresh material in logs, admin
  responses, saga images, and cleanup diagnostics;
- bounded table growth under repeated expired ceremonies, sessions, refresh
  families, consent revocations, and evidence refreshes.

Update documentation with the retention model, cleanup interval, manual
cleanup fallback, refresh-table semantics, and app-deletion cascade.

## Review criteria

- `zum.grant` no longer serves as the refresh-token-family table.
- `zum.refresh` contains no access tokens and no unbounded replay history.
- Cleanup is periodic, bounded, restartable, and safe under concurrent use.
- Expired grants and sessions are invalid immediately and physically removed
  eventually.
- Deleted apps cannot retain or recreate evidence or consent rows.
- Every persistent table has a bounded cardinality, explicit expiry, or an
  explicit current-state replacement policy.
