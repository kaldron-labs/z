//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/zumd.hh>

using namespace ZuTestUtil;

static void scopeMatchesApp()
{
  ZuTestScope(scopeMatchesApp);
  Zum::Client client{.id = "client", .appID = 1};
  Zum::ClientAccess access{.clientID = "client", .appID = 1, .roleIDs = {7}};
  Zum::Role scopes[2] = {
    {.appID = 1, .id = 7, .name = "operator",
      .state = Zum::State::Active},
    {.appID = 2, .id = 7, .name = "billing",
      .state = Zum::State::Active}
  };
  Zum::ScopeSelection selection;
  ZuCheck(Zum::selectScopes(client, access, "operator", scopes, selection) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.roleIDs.length() == 1 && selection.roleIDs[0] == 7 &&
    selection.scope == "operator");
  ZuCheck(Zum::selectScopes(client, access, "billing", scopes, selection) ==
    Zum::ScopeError::Unavailable);
}

static void roleNamedScopes()
{
  ZuTestScope(roleNamedScopes);
  Zum::Client client{.id = "client", .appID = 1};
  Zum::ClientAccess access{.clientID = "client", .appID = 1, .roleIDs = {7, 8}};
  Zum::Role scopes[2] = {
    {.appID = 1, .id = 7, .name = "operator",
      .state = Zum::State::Active},
    {.appID = 1, .id = 8, .name = "auditor",
      .state = Zum::State::Active}
  };
  Zum::ScopeSelection selection;
  ZuCheck(Zum::selectScopes(client, access, "operator auditor", scopes, selection) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.roleIDs.length() == 2 && selection.roleIDs[0] == 7 &&
    selection.roleIDs[1] == 8 && selection.scope == "operator auditor");

  client.identityScopes.push("openid");
  ZuCheck(Zum::selectScopes(client, access, "openid", scopes, selection) ==
    Zum::ScopeError::OK);
  ZuCheck(selection.identity && !selection.roleIDs && selection.scope == "openid");

  scopes[1].state = Zum::State::Disabled;
  ZuCheck(Zum::selectScopes(client, access, "operator auditor", scopes, selection) ==
    Zum::ScopeError::Unavailable);
}

int main()
{
  ZuTestMain();
  ZuTestCall(scopeMatchesApp);
  ZuTestCall(roleNamedScopes);
}
