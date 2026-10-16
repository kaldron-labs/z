# Repair plan for `findings4.md`

## Objective

Repair the specific OIDC gap identified in `findings4.md`:

- recognize `offline_access` as a reserved OIDC scope;
- accept and honor `prompt=consent`;
- issue a refresh token only for an approved offline-access request; and
- preserve the existing one-time code, consent, refresh-rotation, and
  revocation behavior.

Keep the repair small. The current model already has the required durable
places: `Grant.scope`, `Grant.prompt`, `Grant.promptPresent`, `Consent`,
`ConsentGate_`, and `Client::refreshAllowed`. Do not add a new grant table,
refresh-family table, access-token table, or consent boolean unless an
implementation test demonstrates that the existing model cannot represent the
decision.

This is an intentional behavior change: ordinary authorization-code exchanges
must stop receiving refresh tokens. Propagate the change through in-tree
clients and tests; do not retain an unconditional-refresh compatibility path.

## Design decision: strict offline consent

Always require an interactive consent decision for `offline_access`, even when
the authorization policy is `Preauthorized` or an equivalent consent record
already exists. This is stricter than the minimum OIDC exception and avoids
changing the `Consent` schema merely to remember that offline access was
approved separately from ordinary scopes.

This decision is the simplest standards-safe repair:

- `offline_access` is never silently granted;
- `prompt=none` fails with `consent_required` when offline consent is needed;
- `prompt=consent` is honored; and
- an approved offline request can create a refresh family.

Document this strict policy in the implementation comments and metadata/client
documentation. Do not add an existing-consent bypass until the product has a
durable representation and tests for the OIDC exception.

## Framework and style constraints

Apply these rules to every code change:

- Use the existing `ZfStruct`/URI form machinery and `scopeContains()`; do not
  hand-code JSON, number, or form serialization/parsing. This follows
  `GUIDELINES.md` sections “Framework fit” and “Metadata”.
- Extend the existing prompt validation rather than adding a second prompt
  parser. Keep one parsed representation and one validation path. This follows
  the “Red Flag: reimplementing lower-level Z Framework capabilities” and
  “Red Flag: near-identical repeated blocks” rules.
- Do not add casts between convertible Z strings/spans, temporary contiguous
  copies, or default-initialized storage before overwrite. These are covered by
  “Type and API friction” and “Data movement and initialization”.
- Do not add heap allocation for a boolean decision or scratch parsing state.
  Use existing sentinel fields (`promptPresent`) and existing stack/scratch
  facilities if temporary storage is genuinely required, as required by
  “Storage and lifetime” and `ZtScratch` guidance.
- Use `ZuAssert` only for compile-time invariants; use the existing asynchronous
  continuation style for database operations. Do not block, poll, or read
  results after `ZmScheduler::run()`/`invoke()`; see “Assertions and type
  mechanics” and “Algorithm and control flow”.
- Keep new names concise, camel-case, and under the repository’s 28-byte limit.
  Prefer `offlineAccess`, `needsConsent`, and `refreshAllowed`; retain the
  protocol spelling `offline_access` and `prompt=consent` where they are wire
  names, as permitted by “Naming: Casing and member prefixes”. Use trailing
  underscores only for thread-local/internal functions.
- Do not introduce names such as `offlineAccessRequestedAndApproved` or
  `offlineAccessPolicyDecision`; they are redundant and exceed the intended
  vocabulary/style.

## 1. Admit `offline_access` using the existing scope model

Update `zum/src/zumd_core.cc` and its declarations only as needed. Extend the
existing scope-selection path around `scopeName()`, `addIdentityScope()`, and
`selectScopes_()` so `offline_access` is treated as a reserved identity scope:

- accept it only for an active client with `refreshAllowed` and the
  `ClientGrant::RefreshToken` grant;
- add it to `ScopeSelection.scope` using the existing scope-building logic;
- do not resolve it through application resource scopes or assign it a role or
  audience; and
- preserve it when selecting the granted scope during code and refresh-token
  processing.

Use the existing `scopeContains()` implementation in
`zum/src/zumd_jwt.cc:161` for later checks. Do not add another space-delimited
scope scanner. A single reserved-name comparison is sufficient; do not replace
the existing Z matcher/scope machinery with a new parser.

Reject `offline_access` for client-credentials and other non-interactive
flows through the existing `selection.identity`/authority checks. Do not add a
new grant kind or a new scope-ID namespace.

Use the existing `Client::refreshAllowed` value as the policy bit. If current
Tighten the existing client configuration validation so `refreshAllowed` cannot
be true without `ClientGrant::RefreshToken`; do not add a second policy field.

Relevant guidance: “Use metadata integrations instead of ad hoc parsing”,
“Use `ZuTypeList`/`ZuMatcher` for compile-time matching”, “Red Flag:
reimplementing lower-level Z Framework capabilities”, and “Red Flag: chained
`if` statements that should be `switch`” where a discrete enum branch is
involved.

