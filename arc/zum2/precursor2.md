# SSF refresh-revocation-push precursor

## Outcome

Complete the shared Zum/libZum refresh-token revocation notification facility
before ztchub admission work begins. This extends the application-scoped OAuth
model in [precursor.md](precursor.md):

- zumd is the OpenID SSF 1.0 Transmitter.
- libZum is the Receiver implementation used by zumpingd, ztchub, and future
  Zum clients and resource servers.
- Delivery is a signed RFC 8417 Security Event Token through authenticated
  RFC 8935 HTTPS push.
- Recovery is durable Transmitter retry. Access-token verification remains
  self-contained; an otherwise unknown presented access token may use one
  authenticated RFC 7662 introspection after receiver state loss. There is no
  RFC 8936 delivery, periodic polling, cursor, replay endpoint, WebSocket, or
  SSE.

The work is shared authorization infrastructure, not a ztchub protocol. It
must not expose an OAuth facade, a bearer token, or application-specific agent
state.

## Exact change set

| Location | Change |
|---|---|
| zum/src/ZumJWTVerify.hh and .cc | Retain stable TokenID extraction to an already validated access-token principal for diagnostics and connection correlation; it is not a revocation handle. |
| zum/src/ZumService.hh and .cc | Add SSF Receiver configuration, refresh-revocation SET delivery, bounded SET deduplication, and a refresh-family callback. |
| zum/src/zumd_revoke.hh and .cc | Extend the existing revocation implementation with Transmitter event construction and delivery scheduling. |
| zum/src/zumd_schema.cc and fbs/ | Add only the durable records needed for configured Receivers and unacknowledged deliveries. |
| zum/example/zumpingd.cc | Route its HTTPS callback to libZum and consume refresh-family notifications through the common callback. |
| zum/test and zum/itest | Add isolated SET tests and real transmitter/receiver/restart tests. |

Do not create a second revocation daemon, generic event bus, subscription
service, WebSocket/SSE channel, polling client, or a service-specific
revocation parser.

## libZum Receiver

Retain TokenID in ZumJWTVerify.hh as the access token's exact issuer and jti for
diagnostics and connection correlation only; it never retains the bearer token
and never makes a JWT access token revocable. Add tokenID and expires to
Principal and ServicePrincipal. A purported access token without the claims
needed to form TokenID still fails the existing access-token validation path.

Extend ServiceConfig with a compact SSF Receiver section:

- callback path and authentication;
- Receiver ID;
- Transmitter issuer/discovery identity and expected Receiver audience;
- SET byte/time bounds; and
- introspection endpoint/client credentials for the on-demand fallback.

Keep management issuer/client credentials, protected issuer/audience, and SSF
configuration separate. No endpoint is constructed by concatenating strings:
use issuer discovery and typed URL metadata.

Add only these Service responsibilities:

| Operation | Behaviour |
|---|---|
| start | discover the Transmitter material and refuse readiness until the Receiver can validate delivery. |
| verify | validate the access token normally; never reject it because of a refresh-token SET. If verification state is unknown after restart/loss, introspect this presented token once or fail closed. |
| receiveSET | accept the application-routed RFC 8935 request, authenticate and validate its SET, deduplicate it, invoke the refresh-family callback once, and return the standard response. |
| setRefreshRevocationFn | install the callback receiving a refresh-family identity and expiry on the Service control shard. |

receiveSET requires explicit secevent+jwt. It uses existing Zhttp request
handling, ZfJSON, and JWT verification facilities to validate signature,
Transmitter issuer, Receiver audience, issue/expiry bounds, SET jti, event
type, and subject. It accepts only:

~~~
urn:zum:events:refresh-token-revoked
subject: opaque { iss: application issuer, family_id: encoded refresh-family id }
~~~

The callback uses `Zrest::BodyPolicy::Raw` deliberately: a compact JWS is an
opaque signed representation and must reach the existing JWT verifier
byte-for-byte. `Zrest::SignBody` is a custom prefix/suffix wrapper around a
serialized JSON body, not a JWS implementation. Adding a generic JWS body
policy would duplicate JOSE construction and key handling and is outside this
precursor.

The SET's own iss, aud, and jti remain distinct from the refresh-family
subject. Invalid input changes no state. A duplicate SET returns successful
acknowledgement but does not invoke the callback again. Receiving this event
does not reject or shorten any already-issued access token; it tells a client
holding the refresh family to stop renewal before its normal expiry.

The Receiver owns only an intrusive SET-jti deduplication index, expiry-ordered
by the refresh-family expiry. It retains notification identity only until the
family can no longer be renewed. It has no access-token denylist or inventory.

## zumd Transmitter

Extend zumd_revoke rather than adding a parallel service. Persist exactly the
state required to retry HTTPS push:

| Record | Fields |
|---|---|
| SSFRx | Receiver ID, protected application/audience, delivery URL, callback-authentication secret reference, revision. |
| SSFDelivery | Receiver ID, SET jti/bytes, refresh-family identity, family-expiry bound, and next delivery time. Row existence means the SET is unacknowledged. |

