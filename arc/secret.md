# Secret-container migration plan

## Objective and boundary

Move *owned, variable-size data already subject to an explicit `ZuClear`*
into `ZtArraySecret<true, ...>` or `ZtStringSecret<true, ...>` where that
improves its existing wipe contract. Keep the existing `Secret = false`
behavior and performance of general-purpose containers.
This is a storage-lifetime change, not a mandate to replace every `ZuClear`.
The `ZuClear` inventory is a search aid, not a migration target or a measure
of progress. Re-run it against the current tree before making any edit.

Before each edit, identify the existing `ZuClear` call on the **same object or
its owned backing buffer**, establish who owns the bytes and *when* the call
wipes them. That call is a prerequisite for migrating an owner, not an
instruction to migrate: do not infer `Secret` from a field name, a
neighboring wipe, or a copy of the same value elsewhere. An otherwise
unwiped, dedicated temporary may also become secret-typed solely to preserve
an existing move into that migrated owner; do not add a copy or broaden a
mixed-purpose type for this. Migrate only if the policy improves the
established contract without adding an allocation, copy, wider type change,
or delayed wipe. Preserve unrelated dirty work. Follow `GUIDELINES.md`'s repair
procedure, storage/capacity and heap flags,
and its dependent-compatibility rule.

Use the existing NTP composition, retaining each buffer's heap policy and
tuning where practical (the IDs below are illustrative):

```cpp
using SecretText = ZtString<ZtStringSecret<true,
  ZtStringHeapID<"Module.SecretText">>>;
using SecretBytes = ZtBArray<ZtArraySecret<true,
  ZtArrayHeapID<"Module.SecretBytes">>>;
```

These are examples, not a request to create a repository-wide secret alias.
`Secret` is valid only for primitive elements. Preserve wire/schema field
names, `ZfStruct` metadata, and app-facing semantics when changing a field's
C++ storage type. Propagate genuinely breaking type changes to dependents;
do not add compatibility wrappers merely to avoid the propagation.

## Non-negotiable wiping rules

- `ZtArraySecret` clears its owned **logical length**, not spare capacity;
  `ZtStringSecret` currently clears its owned capacity at release. Do not
  preserve a former `ZuClear(data, size())` merely because it wiped capacity:
  unwritten spare capacity is not secret data. Trace actual writes instead.
  A logical-length wipe suffices if all secret-bearing bytes are within the
  logical length on success and the writer clears every byte it touched on
  failure. Retain an explicit span wipe only for demonstrated secret-bearing
  bytes outside the logical length. Check stack-backed `ZtScratch` separately:
  its array/string base destructor runs before its backing allocator is
  released. Remove a guard only after proving equivalent byte extent and
  timing, including partial writes and exceptions.
- Do not presume an explicit early wipe is necessary. Identify the last use
  of that particular copy, how long its owner remains live afterward, and
  other copies that remain. Keep an early erasure only if it materially
  shortens exposure or is required before reuse/handoff; a secret-aware
  replacement assignment or imminent scope exit may already suffice. Where
  early erasure is justified, use `null()`/`clear()` only if the container owns
  the bytes and the changed length/state is acceptable. As a general rule,
  **do not call `ZuClear` on an owner changed to `...Secret<true>`**: let
  destruction, replacement, shortening, `clear()` or `null()` perform its
  erasure. An in-place `ZuClear(x)` leaves `x.length()` unchanged; if that
  exact behavior is genuinely required, do not claim the policy alone
  replaces it. Keep `ZuClear` for separate borrowed/output spans or fixed
  buffers only when their own contract warrants it, not as a second wipe of
  the secret container.
- Do not mark ciphertext, public identifiers, public JWKs, public URLs,
  ordinary digests, generic HTTP bodies, or mixed-purpose common aliases
  secret merely because nearby code uses `ZuClear`. Assess a digest that is
  itself a credential or password-equivalent value on its own contract. Do
  not switch all of `Zum::String`, `Zum::Bytes`, `ZumVaultClient::Text`,
  `ZumVaultService::Text`, or `OAuthString` wholesale without proving every
  use has the same sensitivity.
