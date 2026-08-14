//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmTime.hh>

#include "zrestd_proto.hh"

bool App::init()
{
  return m_rng.init();
}

int App::auth_(const AuthParser &parser, bool ok,
    ZmRef<TokenResponse> &tokens)
{
  if (!ok) { fail_(); return AuthResult::Failed; }
  const auto &credentials = *parser.object;
  if (!credentials.username || !credentials.password ||
      credentials.username != m_options->user ||
      credentials.password != m_options->pass)
    return AuthResult::Unauthorized;
  tokens = new TokenResponse{};
  if (!jwtIssuePair(m_rng, m_options->jwtSecret, credentials.username,
	Zm::now().sec(), m_options->accessSecs, m_options->refreshSecs,
	*tokens)) {
    return AuthResult::InternalError;
  }
  return AuthResult::OK;
}

void App::authOK_()
{
  ++m_auth;
  event_("auth");
}

int App::refresh_(const RefreshParser &parser, bool ok,
    ZmRef<TokenResponse> &tokens)
{
  if (!ok) { fail_(); return AuthResult::Failed; }
  CredString subject;
  if (!parser.object->refreshToken ||
      !jwtValidate(m_options->jwtSecret, parser.object->refreshToken,
	TokenType::refresh, Zm::now().sec(), subject))
    return AuthResult::Unauthorized;
  tokens = new TokenResponse{};
  if (!jwtIssuePair(m_rng, m_options->jwtSecret, subject, Zm::now().sec(),
	m_options->accessSecs, m_options->refreshSecs, *tokens)) {
    return AuthResult::InternalError;
  }
  return AuthResult::OK;
}

void App::refreshOK_()
{
  ++m_refresh;
  event_("refresh");
}
