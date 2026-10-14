//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/zumd_scope.hh>

using namespace ZuTestUtil;

static void scopeMatchesApp()
{
  ZuTestScope(scopeMatchesApp);
  Zum::Client client{.id = "client", .appID = 1};
  Zum::ClientAccess access{.clientID = "client", .appID = 1, .roleIDs = {7}};
  Zum::ScopeSelect select;
  select.init(client, true, "operator operator");
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.name() == "operator");
  Zum::Role role{.appID = 2, .id = 7, .name = "operator"};
  ZuCheck(select.role(role) == Zum::ScopeError::Unavailable);
  role.appID = 1;
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup);
  ZuCheck(select.role(role) == Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  auto result = select.take();
  ZuCheck(result.scope == "operator" && result.roleIDs == Zum::IDVec{7});
  ZuCheck(select.selected(Zum::RoleID{7}));

  role.state = Zum::State::Disabled;
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup);
  ZuCheck(select.role(role) == Zum::ScopeError::Unavailable);
  role.state = Zum::State::Active;
  role.tombstone = true;
  ZuCheck(select.role(role) == Zum::ScopeError::Unavailable);
  role.tombstone = false;
  role.owner = 1;
  ZuCheck(select.role(role) == Zum::ScopeError::Unavailable);
  role.owner = 0;
  role.id = 8;
  ZuCheck(select.role(role) == Zum::ScopeError::Unavailable);
  role.id = 7;
  access.clientID = "other";
  select.begin(access, false);
  ZuCheck(select.role(role) == Zum::ScopeError::Unavailable);
}

static void roleNamedScopes()
{
  ZuTestScope(roleNamedScopes);
  Zum::Client client{.id = "client", .appID = 1, .identityScopes = {"openid", "profile"}};
  Zum::ClientAccess access{.clientID = "client", .appID = 1, .roleIDs = {7, 8}};
  Zum::ScopeSelect select;
  select.init(client, true, "  openid operator  auditor operator profile  ");
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.name() == "operator");
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 7, .name = "operator"}) ==
    Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.name() == "auditor");
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 8, .name = "auditor"}) ==
    Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  auto result = select.take();
  ZuCheck((result.identity && result.scope == "openid operator auditor profile" &&
    result.roleIDs == Zum::IDVec{7, 8}));
  // A failed candidate must not leak selection membership into the next one.
  access.roleIDs = {8};
  select.begin(access, false);
  ZuCheck(!select.selected(Zum::RoleID{7}));
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup);
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 7, .name = "operator"}) ==
    Zum::ScopeError::Unavailable);
  access.roleIDs = {7, 8};
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.name() == "operator");
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 7, .name = "operator"}) ==
    Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.name() == "auditor");
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 8, .name = "auditor"}) ==
    Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  result = select.take();
  ZuCheck((result.identity && result.scope == "openid operator auditor profile" &&
    result.roleIDs == Zum::IDVec{7, 8}));

  select.init(client, true, "openid profile openid");
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  result = select.take();
  ZuCheck(result.identity && !result.roleIDs && result.scope == "openid profile");
  select.init(client, true, "   ");
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeError::Malformed);
  select.init(client, true, {});
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeError::Malformed);
}