- Keep protocol- or crypto-sized primitive stack arrays, `ZmScratch` buffers,
  caller-owned output spans, and pooled `ZiIOBuf` frames as they are. Their
  present explicit wipes can be required at a precise failure boundary; a
  `ZtArray`/`ZtString` would not improve ownership and may add work.
- A secret-policy type does not make copies outside the container disappear.
  Review serialization, callbacks, captures, `ZuSpan` borrowing, and moves
  for additional live copies; do not claim a guard is redundant solely
  because one field became secret-aware.
- Do not extend the migration to an unwiped source, destination, schema field,
  or related credential just because a different copy is currently wiped.
  The sole exception is a dedicated temporary that already moves into a
  migrated, formerly wiped owner: it may use the matching secret type to
  preserve that move. Do not change serialized names, add copies, or widen a
  shared type to make the exception work. Record the pre-existing `ZuClear`
  site for each migrated owner and the exact move chain for each exception.
  Otherwise, leave unwiped types unchanged and report separate security
  concerns outside this migration.
- For every proposed replacement, record: clear site and exact byte extent;
  first and last secret-bearing writes; whether the object is owned, shadowed,
  or borrowed; the earliest required erasure point; move/copy/callback aliases;
  and whether the type change changes an API, parser, or serialized schema.
  A field name or a destructor wipe alone proves neither that all values are
  secret nor that the policy is an equivalent replacement.

## 1. Establish the container gate

Review the current `ZtArray.hh` and `ZtString.hh` implementation before
migrating dependents. Verify, with focused `zt/test` cases, in-place and
reallocating `splice`, shorter replacement results, `length` reduction,
assignment, `clear`/`null`, destruction, unchanged shadowing semantics, and
stack-backed `ZtScratch`. Verify that only owned mutable storage is wiped.
Check that non-secret tests still exercise the
original path and that `Secret = false` adds no cleanup work. Fix any actual
gap in the containers first, within `zt` scope; do not add per-dependent
workarounds. The already migrated `ZtlsVaultFixture.cc` is the small
reference for replacing hand-written save/shrink wiping.

## 2. Vault owners: approve only equivalent replacements

| Owner | Intended change |
| --- | --- |
| `ztls/src/ZtlsVault.cc:54,85,89` | `Entry_::value` is an approved secret-array candidate: the existing destructor wipes its logical length. The manual `Ephemeral_::save` replacement is **conditional** on proving `assign_` preserves overlapping source/destination semantics and wipes the discarded old logical bytes. Do not replace the manual path until both are tested. |
| `ztls/src/ZtlsVault.cc:193,214,297` | `FileText` is an approved candidate: each guard wipes the file's plaintext JSON buffer by logical length. Check partial-read/write paths before removing guards; keep the file parser's mutable views valid until their last use. |
| `ztls/src/ZtlsVault.cc:436,470` | Decrypted `plain` is an approved secret-array candidate. `decrypt_()` reserves `size + extra` but sets `length` only on successful `Age::decrypt`; `readPayload_()` already wipes its written output on failure (including the attempted authentication chunk). The unused reserve need not be wiped. After migration, test successful shortening/reallocation and failed decryption, then remove the overbroad `size()` guards if no other writer leaves secret bytes beyond `length()`. |
| `ztls/test/ZtlsVaultTest.cc:481,513,538,646` | `initialText`, `text`, `plain`, `checkPlain` are candidates: each is constructed with `length == size`, so a secret-array destructor covers the same range as these guards. For the decrypted buffers, `Age::decrypt` itself wipes partial output on failure. Do not preserve a capacity wipe solely because it is broader. Do not convert encoded fixtures, fixed arrays, or borrowed spans by association. |
| `ztls/interop/ztlsageinterop.cc:71` | `keyText` is **mixed-purpose** (public recipient and private identity/key input). Do not mark the shared variable secret solely to remove its wipe. The current guard is a scope-exit wipe; a separate private-key-only owner is a candidate if it preserves that lifetime without adding copies. |
| `ztls/test/ZtlsAgeTest.cc:129` | `recovered` is wiped inside the size loop, before reuse. A secret type would wipe at destruction but would **not** replace this early wipe; retain `ZuClear(recovered.data(), size)`. A distinct secret type is optional defense in depth, not an approved substitution. |