## 2. Add `prompt=consent` to the existing validator

Update the prompt validation in `zum/src/zumd_oauth.cc:190-220`:

- accept the existing OIDC value `consent`;
- retain rejection of `none` combined with another prompt value;
- retain the current supported `login` behavior; and
- preserve the original prompt string in `Grant.prompt` using the existing
  `promptPresent` sentinel.

Do not add a separate `bool consent` or `bool offlineAccess` field merely to
track presence. The existing string plus presence sentinel already preserves
absent versus explicitly supplied values, matching the guideline against
separate unset/null flags.

If the current validator deliberately supports only one prompt value besides
the `none` exclusion, keep that restriction explicit and test it. Do not
expand to every OIDC prompt value as part of this repair.

## 3. Force the existing consent gate for offline access

Modify `ConsentGate_` in `zum/src/zumd_authorize.cc:109-205`, not the server
routing or session model.

Add one internal predicate, named according to the repository rules (for
example `needsConsent()`), that returns true when the existing grant contains
`offline_access` or has `prompt == "consent"`. Implement it using
`scopeContains()` and the existing prompt field.

Use that predicate in the existing policy and consent-record branches:

- `ConsentPolicy::Preauthorized` may bypass the UI only when
  `needsConsent()` is false;
- an existing `Consent` row may satisfy ordinary scope consent only when
  `needsConsent()` is false; and
- when `needsConsent()` is true, use the existing consent page and
  `authorizeConsentFinish()` flow.

Do not change `Session` or add a second consent flow. The current server paths
already pass through the consent gate for authenticated authorization flows;
the repair should make that gate recognize the two cases above. Preserve the
existing `prompt=none` behavior so a missing required consent returns
`ConsentRequired` and never renders an interactive page.

On denial, retain the existing `access_denied` redirect and state handling.
Do not allocate a refresh family before consent succeeds.

Relevant guidance: “Red Flag: highly nested logic” (use one small predicate and
early existing exits), “Red Flag: dead code”, “Amber Flag: separate `bool`
flags for unset/uninitialized/null state”, and the persistence rule to use Zdb
and sagas for durable state.

## 4. Gate refresh-family creation at the one existing call site

Update `CodeToken_::tokens_()` in `zum/src/zumd_token.cc:548-566`.

Before calling the existing `codeFamilyPrepare()`:

- require `scopeContains(m_response.scope, "offline_access")`;
- require `m_client.refreshAllowed`; and
- require the existing `ClientGrant::RefreshToken` bit.

If the condition is false, atomically consume the already-validated
authorization code through the existing asynchronous Zdb row-update machinery,
then call the existing no-family `release_()` path. The no-family
`tokenRelease()` fast path accepts an empty family ID but does not consume a
code by itself; skipping the atomic transition would create a replay
vulnerability. Do not duplicate `tokenRelease()`'s application/user/version
checks.

If the condition is true, leave `codeFamilyPrepare()`, `CodeFamily`, and the
existing saga unchanged except for the minimum data needed to preserve the
approved scope. The existing code-grant consumption and family insertion must
remain atomic.

Do not revalidate issuer, application, client, user, expiry, or authority data
in a new layer. `loadGrantAuth()`, the existing code checks, `tokenRelease()`,
and the saga already own those invariants. Add only the new refresh-eligibility
condition at the decision point.

This removes the original finding’s unconditional family creation while
preserving refresh rotation/reuse detection in `refreshMatch()` and
`refreshRotate()`.

Relevant guidance: “Red Flag: near-identical repeated blocks”, “Red Flag:
redundant validation/reimplementation” under framework fit, “Amber Flag:
algorithmic inefficiency”, and “Data movement and initialization”.

## 5. Align discovery without client-specific overengineering

Update `metadataJSON()` in `zum/src/zumd_discovery.cc:296-320` to include
`offline_access` in the existing `scopes_supported` list.

Do not make discovery metadata vary by client. The current endpoint is
application-level, not a client-registration endpoint; client eligibility is
enforced by the existing client policy during authorization. Do not add a
database scan, cached client-policy projection, or a second metadata format.

Keep `refresh_token` in `grant_types_supported` because the server supports the
grant for eligible clients. Metadata must advertise only capabilities that the
application authorization server actually implements.

Use the existing `ZfJSON` metadata serialization. Do not manually construct or
compare JSON. This follows the “Metadata” and “Framework fit” rules.

## 6. Do not change the persisted schema

No new `Grant` or `Consent` field is required by this plan:

- `Grant.scope` records the requested/granted `offline_access` member;
- `Grant.prompt` and `Grant.promptPresent` record the consent prompt;
- `Consent` continues to record ordinary resource-scope consent; and
- strict re-consent means no separate remembered offline-consent bit is needed.

