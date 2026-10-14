# Zum saga migration inventory

Baseline from the former `SagaCatalog` order and concrete definitions. The index is the former catalog ordinal; the type string, FlatBuffers table and field ordinals record the pre-migration formats. Phase entries list `ordinal:table/op`, with `*` marking a repeated phase. The current AppEnrollment owner has 12 phases after removal of two inert `client_access` phases, and its unused `coreAppID` field was removed. No persisted-data migration or backward compatibility is required.

| # | Type | Persisted ID | Codec | Source | Phases |
| ---: | --- | --- | --- | --- | ---: |
| 0 | `Enrollment` | `enrollment.v2` | `Zum.fbs.Enrollment` | `zumd_sagas.hh` | 9 |
| 1 | `CredentialAdd` | `credentialAdd.v2` | `Zum.fbs.CredentialAdd` | `zumd_sagas.hh` | 5 |
| 2 | `RecoveryStart` | `recoveryStart.v3` | `Zum.fbs.RecoveryStart` | `zumd_sagas.hh` | 6 |
| 3 | `RecoveryEnroll` | `recoveryEnroll.v2` | `Zum.fbs.RecoveryEnroll` | `zumd_sagas.hh` | 7 |
| 4 | `CodeFamily` | `codeFamily.v6` | `Zum.fbs.CodeFamily` | `zumd_sagas.hh` | 5 |
| 5 | `AppEnrollment` | `appEnrollment.v6` | `Zum.fbs.AppEnrollment` | `zumd_sagas.hh` | 14 |
| 6 | `ExternalProjection` | `externalProjection.v1` | `Zum.fbs.ExternalProjection` | `zumd_sagas.hh` | 5 |
| 7 | `AppActionAdd` | `appActionAdd.v2` | `Zum.fbs.AppActionAdd` | `zumd_sagas.hh` | 6 |
| 8 | `AssignmentChange` | `assignmentChange.v2` | `Zum.fbs.AssignmentChange` | `zumd_sagas.hh` | 6 |
| 9 | `RoleDelete` | `roleDelete.v2` | `Zum.fbs.RoleDelete` | `zumd_role_delete.hh` | 13 |
| 10 | `CatalogPublish` | `catalogPublish.v3` | `Zum.fbs.CatalogPublish` | [zumd_catalog.hh](src/zumd_catalog.hh) | 20 |
| 11 | `AssignmentAdd` | `assignmentAdd.v1` | `Zum.fbs.AssignmentAdd` | `zumd_sagas.hh` | 4 |
| 12 | `RoleEdit` | `roleEdit.v1` | `Zum.fbs.RoleEdit` | `zumd_sagas.hh` | 6 |
| 13 | `ActionEdit` | `actionEdit.v1` | `Zum.fbs.ActionEdit` | `zumd_sagas.hh` | 6 |
| 14 | `AppChange` | `appChange.v1` | `Zum.fbs.AppChange` | `zumd_sagas.hh` | 4 |
| 15 | `UserEdit` | `userEdit.v1` | `Zum.fbs.UserEdit` | `zumd_sagas.hh` | 4 |
| 16 | `CredEdit` | `credEdit.v1` | `Zum.fbs.CredEdit` | `zumd_sagas.hh` | 4 |
| 17 | `ProviderEdit` | `providerEdit.v1` | `Zum.fbs.ProviderEdit` | `zumd_sagas.hh` | 4 |
| 18 | `ClientEdit` | `clientEdit.v1` | `Zum.fbs.ClientEdit` | `zumd_sagas.hh` | 4 |
| 19 | `KeyRetire` | `keyRetire.v1` | `Zum.fbs.KeyRetire` | `zumd_sagas.hh` | 3 |
| 20 | `ClientAccessState` | `clientAccessState.v1` | `Zum.fbs.ClientAccessState` | `zumd_sagas.hh` | 4 |
| 21 | `AdminAccessState` | `adminAccessState.v1` | `Zum.fbs.AdminAccessState` | `zumd_sagas.hh` | 4 |
| 22 | `RoleMapDelete` | `roleMapDelete.v1` | `Zum.fbs.RoleMapDelete` | `zumd_sagas.hh` | 5 |
| 23 | `RoleMapPut` | `roleMapPut.v1` | `Zum.fbs.RoleMapPut` | `zumd_sagas.hh` | 7 |
| 24 | `PolicyPut` | `policyPut.v1` | `Zum.fbs.PolicyPut` | `zumd_sagas.hh` | 7 |
| 25 | `ClientAccessPut` | `clientAccessPut.v1` | `Zum.fbs.ClientAccessPut` | `zumd_sagas.hh` | 7 |
| 26 | `AdminAccessPut` | `adminAccessPut.v1` | `Zum.fbs.AdminAccessPut` | `zumd_sagas.hh` | 7 |
| 27 | `ProviderAdd` | `providerAdd.v1` | `Zum.fbs.ProviderAdd` | `zumd_sagas.hh` | 4 |
| 28 | `RoleAdd` | `roleAdd.v1` | `Zum.fbs.RoleAdd` | `zumd_sagas.hh` | 6 |
| 29 | `UserInvite` | `userInvite.v2` | `Zum.fbs.UserInvite` | `zumd_sagas.hh` | 8 |
| 30 | `ClientAdd` | `clientAdd.v1` | `Zum.fbs.ClientAdd` | `zumd_sagas.hh` | 6 |
| 31 | `KeyAdd` | `keyAdd.v1` | `Zum.fbs.KeyAdd` | `zumd_sagas.hh` | 3 |
| 32 | `Revoke` | `revoke.v2` | `Zum.fbs.Revoke` | [zumd_revoke.hh](src/zumd_revoke.hh) | 8 |
| 33 | `GrantCleanup` | `grantCleanup.v3` | `Zum.fbs.GrantCleanup` | [zumd_revoke.hh](src/zumd_revoke.hh) | 3 |
| 34 | `SessionCleanup` | `sessionCleanup.v2` | `Zum.fbs.SessionCleanup` | [zumd_revoke.hh](src/zumd_revoke.hh) | 2 |
| 35 | `RefreshCleanup` | `refreshCleanup.v2` | `Zum.fbs.RefreshCleanup` | [zumd_revoke.hh](src/zumd_revoke.hh) | 2 |
| 36 | `AppCleanup` | `appCleanup.v2` | `Zum.fbs.AppCleanup` | [zumd_revoke.hh](src/zumd_revoke.hh) | 3 |
| 37 | `ConsentCode` | `consentCode.v1` | `Zum.fbs.ConsentCode` | `zumd_consent.hh` | 5 |
| 38 | `KeyBinding` | `keyBinding.v1` | `Zum.fbs.KeyBinding` | [zumd_rekey.hh](src/zumd_rekey.hh) | 1 |
| 39 | `SecretRekey` | `secretRekey.v2` | `Zum.fbs.SecretRekey` | [zumd_rekey.hh](src/zumd_rekey.hh) | 4 |
| 40 | `SSFDeliveryAdd` | `ssfDeliveryAdd.v1` | `Zum.fbs.SSFDeliveryAdd` | [zumd_revoke.hh](src/zumd_revoke.hh) | 2 |