Use a normal Zdb saga to durably create each delivery outbox record (and release
its saga ownership) before attempting network I/O. Do not hold a Zdb saga open
while waiting for HTTPS. The receiver's successful response is the terminal
outbox transition: delete that delivery row by its composite key; an absent
response leaves it available for retry. The credential field is a Zum secret
reference; never persist clear callback credentials. SET records contain the
signed SET, never a bearer token.

For an RFC 7009 refresh-token revocation, refresh-token replay detection, or an
administrative operation that invalidates a refresh-token family:

1. Construct one signed refresh-token-revoked SET per affected refresh family.
2. Create one SSFDelivery for every configured Receiver accepting that resource
   application's audience.
3. Send RFC 8935 HTTPS push and record acknowledgement only after the standard
   response.
4. Retry unacknowledged delivery with bounded backoff until acknowledgement or
   the refresh-family expiry, then delete the record.

Refresh-token rotation is not a revocation event: replacing a spent refresh
token keeps its family active. A family is pushed only when the family itself is
revoked (for example, an explicit revoke request or detected replay). Access
tokens are self-contained, short-lived credentials and are not revoked by this
facility; their ordinary expiry bounds any residual access.

There is no delivery cursor, consumer offset, inventory endpoint, event history,
or polling fallback. Receiver recovery depends on the outstanding durable
delivery records. A resource server only introspects a token when it must decide
an otherwise unknown token's validity; it never schedules an introspection loop.

## Resource-server integration

zumpingd is the first consumer. Its HTTPS listener passes callback request data
and authentication metadata to Service::receiveSET; it does not parse a SET.
Its refresh-family callback invalidates the locally held renewal handle (when
the application keeps one). It does not close a connection or reject an
already-issued access token solely because a refresh family was revoked.

ztchub uses this identical Service contract after this precursor passes. It
adds no SSF-specific protocol, storage, HTTP parser, JSON parser, or delivery
worker.

## Framework audit contract

Apply these GUIDELINES.md requirements to this precursor:

| Area | Required implementation |
|---|---|
| Names and files | Use concise CamelCase module-prefixed C++ names and lowercase zumd program files. Keep identifiers under 28 bytes: TokenID, SSFRx, SSFDelivery, receiveSET. Apply Naming: Length and abbreviations; Casing and member prefixes. |
| Parsing and formatting | Use ZfStruct/ZfJSON/ZfURI, Zhttp, existing JWT verification, ZuPrint/ZuFmt/ZuBox, and ZiLOG. Do not hand-parse JSON, JWT segments, numbers, URLs, or HTTP, and do not use printf formatting. Apply Leveraging: Metadata and Formatting; Audit flags: Framework fit. |
| Validation | Validate an access token at Service::verify and a SET only at receiveSET. Do not reparse a SET in zumpingd/ztchub or duplicate generated/JWT checks. Apply Audit flags: Framework fit, Algorithmic inefficiency, Type and API friction. |
| Storage | Use Zdb records and sagas for durable Transmitter state. Use intrusive ZmHash/ordered queues with named ZmHeapID for in-memory Receiver state. Delete final/expired entries by their known key; never scan containers to garbage collect. Apply Persistence; Leveraging: Containers and intrusion; Audit flags: Storage and capacity, Heap allocation, Scope and cleanup. |
| Data path | Keep SET bytes in pooled ZiIOBuf or a retained named-heap buffer only while required. Move handles rather than make temporary contiguous copies; use ZtScratch for transient claim work. Do not raw-new, use STL, default-initialize before overwrite, or cast between convertible Z spans/strings. Apply Audit flags: Heap allocation, Data movement and initialization, Type and API friction. |
| Shards and lifetime | Keep Service state on its control shard; HTTP Rx/Tx state stays on its owner. Cross-shard callbacks post only refresh-family identity, expiry, owner ID, and a buffer handle. Use ZmFn compact captures, no locks/extra atomics, no shared session pointer, and no short-lived reference back to an owner. Apply I/O sharding: Function organization, Cross-Shard Handoffs, Lifetime; Audit flags: Unnecessary atomic operations, locking and copies. |
| Timers and tests | The next-expiry/retry timer is a value member; cancel, same-shard drain, then release it. Use switch for discrete SET/delivery state, cap/repost delivery batches, and use ZuTest/ZuTestUtil with continuations and ZmBlock/Semaphore—not sleeps or polling—in tests. Apply Timers and Timer Teardown; Audit flags: Algorithm and control flow, Liveness; Leveraging: Tests. |

## Acceptance gates

1. Unit tests prove TokenID extraction, ID-token and SET/token confusion
   rejection, malformed/type/signature/issuer/audience/time failures, duplicate
   acknowledgement, expiry cleanup, and refresh-family callback-once behaviour.
2. Integration tests with real zumd and HTTPS prove refresh-token revocation,
   replay-triggered family revocation, all matching Receiver delivery, durable
   retry after Receiver outage, duplicate delivery, and restart recovery.
   Rotation alone produces no SET.
3. zumpingd proves the common libZum callback receives family revocation and
   does not reject an already-issued access token merely because its refresh
   family was revoked.
4. On-demand introspection succeeds only for a presented unknown token;
   unavailable Transmitter/introspection fails closed. Test evidence shows no
   polling traffic.
5. Normal TAP, ASan/LSan, and libtool-executed valgrind checks pass.