Keep this first milestone inside `ztls` and its focused tests/interop driver.
Do not redesign `ZtlsAge` or the vault API.
For `Entry_::value`, preserve the pre-migration in-place save of an
overlapping shorter span. The secret primitive-array `assign_` path must
`memmove` such a span before shortening/wiping the old tail; the ephemeral
vault test must assert that the buffer address is unchanged.

## 3. Split vault credential fields by sensitivity

In `zum/src/ZumVaultClient.hh`, keep `issuerURL`, `audience`,
`clientID`, and scope in the existing ordinary text type. The existing
`ZuClear(credential.accessToken)` and `ZuClear(credential.refreshToken)`
make those two fields candidates for secret text. In
`zum/src/ZumVaultService.hh`, keep `issuerURL` and `clientID` ordinary; the
explicitly wiped `clientSecret` and
`callbackAuth` fields are candidates. The existing `ZuClear(text)` guards
make the serialized JSON `ZtScratch` buffers candidates too: their base
destructor must wipe before the stack/heap fallback buffer is released.
Preserve JSON field IDs and validation. Verify that invalid-result assignment
wipes the old secret fields, then delete the explicit `ZuClear` calls on those
fields. If a field needs erasure before that assignment, use its `null()` at
the demonstrated last-use point. Delete the `ZuClear(text)` guards once the
secret scratch owner's destructor is verified; do not double-wipe.

Propagate the field-type change through the actual clients and fixtures:
`zum/src/zum.cc`, `zum/example/native.hh`, `zum/example/zumpingd.cc`,
`zum/itest/zumvaulttest.cc`, `zdash/src/zdash_oauth.hh`,
`ztc/example/ztchub_client.cc`, and `ztc/src/ztchub.cc` only as required by
the actual field-type change. Their presence in the dependent list does not
authorize migrating their other buffers. Review authorization and HTTP-body
buffers individually; only buffers with their own existing `ZuClear` are
candidates. Do not add `Secret` to an unwiped destination merely because its
input came from a wiped field. `TokenWire` already has its own token wipe;
the narrow move-preserving exception applies to `Token`'s parsed token fields,
which already move into the migrated `Credential`. Keep the JSON field IDs
unchanged.

## 4. Application/client sites: early erasure and mixed content

- `ztc/src/ztcagent_daemon.cc:763`: `env.accessToken` is wiped and nulled
  immediately after constructing `Link`. A secret field may protect other
  exits, but its destructor is **not** a substitute for that early wipe.
  `authorization` and the `Link` copy have no corresponding wipe; do not
  migrate them under this plan.
- `zdash/src/zdash.cc:1068`: `authorization` is wiped immediately after
  constructing `Link`; keep that timing. A secret local is optional only if
  its type fits the `Link` API without another copy. In
  `zdash/test/ZdashTest.cc:52`, the environment-sourced `token` guard is a
  lifetime-wipe candidate, but `session.token` is a separate copy with no
  existing wipe. The `ZiIOBuf` frame at line 87 is not a container migration.