Do not add a migration, FlatBuffers field, table, or compatibility decoder.
Only revisit this decision if an integration test proves that the current grant
state cannot distinguish an approved offline request from an ordinary code
grant. If that happens, use the existing `ZfbStruct` schema/persistence path;
do not call FlatBuffers APIs directly. This follows “Persistence”, “Metadata”,
and the red flag against unnecessary storage and compatibility code.

## 7. Focused tests

Add the smallest tests that prove the changed decisions. Unit tests belong in
`zum/test` and complete HTTP/database/OIDC flows belong in `zum/itest`, per the
module-layout and testing guidance.

Cover:

1. Discovery includes `offline_access` and remains valid JSON through the
   existing `ZfJSON` path.
2. `offline_access` is accepted for an eligible authorization-code client and
   rejected for an ineligible client or client-credentials request.
3. `prompt=consent` is accepted; `prompt=none consent` remains rejected.
4. An ordinary authorization-code exchange returns no refresh token and
   creates no refresh-family row.
5. An approved `offline_access` exchange returns a refresh token and creates
   one family through the existing saga.
6. Offline access still shows consent under `Preauthorized` policy and when an
   ordinary consent row already exists.
7. Denied offline consent returns `access_denied` and creates no family.
8. `prompt=none` with required offline consent returns `consent_required`,
   preserves `state`, and does not render a page.
9. Refresh rotation, spent-token reuse detection, family revocation, and
   scope-narrowing continue to pass using the existing refresh tests.

Use `ZuTest`/`ZuTestUtil` for unit tests and `ZmBlock` or `ZmSemaphore` for
completion of asynchronous integration work. Do not sleep, poll, or use time
intervals to wait for database/saga completion.

## Build and validation policy

Retain the current configured build throughout this repair. Follow the build
rules in `GUIDELINES.md` under “Build configuration” and “Acceptance”:

- Batch all production-source edits, then all test/dependent edits. Do not
  rebuild or retest after individual edits or small intermediate changes.
- Rebuild incrementally with the existing makefiles and current object/library
  artifacts. Do not run `configure`, `z.config`, `autoreconf`, or a clean
  build.
- Keep the current compiler, optimization/debug flags, warning flags, and
  platform. Other compilers, release/debug configurations, sanitizer builds,
  and platforms are out of scope.
- Defer rebuilding `zum/test`, `zum/itest`, examples, and other dependents
  until all implementation and test changes are complete. Build changed
  `zum/src` first at the final implementation milestone, then build the
  dependents in dependency order.
- Use Valgrind sparingly. Invoke it only when investigating suspected memory
  corruption, or as the final memory-leak/corruption gate after the normal
  build and tests pass. Do not make repeated Valgrind runs part of each
  milestone; use `libtool exec` for source-tree binaries as required by the
  repository debugging guidance.

## Implementation order

1. Extend scope selection for reserved `offline_access` and use
   `refreshAllowed`/the refresh grant bit.
2. Extend the existing prompt validator with `consent`.
3. Make `ConsentGate_` force explicit consent for offline access and
   `prompt=consent`.
4. Gate the existing `codeFamilyPrepare()` call; atomically consume ordinary
   codes, then use `release_()` for their no-family response path.
5. Add `offline_access` to discovery with the existing `ZfJSON` serializer.
6. Add focused unit and integration tests, then propagate any compile errors
   through in-tree dependents without compatibility shims.

After step 6, and only after all production, test, and dependent source edits
are complete, reach the single build milestone:

1. Incrementally build `zum/src` with the retained configuration.
2. Build the default targets for `zum/test` and the relevant `zum/itest`
   targets, reusing the updated `zum/src` artifacts.
3. Run the focused and existing tests.
4. If the normal tests expose memory corruption, use Valgrind to diagnose it.
   Otherwise, run one final Valgrind leak/corruption gate only if the target
   is available and practical; do not create another build configuration.

## Verification and completion criteria

Run:

- `git diff --check`;
- a source search confirming `offline_access` has one scope-admission path,
  one consent-gate path, one family-creation gate, and matching discovery;
- a search confirming no new ad-hoc JSON/form/number parser, unnecessary cast,
  raw heap allocation, duplicate consent path, or schema migration was added;
- `make -C zum/src -j8`;
- `make -C zum/test -j8 && make -C zum/test test`; and
- the relevant `zum/itest` default build and test target.

Run the final Valgrind gate only as described in the build policy above, using
`libtool exec`; do not run binaries directly from `.libs`.

The repair is complete when ordinary code grants do not create refresh
families, approved offline grants do, offline consent is always explicit and
fail-closed for `prompt=none`, discovery advertises the supported scope, and
all existing authorization, OIDC, refresh, revocation, and persistence tests
pass.
