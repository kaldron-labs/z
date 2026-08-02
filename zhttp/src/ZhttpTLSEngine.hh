//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - shared TLS ALPN policy and engine configuration

#ifndef ZhttpTLSEngine_HH
#define ZhttpTLSEngine_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/Ztls.hh>

#include <zlib/ZhttpConfig.hh>
#include <zlib/ZhttpH2.hh>

namespace Zhttp {
namespace TLS_ {

inline bool validPolicy(H2Policy::T policy)
{
  return policy >= H2Policy::Force && policy <= H2Policy::Disable;
}

inline bool valid(const H2Config &config)
{
  return config.hpackRxCapacity() <= H2Config::MaxHPackCapacity &&
    config.hpackTxCapacity() <= H2Config::MaxHPackCapacity &&
    config.maxFrameSize() >= H2::DefltFrameSize &&
    config.maxFrameSize() <= H2::MaxFrameSize &&
    config.initialWindowSize() <= H2::MaxWindow &&
    config.maxConcurrentStreams() && config.maxPending() &&
    config.maxQueuedFrames() && config.maxStreamID() &&
    config.maxStreamID() <= H2::MaxWindow &&
    (config.maxStreamID() & 1U) && validPolicy(config.policy());
}

template <typename Params>
inline void alpn(Params &params, H2Policy::T policy)
{
  switch (policy) {
    case H2Policy::Force:
      params.alpn(ZuSpan<ZuCSpan>{"h2"});
      break;
    case H2Policy::Prefer:
      params.alpn(ZuSpan<ZuCSpan>{"h2", "http/1.1"});
      break;
    case H2Policy::Disable:
      params.alpn(ZuSpan<ZuCSpan>{"http/1.1"});
      break;
  }
}

inline Ztls::ClientParams clientParams(
  const EngineConfig &engine, const H2Config &config)
{
  Ztls::ClientParams params{
    engine.mx(), engine.rxThread(), engine.txThread()};
  params.asyncThread(engine.asyncThread())
    .caPath(config.caPath()).certPath(config.certPath())
    .keyPath(config.keyPath());
  alpn(params, config.policy());
  return params;
}

inline Ztls::ServerParams serverParams(
  const EngineConfig &engine, const H2Config &config)
{
  Ztls::ServerParams params{
    engine.mx(), engine.rxThread(), engine.txThread()};
  params.asyncThread(engine.asyncThread())
    .caPath(config.caPath()).certPath(config.certPath())
    .keyPath(config.keyPath())
    .mTLS(config.mTLS()).cacheTimeout(config.cacheTimeout());
  alpn(params, config.policy());
  return params;
}

ZhttpAPI Version::T version(ZuCSpan alpn, H2Policy::T policy);

} // namespace TLS_
} // namespace Zhttp

#endif /* ZhttpTLSEngine_HH */