- `zrest/example/zrest.cc:155,173,186,537,953-954,1029`: define a distinct
  `OAuthSecret` with the same string and heap policy as `OAuthString` plus
  `ZtStringSecret<true, ...>`. Use it for the fields/locals already passed to
  `wipe()`: `Result_::body`, `Call_::body`/`authorization`, token access and
  refresh, callback code, verifier and state. A body need not be *always*
  sensitive: the existing owner already wipes it, and secret typing preserves
  that policy. In `tokenResponse`, `tokens.clear()` immediately precedes
  replacing both tokens; secret-aware assignment can wipe the old values,
  making that preliminary clear redundant. After serializing the code and
  verifier, `callback.code` and `verifier` remain live across the request loop:
  `null()` can still shorten the lifetime of those particular copies, though
  the request/form copies remain. The final `tokens.clear()` and
  `wipe(state)` only advance erasure ahead of shutdown/function exit; retain
  them only if that interval matters, not by default. Delete the `wipe()`
  helper during this migration. It is **not** merely a forwarder to `ZuClear`:
  it also tests mutability and calls `null()`. For migrated `OAuthSecret`
  owners, replace a justified early `wipe(x)` with `x.null()`; remove
  destructor-only calls and redundant early calls. Do **not** replace them
  with direct `ZuClear(x)`, which would defeat the point of the secret policy.
  Propagate the distinct type through `Client::submit`/`perform`,
  builder body accessors, `bearer`, and actual moves so a conversion back to
  `OAuthString` does not create an unprotected copy. Keep general
  `OAuthString` for public-purpose fields. `OAuthTokenRes` is a dedicated
  parsed temporary whose token fields already move into `Tokens`; its secret
  token fields are the move-preserving exception, not an independent owner
  migration. `OAuthCodeTokenReq` copies of
  code/verifier have no existing wipe; they remain a separate security issue,
  not an excuse to broaden this migration silently.
- `zum/example/pinghttp.cc:40,107-116`: `Call_::authorization` and `body`
  are wiped at `finish()` as well as destruction. Evaluate whether `finish()`
  meaningfully precedes destruction; if so, `null()` on a secret owner can
  preserve that boundary without a separate in-place wipe. Otherwise rely on
  destruction; do not keep redundant clears. Assess whether every body is
  actually confidential before typing it as
  secret. `ResponseData_::body` handles multiple statuses and arbitrary
  responses, so **reject** a blanket secret type despite its destructor
  wipe. Keep the existing wipe unless a narrower sensitive response owner is
  established.

## 5. Zum server: classify before changing types

The following are **audit sites**, not a blanket authorization to change
types or delete wipes. Introduce narrowly scoped secret text/byte aliases
only for demonstrated owned buffers that benefit from the policy, retaining
existing heap identities. Keep general `String` and `Bytes` unchanged; avoid
new aliases in `ZumTypes.hh` unless multiple proven owners need precisely the
same type. Propagate only genuine type changes. `ZuClear(x)` on a still-live
object often marks a security boundary that a destructor cannot replace.