## Per-type migration checklist

The source references identify preparation/submission and completion callers for review; a reference alone does not prove that a file submits the operation. The four obsolete cleanup formats have been removed.

### 0. Enrollment

- Submission: live and recovery. Source references: `zumd_daemon.cc`, `zumd_daemon_admin.cc`, `zumd_passkey.cc`, `zumd_sagas.cc`, `zumd_server.cc`, `zumd.hh`, `zumd_server.hh`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:ceremonyID/Bytes`, `1:userID/UInt64`, `2:name/String`, `3:handle/Bytes`, `4:credentialID/Bytes`, `5:publicKey/Bytes`, `6:signCount/UInt32`, `7:created/Int64`, `8:backupEligible/Bool`, `9:backedUp/Bool`, `10:label/String`, `11:precreated/Bool`, `12:beforeGrant/UDT`, `13:beforeUser/UDT`.
- Phases: `0:zum.grant/Update`, `1:zum.user/Update`, `2:zum.user/Insert`, `3:zum.cred/Insert`, `4:zum.cred/Update`, `5:zum.user/Update`, `6:zum.cred/Update`, `7:zum.user/Update`, `8:zum.grant/Delete`.
- Result-related state/methods to audit: none.

### 1. CredentialAdd

- Submission: live and recovery. Source references: `zumd_passkey.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:ceremonyID/Bytes`, `1:issuer/String`, `2:userID/UInt64`, `3:userHandle/Bytes`, `4:credentialID/Bytes`, `5:publicKey/Bytes`, `6:signCount/UInt32`, `7:created/Int64`, `8:backupEligible/Bool`, `9:backedUp/Bool`, `10:label/String`, `11:userVersion/UInt64`, `12:beforeGrant/UDT`.
- Phases: `0:zum.grant/Update`, `1:zum.cred/Insert`, `2:zum.cred/Update`, `3:zum.cred/Update`, `4:zum.grant/Delete`.
- Result-related state/methods to audit: none.

### 2. RecoveryStart

- Submission: live and recovery. Source references: `zumd_passkey.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:capabilityID/Bytes`, `1:digest/Bytes`, `2:issuer/String`, `3:userID/UInt64`, `4:userVersion/UInt64`, `5:created/Int64`, `6:expires/Int64`, `7:actor/String`, `8:version/UInt64`, `9:oldState/Int8`, `10:oldUpdated/Int64`, `11:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.user/Update`, `2:zum.grant/Insert`, `3:zum.user/Update`, `4:zum.grant/Update`, `5:zum.request/Update`.
- Result-related state/methods to audit: none.

### 3. RecoveryEnroll

- Submission: live and recovery. Source references: `zumd_passkey.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:ceremonyID/Bytes`, `1:issuer/String`, `2:actor/String`, `3:userID/UInt64`, `4:userVersion/UInt64`, `5:oldHandle/Bytes`, `6:newHandle/Bytes`, `7:credentialID/Bytes`, `8:publicKey/Bytes`, `9:signCount/UInt32`, `10:created/Int64`, `11:backupEligible/Bool`, `12:backedUp/Bool`, `13:label/String`, `14:beforeGrant/UDT`, `15:beforeUser/UDT`.
- Phases: `0:zum.grant/Update`, `1:zum.cred/Insert`, `2:zum.cred/Update`, `3:zum.user/Update`, `4:zum.cred/Update`, `5:zum.user/Update`, `6:zum.grant/Delete`.
- Result-related state/methods to audit: none.

### 4. CodeFamily

- Submission: live and recovery. Source references: `zumd_sagas.cc`, `zumd_token.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`, `ZumTest.cc`.
- Payload fields (codec order): `0:codeID/Bytes`, `1:codeDigest/Bytes`, `2:familyID/Bytes`, `3:issuer/String`, `4:appID/UInt64`, `5:userID/UInt64`, `6:clientID/String`, `7:credentialID/Bytes`, `8:audience/String`, `9:requestedRoleIDs/UInt64Vec`, `10:roleIDs/UInt64Vec`, `11:actions/UDT`, `12:digest/Bytes`, `13:authVersion/UInt64`, `14:userVersion/UInt64`, `15:authTime/Int64`, `16:created/Int64`, `17:expires/Int64`, `18:scope/String`, `19:nonce/String`, `20:authorityProviderID/UInt64`, `21:policyVersion/UInt64`, `22:evidenceVersion/UInt64`, `23:authoritySource/Int8`, `24:beforeGrant/UDT`, `25:clientVersion/UInt64`, `26:assignmentVersion/UInt64`.
- Phases: `0:zum.grant/Update`, `1:zum.refresh/Insert`, `2:zum.refresh/Update`, `3:zum.refresh/Update`, `4:zum.grant/Delete`.
- Result-related state/methods to audit: none.

### 5. AppEnrollment

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumSagaTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:coreAppID/UInt64`, `1:appID/UInt64`, `2:appName/String`, `3:appLabel/String`, `4:audience/String`, `5:signKey/UDT`, `6:clientID/String`, `7:secretDigest/Bytes`, `8:created/Int64`, `9:catalogPublishOp/UInt32`, `10:operationQueryOp/UInt32`, `11:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Insert`, `2:zum.sign_key/Insert`, `3:zum.client/Insert`, `4:zum.client_access/Insert`, `5:zum.admin_access/Insert`, `6:zum.auth_policy/Insert`, `7:zum.member/Update`, `8:zum.member/Update`, `9:zum.member/Update`, `10:zum.member/Update`, `11:zum.member/Update`, `12:zum.app/Update`, `13:zum.request/Update`.
- Result-related state/methods to audit: none.

