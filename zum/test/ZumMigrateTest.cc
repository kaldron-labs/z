//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZumMigrate.hh>

using namespace ZuTestUtil;

static Zum::Migrate::Plan plan()
{
  using namespace Zum;
  using namespace Zum::Migrate;
  Plan p{.issuer = "https://iam.example", .coreAppID = 900,
    .initialUserID = 901, .initialClientID = "zum-admin", .time = 50};
  p.apps.push(AppMap{.id = 10, .name = "alpha", .label = "Alpha"});
  p.apps.push(AppMap{.id = 20, .name = "beta", .label = "Beta"});
  p.actions.push(ActionMap{.oldID = 1, .appID = 10, .id = 5});
  p.actions.push(ActionMap{.oldID = 2, .appID = 20, .id = 5});
  p.roles.push(Migrate::RoleMap{.oldID = 100, .appID = 10, .id = 7});
  p.roles.push(Migrate::RoleMap{.oldID = 200, .appID = 20, .id = 7});
  p.audiences.push(AudienceMap{
    .uri = "https://alpha.example", .appID = 10, .id = 11,
    .name = "alpha"});
  p.audiences.push(AudienceMap{
    .uri = "https://beta.example", .appID = 20, .id = 11,
    .name = "beta"});
  p.scopes.push(ScopeMap{
    .oldID = 1000, .appID = 10, .id = 13, .audienceID = 11});
  p.scopes.push(ScopeMap{
    .oldID = 2000, .appID = 20, .id = 13, .audienceID = 11});
  p.clients.push(ClientMap{
    .oldID = "old-client", .appID = 10, .id = "new-client",
    .label = "Migrated client",
    .authMethod = ClientAuthMethod::ClientSecretBasic,
    .identityScopes = {"openid", "profile"}});
  return p;
}

static void mapping()
{
  ZuTestScope(mapping);
  using namespace Zum;
  using namespace Zum::Migrate;
  String error;
  auto p = plan();
  Mapper mapper;
  ZuCheck(mapper.init(p, error));
  ZuCheck(!error);

  App app;
  ZuCheck(mapper.app(p.apps[0], 50, app));
  ZuCheck(app.id == 10 && app.nextActionID == 6 &&
    app.name == "alpha" && app.state == State::Active);

  Legacy::Action oldAction{.id = 1, .name = "read", .state = State::Disabled};
  Action action;
  ZuCheck(mapper.action(oldAction, 50, action, error));
  ZuCheck(action.appID == 10 && action.id == 5 && action.name == "read" &&
    action.origin == Origin::Custom && action.state == State::Disabled);
  oldAction.id = 3;
  ZuCheck(!mapper.action(oldAction, 50, action, error));

  Legacy::Role oldRole{.id = 100, .name = "reader", .actions = ZtBitmap{3}};
  oldRole.actions.set(1);
  Role role;
  ZuCheck(mapper.role(oldRole, 50, role, error));
  ZuCheck(role.appID == 10 && role.id == 7 && role.actions.get(5));
  oldRole.actions.set(2);
  ZuCheck(!mapper.role(oldRole, 50, role, error));

  Legacy::Scope oldScope{
    .id = 1000, .audience = "https://alpha.example", .name = "read",
    .roleIDs = {100}};
  Scope scope;
  ZuCheck(mapper.scope(oldScope, 50, scope, error));
  ZuCheck(scope.appID == 10 && scope.id == 13 && scope.audienceID == 11 &&
    scope.roleIDs == IDVec{7} && !scope.catalogRoleIDs);
  oldScope.roleIDs.push(200);
  ZuCheck(!mapper.scope(oldScope, 50, scope, error));

  Legacy::User oldUser{
    .id = 42, .name = "user", .handle = Bytes{ZuBSpan{"handle"}},
    .roleIDs = {100, 200}, .created = 1, .updated = 2,
    .state = State::Active, .authVersion = 8};
  User user;
  Memberships memberships;
  ZuCheck(mapper.user(oldUser, user, memberships, error));
  ZuCheck(user.source == UserSource::Local &&
    user.version == 1 && memberships.length() == 2);
  ZuCheck(memberships[0].appID == 10 && memberships[0].roleIDs == IDVec{7});
  ZuCheck(memberships[1].appID == 20 && memberships[1].roleIDs == IDVec{7});
  oldUser.oidcSub = "subject";
  ZuCheck(!mapper.user(oldUser, user, memberships, error));

  Legacy::Client oldClient{
    .id = "old-client", .secretDigest = Bytes{ZuBSpan{"digest"}},
    .redirects = {"http://localhost/callback"},
    .audiences = {"https://alpha.example", "https://beta.example"},
    .scopeIDs = {1000, 2000}, .roleIDs = {100, 200},
    .created = 3, .updated = 4, .type = ClientType::Confidential,
    .grants = uint8_t(ClientGrant::AuthorizationCode |
      ClientGrant::RefreshToken), .state = State::Active};
  Client client;
  Accesses accesses;
  ZuCheck(mapper.client(oldClient, client, accesses, error));
  ZuCheck(client.id == "new-client" && client.appID == 10 &&
    client.authMethod == ClientAuthMethod::ClientSecretBasic &&
    client.refreshAllowed && accesses.length() == 2);
  ZuCheck(accesses[0].audienceIDs == IDVec{11} &&
    accesses[0].scopeIDs == IDVec{13} && accesses[0].roleIDs == IDVec{7});
  ZuCheck(accesses[1].audienceIDs == IDVec{11} &&
    accesses[1].scopeIDs == IDVec{13} && accesses[1].roleIDs == IDVec{7});
}

static void rejection()
{
  ZuTestScope(rejection);
  using namespace Zum::Migrate;
  Zum::String error;
  {
    auto p = plan();
    p.actions.push(p.actions[0]);
    Mapper mapper;
    ZuCheck(!mapper.init(p, error));
  }
  {
    auto p = plan();
    p.scopes[0].catalogRoleIDs = {7};
    Mapper mapper;
    ZuCheck(mapper.init(p, error));
    p.scopes[0].catalogRoleIDs = {8};
    ZuCheck(!mapper.init(p, error));
  }
  {
    auto p = plan();
    p.audiences[0].appID = 20;
    Mapper mapper;
    ZuCheck(!mapper.init(p, error));
  }
  {
    auto p = plan();
    p.discardExternalUsers = {42};
    Mapper mapper;
    ZuCheck(mapper.init(p, error));
    ZuCheck(mapper.discardUser(42));
    ZuCheck(!mapper.discardUser(43));
    p.discardExternalUsers.push(42);
    ZuCheck(!mapper.init(p, error));
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(mapping);
  ZuTestCall(rejection);
  return 0;
}