| Files | Candidate data; boundary to preserve |
| --- | --- |
| `ZumService.cc:230,235,309,312,481,791-886,946-953` | `json`/`wire.accessToken`/`accessToken` and configured secrets are potential owners. For each replacement/rejection/shutdown clear, compare last use with actual destruction; secret-aware assignment or imminent scope exit may make the manual wipe redundant. `plain`/`encoded` Basic-auth intermediates need separate sensitivity review; encoded credentials remain credential-bearing. No direct wipe site in `ZumService.hh`; change it only for a proven field declaration. |
| `zumd.cc:367-415`; `zumd_daemon.cc:246,349,414,465-473,544-548,763`; `zumd_bootstrap.cc:125,428,502`; `zumd_rekey.cc:81-82,195` | DB keys, SSF values and decrypted private bytes are plausible secret owners. Test each handoff/shutdown/verification wipe against actual last use and owner lifetime; do not preserve an explicit wipe when secret-aware replacement or scope exit already clears promptly. `zumd_daemon.hh` and `zumd_bootstrap.hh` have no direct `ZuClear` sites; they are declaration/dependent files only. |
| `zumd_jwt.cc:64,70`; `zumd_secret.cc:68,118,144,151` | Private-key and decrypted `plain`/`next` bytes are candidates for secret-aware ownership. Clears directly before return may be redundant once the local destructor wipes; clears before continued work may still shorten exposure. `envelope` at `zumd_secret.cc:68` is ciphertext cleared on encryption failure: **reject** secret-container substitution under this plan. |
| `zumd_oauth.hh:73-77`; `zumd_oauth.cc:303,308,318,407`; `zumd_oidc.cc:773,946-948,1028,1094-1100,1243-1245` | Token fields and client secret are candidates only at their owning declarations. A secret-aware replacement may eliminate a pre-reuse clear. `raw` in `opaqueIssue` is wiped directly before return on both paths; a secret local's destructor would suffice if no secret-bearing alias survives, so do not mandate the explicit wipe. Fixed `secret` and random arrays remain fixed. OIDC `plain`, `encoded`, `authorization`, and `body` require individual ownership and content review; do not migrate public/mixed responses. `zumd_oidc.hh` is a declaration/dependent file, not an independent wipe site. |
| `zumd_token.cc:387,722-724,770,1034-1036`; `zumd_authorize.cc:75,316,464,597-602,885-890,1039-1044,1082,1107,1241`; `zumd_db_ops.hh:321,415,421`; `zumd_session.cc:147,186`; `zumd_server.cc:459,499,785,974-978,1038,1246` | Token/code/cookie/authorization owners require exact per-site review. A clear before continued processing or callback completion is not automatically needed; compare the last use, remaining copies, and time until destruction. `location`, `query`, `form`, signatures and HTTP bodies are not automatically all secret. Digest/canonical data must be judged by whether it is itself credential-equivalent, not by proximity to a wipe. Do not migrate borrowed or shadowed storage. |
| `zumd_passkey.cc:90,155,175-178,234,267,285,303,506,533,677,792,808-811,863,875,895,1103,1111`; `zumd_daemon_admin.cc:1933,1943,2547,2604,2627,2674,2705,2712,2716,2738,3453-3455,3514,3647-3734,3916-3971,4154-4161,4731` | Audit early capability/token/secret-input clears by last use; keep only those that materially precede destruction or protect reuse. Fixed random arrays and digest fields are not container substitutions. `issuer` at 3453 is a public URL with a capability appended; assess the resulting *composite* string, not the issuer field. `canonical` at 4731 may include secret-bearing request body; do not classify all canonical text as secret without its data contract. |
| `zumd_revoke.hh:222` | Response `body` is mixed remote content; reject blanket secret typing. Retain the conditional destructor wipe pending a narrower response contract. |
| `ZumSecretTest.cc:33,61,74-76`; `ZumTest.cc:3112,7042` | `plain` and key objects are candidates only if owning, dynamic, and a secret type matches the current wipe timing/extent. Keep fixed scalar arrays fixed. The recovery-capability log value requires a precise owner/lifetime check; do not change the common test string alias. |

`zum/src/zumd_sagas.cc` parses a public JWK copy and is **not** a migration
target on its current contract.

## 6. Explicit non-candidates and completion audit

Do not migrate the `ZuClear` calls in `ztls/src/ZtlsAge.cc`, `ZtlsCOSE.cc`,
`ZtlsHPKE.cc`, `ZtlsKEM.hh`, `ZtlsOpenSSL.cc`, `ZtlsPK.hh`, or `ZtlsSec.cc`
merely to eliminate calls: they clear bounded stack/`ZmScratch` material or
caller-owned outputs on failure. Apply the same rule to the fixed-array
crypto tests (`ZtlsHPKETest`, `ZtlsHybridTest`, `ZtlsKEMTest`, `ZtlsPKTest`,
`ZtlsSecTest`, `ZtlsX25519Test`), `ztls/interop/ztlsvaultinterop.cc`, and
`zu/test/ZuLibTest.cc`. `ZuLib.hh` defines `ZuClear`; `ZtArray.hh` and
`ZtString.hh` implement the policy, so none of those are dependent migrations.