### 6. ExternalProjection

- Submission: live and recovery. Source references: `zumd_oidc.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:providerID/UInt64`, `1:issuer/String`, `2:subject/String`, `3:userID/UInt64`, `4:name/String`, `5:handle/Bytes`, `6:created/Int64`.
- Phases: `0:zum.ext_identity/Insert`, `1:zum.user/Insert`, `2:zum.user/Update`, `3:zum.user/Update`, `4:zum.ext_identity/Update`.
- Result-related state/methods to audit: none.

### 7. AppActionAdd

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:appID/UInt64`, `1:actionID/UInt32`, `2:name/String`, `3:label/String`, `4:created/Int64`, `5:oldAppVersion/UInt64`, `6:oldAuthVersion/UInt64`, `7:oldUpdated/Int64`, `8:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.action/Insert`, `3:zum.action/Update`, `4:zum.app/Update`, `5:zum.request/Update`.
- Result-related state/methods to audit: none.

### 8. AssignmentChange

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:appID/UInt64`, `1:userID/UInt64`, `2:oldRoles/UInt64Vec`, `3:newRoles/UInt64Vec`, `4:oldState/Int8`, `5:newState/Int8`, `6:version/UInt64`, `7:authVersion/UInt64`, `8:oldUpdated/Int64`, `9:updated/Int64`, `10:appVersion/UInt64`, `11:appAuthVersion/UInt64`, `12:appUpdated/Int64`, `13:request/UDT`, `14:assignRoles/Bool`, `15:ifMatch/String`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.assignment/Update`, `3:zum.assignment/Update`, `4:zum.app/Update`, `5:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 9. RoleDelete