static void grantedScopes()
{
  ZuTestScope(grantedScopes);
  Zum::Client client{.id = "client", .appID = 1, .identityScopes = {"openid", "profile"}};
  Zum::ClientAccess access{.clientID = "client", .appID = 1, .roleIDs = {7, 8}};
  Zum::IDVec ids{7, 7};
  Zum::String granted{"openid operator"};
  Zum::ScopeSelect select;
  select.init(client, true, "openid operator", &ids, &granted);
  select.begin(access, true);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.name() == "operator");
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 7, .name = "operator"}) ==
    Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  auto result = select.take();
  ZuCheck(result.identity && result.scope == granted && result.roleIDs == Zum::IDVec{7});
  // Candidate changes clear access/selection while retaining the grant.
  access.roleIDs = {8};
  select.begin(access, true);
  ZuCheck(!select.selected(7));
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup);
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 7, .name = "operator"}) ==
    Zum::ScopeError::Unavailable);
  access.roleIDs = {7, 8};
  select.begin(access, true);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup);
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 7, .name = "operator"}) ==
    Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  result = select.take();
  ZuCheck(result.scope == granted && result.roleIDs == Zum::IDVec{7});
  Zum::String repeated{"  operator openid operator  openid "};
  select.init(client, false, {}, &ids, &repeated);
  select.begin(access, true);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.name() == "operator");
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 7, .name = "operator"}) ==
    Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  result = select.take();
  ZuCheck(result.identity && result.scope == "operator openid" &&
    result.roleIDs == Zum::IDVec{7});
  // An explicitly empty request must not fall back to the grant text.
  select.init(client, true, {}, &ids, &granted);
  select.begin(access, true);
  ZuCheck(select.step() == Zum::ScopeError::Malformed);
  select.init(client, true, "   ", &ids, &granted);
  select.begin(access, true);
  ZuCheck(select.step() == Zum::ScopeError::Malformed);
  select.init(client, true, "profile", &ids, &granted);
  select.begin(access, true);
  ZuCheck(select.step() == Zum::ScopeError::Unavailable);
  select.init(client, true, "auditor", &ids);
  select.begin(access, true);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup);
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 8, .name = "auditor"}) ==
    Zum::ScopeError::Unavailable);
  select.init(client, true, "openid", &ids);
  select.begin(access, true);
  ZuCheck(select.step() == Zum::ScopeError::Unavailable);

  select.begin(access, true, &ids);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.byID() && select.id() == 7);
  ZuCheck(select.role(Zum::Role{.appID = 1, .id = 7, .name = "operator"}) ==
    Zum::ScopeError::OK);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  result = select.take();
  ZuCheck(!result.identity && result.scope == "operator" && result.roleIDs == Zum::IDVec{7});
  ids.null();
  select.init(client, false, {}, &ids);
  select.begin(access, true, &ids);
  ZuCheck(select.step() == Zum::ScopeError::Unavailable);
}

static void offlineAccess()
{
  ZuTestScope(offlineAccess);
  Zum::Client client{.id = "client", .appID = 1,
    .grants = Zum::ClientGrant::Refresh(), .refreshAllowed = true,
    .state = Zum::State::Active};
  Zum::ClientAccess access;
  Zum::ScopeSelect select;
  select.init(client, true, "offline_access offline_access");
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeError::OK);
  auto result = select.take();
  ZuCheck(result.identity && result.scope == "offline_access" && !result.roleIDs);
  client.refreshAllowed = false;
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup);
  client.refreshAllowed = true;
  client.owner = 1;
  select.begin(access, false);
  ZuCheck(select.step() == Zum::ScopeSelect::Lookup);
}

static void outputGrowth()
{
  ZuTestScope(outputGrowth);
  Zum::Client client{.id = "client", .appID = 1};
  Zum::ClientAccess access{.clientID = "client", .appID = 1};
  Zum::IDVec ids;
  Zum::String expected;
  // Enough distinct names to exercise output growth and tree balancing.
  enum { Names = 128 };
  for (unsigned i = 0; i < Names; ++i) {
    ids.push(i + 1);
    if (expected) expected << ' ';
    expected << "scope." << i;
  }
  ids.push(1);
  access.roleIDs = ids;
  Zum::ScopeSelect select;
  select.init(client, false, {});
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    select.begin(access, true, &ids);
    for (unsigned i = 0; i < Names; ++i) {
      ZuCheck(select.step() == Zum::ScopeSelect::Lookup && select.id() == i + 1);
      Zum::String name; name << "scope." << i;
      ZuCheck(select.role(Zum::Role{.appID = 1, .id = i + 1, .name = ZuMv(name)}) ==
	Zum::ScopeError::OK);
    }
    ZuCheck(select.step() == Zum::ScopeError::OK);
    auto result = select.take();
    ZuCheck(result.scope == expected && result.roleIDs.length() == Names);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(scopeMatchesApp);
  ZuTestCall(roleNamedScopes);
  ZuTestCall(grantedScopes);
  ZuTestCall(offlineAccess);
  ZuTestCall(outputGrowth);
}
