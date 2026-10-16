# Zum catalog ownership and bootstrap fix

## Problem

The current `catalogClient` enrollment mode conflates three different
concerns:

- an application owns its action and role catalog;
- a service may publish that catalog at startup; and
- an OAuth client may request resource scopes from the catalog.

`appEnroll` currently uses `integration: catalogClient` to create a protected
`App`, a confidential `Client` attached to the core application, a
`ClientAccess` row granting `CatalogPublisher`, and the corresponding
administrative delegation. This makes the service credential look like a
special cross-application client rather than the app's ordinary default
client. It also obscures the fact that no end-user client such as `zumping` is
needed before the service starts.

The target behavior is that the service represented by an `App` publishes its
initial catalog before any user-facing OAuth clients connect. The publication
may also register the application's recognized OAuth clients and grant those
clients the roles they are intended to request. All applications have this
capability. Administrators may subsequently inspect, replace, suspend, or
otherwise mutate the same app-scoped actions, roles, clients, and access rows.
`zumping` remains an end-user OAuth client and is provisioned by `zumpingd` as
part of the application's manifest; it does not independently populate the
catalog.

## Target model

```text
App
  owns actions and roles
  has a default same-name service client with secretDigest

Service deployment (for example zumpingd)
  authenticates as its App
  publishes actions, roles, and recognized application clients at startup

Admin principal
  may manage any permitted App catalog

OAuth Client (for example zumping)
  authenticates as software acting for a user or service principal
  requests scopes derived from the App's published roles

ClientAccess
  grants each recognized client selected application roles
```

Catalog publication must be authorized by `zumd`, using the app's default
same-name confidential client and its `secretDigest`. That client is created
with the `App` during enrollment and is app-scoped; it is not the end-user
client being published by the service manifest. Do not use the core app's
`client_access` row as the bootstrap permission.

The catalog publication payload should be a declarative, idempotent app
manifest:

```text
actions
roles -> action IDs
clients -> client IDs, type, redirect/grant policy
client access -> client IDs to role IDs
```

For the example, `zumpingd` publishes `ping`, the role containing `ping`, the
native `zumping` client, and that client's access to the `ping` role. The
administrator still controls whether the app may publish and may override or
remove any resulting records. The app publisher must be restricted to its own
application and must not be able to create users, memberships, clients for
other applications, or administrative delegations.

## Implementation plan

### 1. Define the app bootstrap credential and authority

- Make the app's default same-name confidential client the persisted bootstrap
  credential. Store its salted `secretDigest` on the `Client`, and return the
  plaintext secret only through the existing protected secret output path.
- Define the exact authentication contract for the catalog publication
  endpoint: app identity, target app ID, issuer, replay protection, and
  authorization failure behavior.
- Make the catalog-publisher permission implicit for every valid application,
  scoped to that application's own catalog. Preserve administrator
  authorization as a separate path.
- Ensure a service cannot publish to another application's catalog merely by
  possessing its own app credential.

### 2. Simplify application enrollment

- Remove `integration: catalogClient` as a domain discriminator from the
  public enrollment model.
- Make ordinary `appEnroll` create the application and its default client
  together. The default client ID is the app name and its `secretDigest` is
  app-scoped.
- Remove the generated `pub_` client and core `ClientAccess` effects from the
  `AppEnrollment` saga. Keep the app-scoped default client and its
  app-scoped `AdminAccess` catalog permission, including compensation and
  restart recovery.
- Decide whether an optional ordinary OAuth client should still be created as
  a convenience for other integrations; if retained, it must not be required
  for catalog publication and must not define the application's catalog
  ownership.
- Update the enrollment result and secret handling so `zumpingd` receives the
  default client's secret. The public `zumping` client ID and policy are
  supplied by the app manifest; no client secret is required for that public
  native client.

### 3. Fix catalog publication authorization

- Update `catalogPublish` admission to accept the app-scoped default-client
  principal for the target app.
- Retain the administrator path and its existing delegation checks.
- Enforce that publication is app-local and that all action/role/client/access
  identities are validated against the target application's catalog.