- Submission: live and recovery. Source references: `zumd_role_delete.cc`.
- Existing test references: `zumrestarttest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:role/UDT`, `2:members/BytesVec`, `3:clients/BytesVec`, `4:admins/BytesVec`, `5:maps/BytesVec`, `6:updated/Int64`, `7:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.role/Update`, `3:zum.assignment/Update*` `[members.length()]`, `4:zum.client_access/Update*` `[clients.length()]`, `5:zum.admin_access/Update*` `[admins.length()]`, `6:zum.role_map/Delete*` `[maps.length()]`, `7:zum.assignment/Update*` `[members.length()]`, `8:zum.client_access/Update*` `[clients.length()]`, `9:zum.admin_access/Update*` `[admins.length()]`, `10:zum.role/Update`, `11:zum.app/Update`, `12:zum.request/Update`.
- Result-related state/methods to audit: none.

### 10. CatalogPublish

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:after/UDT`, `2:actions/UDT`, `3:roles/UDT`, `4:clients/UDT`, `5:clientAccess/UDT`, `6:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.action/Insert*` `[actions.added.length()]`, `3:zum.action/Update*` `[actions.changed.length()]`, `4:zum.role/Insert*` `[roles.added.length()]`, `5:zum.role/Update*` `[roles.changed.length()]`, `6:zum.client/Insert*` `[clients.added.length()]`, `7:zum.client/Update*` `[clients.changed.length()]`, `8:zum.client_access/Insert*` `[clientAccess.added.length()]`, `9:zum.client_access/Update*` `[clientAccess.changed.length()]`, `10:zum.action/Update*` `[actions.added.length()]`, `11:zum.action/Update*` `[actions.changed.length()]`, `12:zum.role/Update*` `[roles.added.length()]`, `13:zum.role/Update*` `[roles.changed.length()]`, `14:zum.client/Update*` `[clients.added.length()]`, `15:zum.client/Update*` `[clients.changed.length()]`, `16:zum.client_access/Update*` `[clientAccess.added.length()]`, `17:zum.client_access/Update*` `[clientAccess.changed.length()]`, `18:zum.app/Update`, `19:zum.request/Update`.
- Result-related state/methods to audit: none.

### 11. AssignmentAdd

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:appID/UInt64`, `1:userID/UInt64`, `2:created/Int64`, `3:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.assignment/Insert`, `2:zum.assignment/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 12. RoleEdit

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:actionIDs/UInt32Vec`, `3:ifMatch/String`, `4:updated/Int64`, `5:request/UDT`, `6:kind/Int8`, `7:label/String`, `8:state/Int8`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.role/Update`, `3:zum.role/Update`, `4:zum.app/Update`, `5:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 13. ActionEdit

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:ifMatch/String`, `3:updated/Int64`, `4:request/UDT`, `5:state/Int8`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.action/Update`, `3:zum.action/Update`, `4:zum.app/Update`, `5:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 14. AppChange

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:ifMatch/String`, `2:label/String`, `3:updated/Int64`, `4:request/UDT`, `5:stateOnly/Bool`, `6:state/Int8`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.app/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 15. UserEdit

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:ifMatch/String`, `2:profile/String`, `3:updated/Int64`, `4:request/UDT`, `5:stateOnly/Bool`, `6:state/Int8`, `7:email/String`, `8:fields/UInt8`.
- Phases: `0:zum.request/Insert`, `1:zum.user/Update`, `2:zum.user/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 16. CredEdit

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:ifMatch/String`, `2:label/String`, `3:updated/Int64`, `4:request/UDT`, `5:stateOnly/Bool`, `6:state/Int8`.
- Phases: `0:zum.request/Insert`, `1:zum.cred/Update`, `2:zum.cred/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 17. ProviderEdit

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:ifMatch/String`, `2:values/UDT`, `3:fields/UInt8`, `4:updated/Int64`, `5:request/UDT`, `6:stateOnly/Bool`, `7:state/Int8`.
- Phases: `0:zum.request/Insert`, `1:zum.provider/Update`, `2:zum.provider/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 18. ClientEdit

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:ifMatch/String`, `2:values/UDT`, `3:fields/UInt8`, `4:updated/Int64`, `5:request/UDT`, `6:stateOnly/Bool`, `7:state/Int8`, `8:overlapSeconds/UInt32`.
- Phases: `0:zum.request/Insert`, `1:zum.client/Update`, `2:zum.client/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 19. KeyRetire

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:ifMatch/String`, `2:retireAfter/Int64`, `3:updated/Int64`, `4:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.sign_key/Update`, `2:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 20. ClientAccessState

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:ifMatch/String`, `2:updated/Int64`, `3:request/UDT`, `4:state/Int8`.
- Phases: `0:zum.request/Insert`, `1:zum.client_access/Update`, `2:zum.client_access/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 21. AdminAccessState

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:ifMatch/String`, `2:updated/Int64`, `3:request/UDT`, `4:state/Int8`.
- Phases: `0:zum.request/Insert`, `1:zum.admin_access/Update`, `2:zum.admin_access/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 22. RoleMapDelete

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:ifMatch/String`, `3:updated/Int64`, `4:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.role_map/Delete`, `3:zum.app/Update`, `4:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 23. RoleMapPut

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:roleID/UInt64`, `3:ifMatch/String`, `4:ifNoneMatch/String`, `5:updated/Int64`, `6:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.role_map/Insert`, `3:zum.role_map/Update`, `4:zum.role_map/Update`, `5:zum.app/Update`, `6:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 24. PolicyPut

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:values/UDT`, `3:ifMatch/String`, `4:ifNoneMatch/String`, `5:updated/Int64`, `6:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.auth_policy/Insert`, `3:zum.auth_policy/Update`, `4:zum.auth_policy/Update`, `5:zum.app/Update`, `6:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 25. ClientAccessPut

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:values/UDT`, `3:ifMatch/String`, `4:ifNoneMatch/String`, `5:updated/Int64`, `6:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.client_access/Insert`, `3:zum.client_access/Update`, `4:zum.client_access/Update`, `5:zum.app/Update`, `6:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 26. AdminAccessPut

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:values/UDT`, `3:ifMatch/String`, `4:ifNoneMatch/String`, `5:updated/Int64`, `6:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.admin_access/Insert`, `3:zum.admin_access/Update`, `4:zum.admin_access/Update`, `5:zum.app/Update`, `6:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 27. ProviderAdd

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:values/UDT`, `2:updated/Int64`, `3:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.provider/Insert`, `2:zum.provider/Update`, `3:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 28. RoleAdd

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:values/UDT`, `3:updated/Int64`, `4:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.role/Insert`, `3:zum.role/Update`, `4:zum.app/Update`, `5:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 29. UserInvite

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:values/UDT`, `2:grant/UDT`, `3:updated/Int64`, `4:request/UDT`, `5:external/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.user/Insert`, `2:zum.grant/Insert`, `3:zum.user/Update`, `4:zum.user/Update`, `5:zum.user/Update`, `6:zum.grant/Update`, `7:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 30. ClientAdd

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:app/UDT`, `1:before/UDT`, `2:values/UDT`, `3:updated/Int64`, `4:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.app/Update`, `2:zum.client/Insert`, `3:zum.client/Update`, `4:zum.app/Update`, `5:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 31. KeyAdd

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`, `zumd_sagas.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:before/UDT`, `1:values/UDT`, `2:updated/Int64`, `3:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.sign_key/Insert`, `2:zum.request/Update`.
- Result-related state/methods to audit: `error`, `result`.

### 32. Revoke

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:sessions/BytesVec`, `1:consents/BytesVec`, `2:grants/BytesVec`, `3:updated/Int64`, `4:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.session/Update*` `[sessions.length()]`, `2:zum.consent/Update*` `[consents.length()]`, `3:zum.grant/Update*` `[grants.length()]`, `4:zum.session/Update*` `[sessions.length()]`, `5:zum.consent/Update*` `[consents.length()]`, `6:zum.grant/Update*` `[grants.length()]`, `7:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 33. GrantCleanup

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:grants/UDT`, `1:updated/Int64`, `2:request/UDT`.
- Phases: `0:zum.request/Insert`, `1:zum.grant/Delete*` `[grants.length()]`, `2:zum.request/Update`.
- Result-related state/methods to audit: `error`.

