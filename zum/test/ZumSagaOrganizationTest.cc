//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumSagaTestSupport.hh"
#include <zlib/zumd_saga_app_action_add.hh>
#include <zlib/zumd_saga_assignment_change.hh>
#include <zlib/zumd_saga_role_delete.hh>
#include <zlib/zumd_saga_catalog_publish.hh>
#include <zlib/zumd_saga_assignment_add.hh>
#include <zlib/zumd_saga_role_edit.hh>
#include <zlib/zumd_saga_action_edit.hh>
#include <zlib/zumd_saga_app_change.hh>
#include <zlib/zumd_saga_user_edit.hh>

static void organization()
{
  ZuTestScope(organization);
  roundTrip(Zum::appActionAddDef(), Zum::appActionAdd(Zum::AppActionAddArgs{}));
  roundTrip(Zum::assignmentChangeDef(), Zum::assignmentChange(Zum::AssignmentChangeArgs{}));
  roundTrip(Zum::roleDeleteDef(), Zum::roleDelete(Zum::RoleDeleteArgs{}));
  roundTrip(Zum::catalogPublishDef(), Zum::catalogPublish(Zum::CatalogPublishArgs{}));
  roundTrip(Zum::assignmentAddDef(), Zum::assignmentAdd(Zum::AssignmentAddArgs{}));
  roundTrip(Zum::roleEditDef(), Zum::roleEdit(Zum::RoleEditArgs{}));
  roundTrip(Zum::actionEditDef(), Zum::actionEdit(Zum::ActionEditArgs{}));
  roundTrip(Zum::appChangeDef(), Zum::appChange(Zum::AppChangeArgs{}));
  roundTrip(Zum::userEditDef(), Zum::userEdit(Zum::UserEditArgs{}));

  Zum::AssignmentChangeArgs assignment;
  assignment.appID = 9;
  assignment.userID = 42;
  assignment.oldRoles.push(1);
  assignment.newRoles.push(2);
  assignment.assignRoles = true;
  assignment.ifMatch = "\"v3\"";
  ZuCheck(!assignment.unchanged());
  distinct(Zum::assignmentChangeDef(), Zum::assignmentChange(ZuMv(assignment)),
    Zum::assignmentChange(Zum::AssignmentChangeArgs{}));
  phase(Zum::assignmentChangeDef(), 4, "zum.app", ZdbSagaOp::Update);

  Zum::RoleDeleteArgs remove;
  remove.app.id = 9;
  remove.role = Zum::Role{.appID = 9, .id = 4};
  remove.members.push(Zum::Bytes{ZuBSpan{"member-image"}});
  remove.clients.push(Zum::Bytes{ZuBSpan{"client-image"}});
  remove.admins.push(Zum::Bytes{ZuBSpan{"admin-image"}});
  remove.maps.push(Zum::Bytes{ZuBSpan{"map-image"}});
  auto saga = Zum::roleDelete(ZuMv(remove));
  ZuCheck(saga->nSteps() == 13);
  distinct(Zum::roleDeleteDef(), ZuMv(saga),
    Zum::roleDelete(Zum::RoleDeleteArgs{}));
  phase(Zum::roleDeleteDef(), 3, "zum.assignment", ZdbSagaOp::Update);
  phase(Zum::roleDeleteDef(), 6, "zum.role_map", ZdbSagaOp::Delete);
  phase(Zum::roleDeleteDef(), 12, "zum.request", ZdbSagaOp::Update);

  Zum::CatalogPublishArgs catalog;
  catalog.before = Zum::App{.id = 9, .version = 2};
  catalog.after = Zum::App{.id = 9, .version = 3};
  catalog.actions.add(Zum::Action{.appID = 9, .id = 3,
    .name = "ship", .origin = Zum::Origin::Standard, .version = 1});
  catalog.roles.add(Zum::Role{.appID = 9, .id = 4,
    .name = "operator", .origin = Zum::Origin::Standard, .version = 1});
  catalog.clients.change(Zum::Client{.id = "client", .appID = 9, .version = 1},
    Zum::Client{.id = "client", .appID = 9, .version = 2});
  auto publish = Zum::catalogPublish(ZuMv(catalog));
  ZuCheck(publish->nSteps() == 10);
  distinct(Zum::catalogPublishDef(), ZuMv(publish),
    Zum::catalogPublish(Zum::CatalogPublishArgs{}));
  phase(Zum::catalogPublishDef(), 2, "zum.action", ZdbSagaOp::Insert);
  phase(Zum::catalogPublishDef(), 4, "zum.role", ZdbSagaOp::Insert);
  phase(Zum::catalogPublishDef(), 7, "zum.client", ZdbSagaOp::Update);
  phase(Zum::catalogPublishDef(), 10, "zum.action", ZdbSagaOp::Update);
  phase(Zum::catalogPublishDef(), 13, "zum.role", ZdbSagaOp::Update);
  phase(Zum::catalogPublishDef(), 15, "zum.client", ZdbSagaOp::Update);
  phase(Zum::catalogPublishDef(), 19, "zum.request", ZdbSagaOp::Update);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(organization);
  return 0;
}