After each milestone, rerun `rg -n 'ZuClear\s*\('` and classify every
remaining occurrence as: immediate wipe still required, borrowed output,
fixed/stack scratch, pooled I/O, nonsensitive/mixed-purpose data, or an
unfinished owned-secret migration. A zero-call target is **not** the goal.
For every added `Secret<true>` declaration, cite the pre-existing wipe site
or the dedicated move into a migrated, formerly wiped owner in the review
notes; a migrated owner without either fails this plan's scope gate. The
policy's own focused test types are not migrations and instead must test a
specific wiping invariant.
Do not retain a redundant explicit wipe solely because it predated the
policy, and do not remove one without proving the new erasure point.

Batch code changes by milestone. Use the current Linux + clang + debug
configuration and existing incremental build artifacts; do not reconfigure
for other compilers, build flags or platforms as part of this work. Rebuild
infrequently, rebuild each changed `src` before its dependent tests, and
defer broad dependent rebuilding until the end. Run focused TAP tests for
each changed module and relevant vault/age interop checks; use Valgrind
sparingly for a suspected corruption issue or the final leak/corruption
gate. Finish with `git diff --check`, a warning review, and a file-by-file
audit for extra copies, allocations and changed non-secret behavior.

## Implementation review record

The current migrations have these former erasure points or move chains:

| Secret owner or type | Former wipe / move chain |
| --- | --- |
| `Vault_::Entry_::value`, `FileText`, decrypted `plain` | `ZtlsVault.cc` entry destructor, file-text guards, and decrypted-plain guards respectively; overlapping ephemeral save remains in place and wipes the shortened tail. |
| Vault fixture `Value` and vault-test plaintext buffers | Fixture's manual replacement wipe and `ZtlsVaultTest.cc` plaintext guards; fixed passphrases and borrowed outputs retain their explicit wipes. |
| `ZumVaultClient::Credential` token fields and `ZumVaultService::Credential` secret fields | The invalid-result `ZuClear` calls in their `load` functions; secret `ZtScratch` JSON replaces each `save` guard. |
| Parsed `TokenWire` fields in native, ztc and zdash clients | Each `tokenJSON` guard formerly wiped those fields, which still move into `Credential`. The parsed `Token` in `zum.cc` is the dedicated move-preserving exception. |
| zrest `Result_`, `Call_`, `Tokens`, callback and local secret values | Former `wipe()` calls and destructors in `zrest.cc`; form/bearer builders and `OAuthTokenRes` token fields are dedicated move-preserving intermediates. The server JWT writer now accepts the latter type directly. |
| ztc agent `accessToken`; zdash authorization and test token | `ztcagent_daemon.cc`'s pre-link wipe/null, `zdash.cc`'s post-link wipe, and `ZdashTest.cc`'s token guard respectively. The ztc early `null()` remains. |
| Zum private/raw bytes and Basic-auth text | `zumd_jwt.cc` private-key wipes, `zumd_oauth.cc` raw wipe, `zumd_oidc.cc` Basic-auth wipes, and `ZumSecretTest.cc` key wipes. Public key bytes remain ordinary. |
| Zum recovery-capability test string | `ZumTest.cc`'s former explicit clear; its explicit `null()` still marks the original teardown point. |

The added secret types in `zt/test` exercise policy behavior; they do not
replace an application wipe. Remaining `ZuClear` calls in mixed HTTP bodies,
borrowed crypto outputs, bounded stack buffers, and pre-handoff/early-erasure
sites are not a zero-call cleanup target. In particular, do not migrate Zum
server token/code/capability owners by association: the outstanding
cross-owner handoffs must be proved copy-neutral and timing-equivalent before
changing their common types.

