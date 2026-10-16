`zum.evidence`?
`zum.consent`?


zumd currently registers 22 persisted Zum application tables (schema 20). Saga payload types and
Zdb infrastructure tables are excluded.

 Table                Description
━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 zum.issuer           Singleton issuer/bootstrap metadata, schema version, and key-rotation
                      state.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.app              Protected applications/resources, including audience, owner, state, and
                      catalog revision.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.user             Local user/principal identities and profile attributes.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.cred             User credentials, primarily WebAuthn/passkey public keys and counters.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.membership       User membership in an application and assigned role IDs.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.action           Application-scoped actions/permissions.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.role             Application-scoped roles and their action bitmaps.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.client           OAuth clients, credentials, redirect URIs, grant settings, and identity
                      scopes.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.client_access    Roles granted to a client within an application.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.admin_access     Administrative delegation: permitted operations and roles for an actor/
                      application.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.provider         External OIDC identity-provider configuration and encrypted client
                      credentials.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.auth_policy      Application/provider authentication, eligibility, session, token, and
                      consent policy.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.ext_identity     Mapping from an external provider subject to a local user.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.role_map         Mapping of provider-supplied role values to local application roles.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.evidence         Cached external eligibility/role evidence used during authorization.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.session          Federated/local authentication sessions and their expiry state.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.consent          User consent and approved roles for a client/application pair.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.grant            OAuth authorization transactions, codes, refresh families, and token-
                      related state.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.sign_key         Issuer signing keys, JWKs, lifecycle state, and protected private
                      material.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.request          Idempotency records linking administrative requests to sagas and results.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.ssf_rx           Security Event Token receiver configuration for an application.
───────────────────  ───────────────────────────────────────────────────────────────────────────
 zum.ssf_delivery     Pending/completed SSF revocation-event deliveries and retry state.

