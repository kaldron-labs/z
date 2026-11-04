//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixRaw.hh>

namespace Zfix {

RawOutput::Ref RawOutput::fromFrame(TxBody::Ref body, TxContext context,
    ZmRef<ZiIOBuf> bytes, Fmt::Error &error, Fmt::Limits limits, ZuDateTime original)
{
  error = {};
  if (!body || !bytes || bytes->failed()) { error = {Fmt::ErrorCode::Output}; return {}; }
  auto r = Fmt::frame(ZuCSpan{bytes->cspan()}, limits);
  if (r.state != Fmt::FrameState::Complete || r.consumed != bytes->length) {
    error = r.error ? r.error : Fmt::Error{r.state == Fmt::FrameState::Incomplete ?
      Fmt::ErrorCode::Truncated : Fmt::ErrorCode::Envelope}; return {};
  }
  if (r.view.msgType != body->msgType()) { error = {Fmt::ErrorCode::Value, 35}; return {}; }
  TxImage image{ZuMv(bytes)};
  unsigned offset = unsigned(r.view.content.data() - r.view.wire.data());
  error = body->validate(TxFrame_::Check{image.view(offset, r.view.content.length()), offset}, limits);
  if (error) return {};
  return new RawOutput{ZuMv(body), context, ZuMv(image), original};
}
} // Zfix