`Zum::TokenResponse` is deliberately not migrated in this batch:
`zumd_token.cc` moves `PreparedJWT::token` and the refresh-rotation callback's
`String` directly into its token fields. Changing just those fields to
`SecretString` would replace those moves with copies; widening their producers
would cross the JWT and database-callback contracts. Its `tokenClear` calls
also erase rejected or superseded responses before their enclosing async
objects finish. Similarly, `ZumService.cc`'s verification token is a `String`
passed through public and callback boundaries, and its clears precede
callbacks or token handoff. Keep those explicit boundaries until a separate,
copy-neutral owner migration is demonstrated.
`serverSecretDecrypt` likewise fills a caller-owned `Bytes &` via an
intermediate `Bytes` move. Its failure-path wipe, the rekey plaintext wipes,
and daemon private-key wipes remain explicit: changing only one local to
`Secret` would introduce a conversion copy, while changing the output
contract would require a separate caller-wide migration. The `Rekey__` key
members also receive moved `Bytes` constructor parameters, so they are not
independent secret-type substitutions.

The remaining authorization/token/session clears in `zumd_authorize.cc`,
`zumd_token.cc`, and `zumd_session.cc` mark actual async boundaries. Codes,
tokens, query/form bodies, and authorization strings are `String` API or
callback values, commonly moved into request state; changing only a member
would add a copy. `AuthorizeRequest::clear_`, the token/revoke request
`clear_` functions, and session `use_`/`revoke_` erase their copies before
subsequent processing or callback completion. `AuthorizeConsentFinish__`
erases its code on completion as well as destruction. Retain those wipes;
do not widen the shared `String` contract to eliminate them.

In `zumd_server.cc`, the CSRF digest is bounded fixed storage. Cookies,
tokens, submitted/expected CSRF values, and signatures are cleared after
their last use but before the surrounding request or callback necessarily
ends; their ordinary `String` types cross existing interfaces. In
`zumd_db_ops.hh`, the code and next token are cleared on commit failure but
moved through the completion callback on success. Secret-typing those locals
alone would convert the successful moves into copies. These are retained
boundary wipes, not unfinished owner migrations.

`zumd_passkey.cc`'s capability and token values arrive as or leave through
`String` callback parameters. `EnrollmentBegin__` and `RecoveryBegin__`
erase capability text as soon as it is parsed, then retain only the parsed
ID/digest; their `clear_` also covers the retained digest on failure and
cancellation. Local random arrays are fixed-size; grant digests are cleared
at their failure boundary.
`zumd_bootstrap.cc` similarly
clears its DB key before completion and its callback token immediately after
writing the capability. Keep these timing boundaries without changing the
shared callback types.

`zumd_oidc.cc` clears `OIDCConfig::clientSecret` when configuration is
rejected or the request ends, and clears the request's verifier and tokens
when it completes. Its access token is also cleared immediately after
building a user-info Authorization header, before the HTTP callback. The
HTTP call object's Authorization header and body are cleared at `finish()`;
its response body is mixed remote content. These are distinct copies and
early handoff boundaries, not a basis for converting all OIDC `String`
fields. The matching `native.hh`, `zdash_oauth.hh`, and
`ztchub_client.cc` clears act on borrowed HTTP spans or on local callback
values before handoff; keep their separate ownership contracts.

`zumd_daemon_admin.cc`'s cursor `raw` buffers hold cursor framing and MACs,
not the source secret, while its entropy sources are fixed arrays. The
invitation URL
becomes capability-bearing only after appending the token; both URL and
token are cleared before the async completion callback. Client-secret and
recovery-capability values likewise pass through `String` callbacks and
are cleared at that boundary. Provider input secrets and signing private
material are erased immediately after encryption or validation, before
database work; the signing `plain` buffer is caller-facing `Bytes` along the
existing crypto path. Changing isolated locals would not eliminate the
upstream input or callback copies. The idempotency canonical buffer may
contain a secret-bearing body but is mixed request text; its explicit clear
at the digest boundary remains appropriate.