- Make client registration and `ClientAccess` updates part of the same
  manifest transaction as catalog publication, with stable client IDs and
  idempotent replay.
- Preserve stable action and role IDs on idempotent publication and on edits
  where the service's code depends on those IDs. Reject or explicitly handle
  attempts to change an action's semantic identity.
- Keep catalog publication separate from `ClientAccess`: client access
  controls which already-published resource roles an OAuth client may request.

### 4. Update `zumpingd` and `zumping`

- Change `zumpingd` configuration and startup to use the app-owned bootstrap
  credential and publish its `ping` action, `ping` role, native `zumping`
  client, and `ClientAccess` grant before opening its resource listener.
- Ensure startup succeeds when no `zumping` client exists before publication;
  the manifest creates or reconciles that client during startup.
- Have the publication result provide the recognized `zumping` client ID and
  configuration inputs; ensure `zumping` contains only that client ID and the
  application issuer/resource audience.
- Keep `zumping` entirely read/authorize-only with respect to the catalog: it
  requests `ping`, completes user login, and calls `/ping`.
- Document that user membership and `ClientAccess` are independent: the user
  needs membership in the role, and the OAuth client needs access to the role.

### 5. Update management API and documentation

- Remove or deprecate the `integration: catalogClient` request shape and
  document the replacement enrollment flow.
- Document that every application may publish its own initial catalog and that
  administrators may mutate any authorized application's catalog afterward.
- Clarify that `zum.app` is the catalog namespace and protected resource,
  `zum.client` is credentialed software, and `zum.client_access` is the
  client-to-application role grant.
- Update `README.md`, `MANAGEMENT.md`, the example README/configuration, and
  management operation descriptions to remove the implication that catalog
  publication requires a special client type. Document that an app manifest
  may register its recognized clients and their role grants alongside the
  action/role catalog.
- Make the audience and scope documentation continue to state that audience
  belongs to `App` and runtime resource scopes are derived from app roles.

### 6. Update schema, generated data, and cleanup behavior

- Increment the schema version if the app credential changes the physical
  schema, and update the generated FlatBuffers schema/header inputs.
- Remove obsolete catalog-client-only fields from saga payloads and any
  derived metadata after the new recovery format is defined.
- Update role-deletion/reference cleanup so it no longer treats the synthetic
  catalog client as a required application reference.
- Verify that deleting, disabling, or suspending an app also invalidates its
  app credential and blocks catalog publication.
- Follow the repository's current policy for unsupported older non-empty
  stores; do not add a compatibility shim unless explicitly required.

### 7. Tests

Add or revise tests for:

- fresh app enrollment with no end-user client;
- app-authenticated publication of actions, roles, and `zumping` client access
  before `zumping` connects;
- all applications having the catalog-publisher capability;
- administrator catalog publication and mutation;
- rejection of an app credential publishing to another app;
- stable action/role IDs and idempotent repeated publication;
- catalog publication after app suspension/disablement;
- manifest-created `zumping` client and `ClientAccess` role grant;
- denial when the client has no `ClientAccess` role, even if the catalog exists;
- user membership and client access remaining separate requirements;
- restart/recovery at every new credential and catalog saga boundary;
- secret redaction, rotation, stale credential rejection, and replay handling.

The end-to-end example should assert this order:

```text
admin enrolls App
zumpingd starts and publishes ping action/role, zumping Client, and ClientAccess
admin enrolls user and assigns ping membership
zumping logs in and calls /ping
```

## Review criteria

- No end-user OAuth client is required for application startup or initial
  catalog publication.
- No application category or integration flag determines the ownership of its
  catalog; every enrolled app has the same default-client capability.
- `App` owns the catalog; administrators and the app service operate on the
  same action/role records.
- `Client` and `ClientAccess` remain focused on OAuth caller identity and
  caller role grants, even when the app service populates them.
- The app manifest cannot grant access outside its own application or create
  user memberships.
- The service cannot self-grant users, memberships, arbitrary clients, or
  roles beyond its own catalog.
- Catalog publication is app-scoped, authenticated, replay-safe, idempotent,
  and recoverable through Zdb sagas.
