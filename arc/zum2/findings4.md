# `zumd` OIDC offline-access audit

## Finding

### High: refresh-token issuance is not reconciled with OIDC `offline_access`

The current `zumd` authorization request profile does not recognize
`offline_access` as a special OIDC scope.  `AuthorizeFields` accepts ordinary
`scope` input, but there is no `offline_access` handling in
`zum/src/zumd_oauth.cc:34-55`; discovery advertises `openid`, `profile`,
`email`, and catalog scopes, but does not add or describe `offline_access` at
`zum/src/zumd_discovery.cc:296-320`.  The authorization validator accepts only
`prompt=none` and `prompt=login` at `zum/src/zumd_oauth.cc:190-220`, so
`prompt=consent` is currently rejected.

At the same time, a successful authorization-code exchange unconditionally
prepares a refresh-token family through `codeFamilyPrepare()` at
`zum/src/zumd_token.cc:548-566` and `zum/src/zumd_sagas.cc:1092-1129`, subject
to the client grant configuration.  The persisted `Client::refreshAllowed`
field exists at `zum/src/zumd.hh:392-430`, but the code-family creation path
does not use it to implement the OIDC `offline_access` decision.

OIDC Core defines `offline_access` as the request for a refresh token that can
be used when the End-User is absent.  The authorization server must obtain
consent for that offline access, normally by requiring `prompt=consent`, and
must not silently treat every authorization-code exchange as an offline-access
grant.  See [OpenID Connect Core 1.0, section 11](https://openid.net/specs/openid-connect-core-1_0-18.html).

This creates an interoperability and policy ambiguity: clients cannot discover
or request standard offline access reliably, while eligible authorization-code
clients may receive refresh tokens without the corresponding OIDC request and
consent semantics.  It also makes `zumd` diverge from Okta's documented model,
where refresh-token issuance is tied to the configured refresh-token grant and
the `offline_access` scope for authorization-code flows.  See [Okta refresh
tokens](https://developer.okta.com/docs/guides/refresh-tokens/main/).

## Recommendation

Choose and implement one explicit policy:

1. Treat `offline_access` as a reserved OIDC scope.  Advertise it only when
   the application/client is permitted to issue refresh tokens.
2. Parse and preserve it through the authorization ceremony, code grant, and
   consent decision.
3. Accept `prompt=consent` and require explicit consent for offline access,
   except where a documented existing-consent/policy rule permits reuse.
4. Create the refresh family only when offline access was requested and
   approved, or explicitly document a non-OIDC OAuth profile in which refresh
   tokens are always issued.
5. Enforce the decision using the persisted client policy (`refreshAllowed` or
   its replacement), and add tests for: no `offline_access`, approved
   `offline_access`, denied consent, `prompt=consent`, and refresh-token reuse.

The preferred direction is to align with OIDC: preserve the distinction between
an authorization-code grant and an offline-access grant, and make refresh-token
issuance a visible, policy-controlled result of consent.
