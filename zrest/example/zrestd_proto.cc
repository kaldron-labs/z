//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiLog.hh>

#include "zrestd_proto.hh"

int App::ping_(const PingParser &parser, bool ok)
{
  auto authorization = ZuCSpan{parser.authorization};
  auto prefix = authorization;
  prefix.trunc(7);
  auto token = authorization;
  token.offset(7);
  bool bearer = authorization.length() > 7 && prefix == "Bearer " &&
    token.find([](char c) { return c == ' '; }) < 0;
  CredString subject;
  if (!ok || !parser.object->ping || parser.authorizationCount != 1 ||
      !bearer || !jwtValidate(m_options->jwtSecret, token,
	TokenType::access, Zm::now().sec(), subject))
    return PingResult::Unauthorized;
  return PingResult::OK;
}

void App::pong_()
{
  ++m_pong;
  event_("pong");
  if (m_pong < m_options->requests) return;
  ZiLOG(Info, "zrestd", ([auth = m_auth, refresh = m_refresh,
      pong = m_pong](auto &s) {
    s << "event=summary auth=" << auth << " refresh=" << refresh <<
      " pong=" << pong;
  }));
  signal_(true);
}