### 34. SessionCleanup

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:sessions/UDT`, `1:updated/Int64`.
- Phases: `0:zum.session/Delete*` `[sessions.length()]`, `1:zum.session/Update`.
- Result-related state/methods to audit: `error`.

### 35. RefreshCleanup

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:refreshes/UDT`, `1:updated/Int64`.
- Phases: `0:zum.refresh/Delete*` `[refreshes.length()]`, `1:zum.refresh/Update`.
- Result-related state/methods to audit: `error`.

### 36. AppCleanup

- Submission: live and recovery. Source references: `zumd_daemon_admin.cc`.
- Existing test references: `ZumSagaTest.cc`.
- Payload fields (codec order): `0:appID/UInt64`, `1:evidence/UDT`, `2:consents/UDT`, `3:updated/Int64`.
- Phases: `0:zum.evidence/Delete*` `[evidence.length()]`, `1:zum.consent/Delete*` `[consents.length()]`, `2:zum.app/Update`.
- Result-related state/methods to audit: `error`.

### 37. ConsentCode

- Submission: live and recovery. Source references: `zumd_authorize.cc`.
- Existing test references: `ZumRuntimeTest.cc`, `ZumSagaTest.cc`.
- Payload fields (codec order): `0:beforeGrant/UDT`, `1:afterGrant/UDT`, `2:beforeConsent/UDT`, `3:roleIDs/UInt64Vec`, `4:now/Int64`.
- Phases: `0:zum.grant/Update`, `1:zum.consent/Insert`, `2:zum.consent/Update`, `3:zum.grant/Update`, `4:zum.consent/Update`.
- Result-related state/methods to audit: `result`.

