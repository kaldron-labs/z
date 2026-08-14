//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiLog.hh>

#include "zrest_proto.hh"

void Client::tokens(const TokenResponse *tokens, bool refresh)
{
  ZmRef<TokenState> state;
  auto receipt = Zm::now();
  if (tokens && tokens->accessToken && tokens->refreshToken &&
      tokens->accessToken.length() <= JWTMax &&
      tokens->refreshToken.length() <= JWTMax && tokens->expiresIn &&
      receipt.sec() > 0 &&
      tokens->expiresIn <= uint64_t(INT64_MAX - receipt.sec())) {
    state = new TokenState{};
    state->refreshToken = tokens->refreshToken;
    state->bearer << "Bearer " << tokens->accessToken;
    state->deadline = receipt.sec() + int64_t(tokens->expiresIn);
  }
  txRun(0, [this, state = ZuMv(state), refresh]() mutable {
    retire_();
    if (m_state == Failed || m_state == Complete) return;
    if (!state) { fail_(); return; }
    m_tokens = ZuMv(state).constRef();
    m_state = Ready;
    if (m_options->verbose) {
      auto now = Zm::now();
      uint64_t ns = uint64_t(now.sec()) * 1000000000ULL + now.nsec();
      ZiLOG(Info, "zrest", ([refresh, ns](auto &s) {
	s << "event=" << (refresh ? "refresh" : "auth") <<
	  " time_ns=" << ns;
      }));
    }
    if (!refresh && m_interval) {
      m_next = Zm::now() + m_interval;
      armTimer_();
    } else drive_();
  });
}

void Client::unauthorized(bool ping, uint64_t logicalID, bool replayed,
    ZmRef<const TokenState> state)
{
  txRun(0, [this, ping, logicalID, replayed, state = ZuMv(state)]() mutable {
    retire_();
    if (m_state == Failed || m_state == Complete) return;
    if (!ping || replayed) { fail_(); return; }
    m_pending.unshift(Pending{logicalID, true});
    if (state.ptr() == m_tokens.ptr()) refresh_();
    else drive_();
  });
}

void Client::authenticate_()
{
  m_state = Authenticating;
  auto request = new AuthReq{};
  request->client = this;
  request->username = m_options->user;
  request->password = m_options->pass;
  send_<AuthBuilder>(request);
}

void Client::refresh_()
{
  if (m_state == Refreshing) return;
  if (!m_tokens) { fail_(); return; }
  m_state = Refreshing;
  auto request = new RefreshReq{};
  request->client = this;
  request->state = m_tokens;
  send_<RefreshBuilder>(request);
}
