//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_db.hh>
#include <zlib/zumd_saga_enrollment.hh>
#include <zlib/zumd_saga_credential_add.hh>
#include <zlib/zumd_saga_recovery_start.hh>
#include <zlib/zumd_saga_recovery_enroll.hh>
#include <zlib/zumd_saga_code_family.hh>
#include <zlib/zumd_saga_app_enrollment.hh>
#include <zlib/zumd_saga_external_projection.hh>
#include <zlib/zumd_saga_app_action_add.hh>
#include <zlib/zumd_saga_assignment_change.hh>
#include <zlib/zumd_saga_role_delete.hh>
#include <zlib/zumd_saga_catalog_publish.hh>
#include <zlib/zumd_saga_assignment_add.hh>
#include <zlib/zumd_saga_role_edit.hh>
#include <zlib/zumd_saga_action_edit.hh>
#include <zlib/zumd_saga_app_change.hh>
#include <zlib/zumd_saga_user_edit.hh>
#include <zlib/zumd_saga_cred_edit.hh>
#include <zlib/zumd_saga_provider_edit.hh>
#include <zlib/zumd_saga_client_edit.hh>
#include <zlib/zumd_saga_key_retire.hh>
#include <zlib/zumd_saga_client_access_state.hh>
#include <zlib/zumd_saga_admin_access_state.hh>
#include <zlib/zumd_saga_role_map_delete.hh>
#include <zlib/zumd_saga_role_map_put.hh>
#include <zlib/zumd_saga_policy_put.hh>
#include <zlib/zumd_saga_client_access_put.hh>
#include <zlib/zumd_saga_admin_access_put.hh>
#include <zlib/zumd_saga_provider_add.hh>
#include <zlib/zumd_saga_role_add.hh>
#include <zlib/zumd_saga_user_invite.hh>
#include <zlib/zumd_saga_client_add.hh>
#include <zlib/zumd_saga_key_add.hh>
#include <zlib/zumd_saga_revoke.hh>
#include <zlib/zumd_saga_grant_cleanup.hh>
#include <zlib/zumd_saga_session_cleanup.hh>
#include <zlib/zumd_saga_refresh_cleanup.hh>
#include <zlib/zumd_saga_app_cleanup.hh>
#include <zlib/zumd_saga_consent_code.hh>
#include <zlib/zumd_saga_key_binding.hh>
#include <zlib/zumd_saga_secret_rekey.hh>
#include <zlib/zumd_saga_ssf_delivery_add.hh>

namespace Zum {

Zdb_::SagaDefs sagaDefs()
{
  return {
    enrollmentDef(), credentialAddDef(), recoveryStartDef(), recoveryEnrollDef(),
    codeFamilyDef(), appEnrollmentDef(), externalProjectionDef(), appActionAddDef(),
    assignmentChangeDef(), roleDeleteDef(), catalogPublishDef(), assignmentAddDef(),
    roleEditDef(), actionEditDef(), appChangeDef(), userEditDef(), credEditDef(),
    providerEditDef(), clientEditDef(), keyRetireDef(), clientAccessStateDef(),
    adminAccessStateDef(), roleMapDeleteDef(), roleMapPutDef(), policyPutDef(),
    clientAccessPutDef(), adminAccessPutDef(), providerAddDef(), roleAddDef(),
    userInviteDef(), clientAddDef(), keyAddDef(), revokeDef(), grantCleanupDef(),
    sessionCleanupDef(), refreshCleanupDef(), appCleanupDef(), consentCodeDef(),
    keyBindingDef(), secretRekeyDef(), ssfDeliveryAddDef()
  };
}

void sagaInit(DB *db)
{
  db->sagas(sagaDefs());
}

} // namespace Zum