### 38. KeyBinding

- Submission: live and recovery. Source references: `zumd_rekey.cc`.
- Existing test references: `ZumSagaTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:issuer/String`, `1:beforeCheck/Bytes`, `2:afterCheck/Bytes`, `3:beforePending/Bytes`, `4:afterPending/Bytes`.
- Phases: `0:zum.issuer/Update`.
- Result-related state/methods to audit: none.

### 39. SecretRekey

- Submission: live and recovery. Source references: `zumd_rekey.cc`.
- Existing test references: `ZumSagaTest.cc`, `zumrestarttest.cc`.
- Payload fields (codec order): `0:field/UInt32`, `1:providerID/UInt64`, `2:appID/UInt64`, `3:userID/UInt64`, `4:keyID/String`, `5:before/Bytes`, `6:after/Bytes`.
- Phases: `0:zum.provider/Update`, `1:zum.evidence/Update`, `3:zum.ssf_rx/Update`, `2:zum.sign_key/Update`.
- Result-related state/methods to audit: none.

### 40. SSFDeliveryAdd

- Submission: live and recovery. Source references: `zumd_revoke.cc`.
- Existing test references: `ZumRuntimeTest.cc`.
- Payload fields (codec order): `0:delivery/UDT`.
- Phases: `0:zum.ssf_delivery/Insert`, `1:zum.ssf_delivery/Update`.
- Result-related state/methods to audit: none.
## Existing test entry points to preserve

`ZumTest.cc` currently has 21 top-level `ZuTestCall` cases:
`loginNames`, `managementCatalog`, `jsonContract`, `requests`,
`oauthForms`, `oidcRoles`, `httpRoutes`, `limits`, `redirects`, `pkce`,
`opaque`, `actions`, `records`, `appRecords`, `discovery`, `refresh`,
`jwt`, `ssfSET`, `webAuthn`, `webAuthnOptions`, and `webAuthnInput`.
The requested eight test binaries must preserve the assertions within these
cases, including any helper assertions they call.

`ZumSagaTest.cc` has the single `enrollmentSaga` driver covering multiple
persisted formats. `ZumRuntimeTest.cc` has the single `enrollmentRuntime`
driver covering the runtime scenarios. Both need their contained cases split
into the seven requested functional groups. `zumrestarttest.cc` calls
`restart`, `grantUpdate`, `sagaRecovery`, `memberRecovery`,
`invitationRecovery`, `roleCreationRecovery`, `rekeyRecovery`,
`roleRemoval` in three initial variants, `roleBoundaries`, `catalogRecovery`
in three initial variants, and `catalogBoundaries`; its helpers enumerate
additional crash cuts for catalog and role deletion.

The `ZumTest.cc` case list is the assertion-preservation baseline; the two
large saga drivers require finer case-level inspection during extraction.
