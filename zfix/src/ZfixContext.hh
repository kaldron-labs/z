//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Immutable receive context retaining one enclosing message backing

#ifndef ZfixContext_HH
#define ZfixContext_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuDateTime.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixRxMsg.hh>

namespace Zfix {

// Copy this context with borrowed payload values that escape a receive hook.
// It owns the enclosing message once, not each field or the physical link.
// Decoded owning containers have their own lifetimes. Runtime session identity
// is not a persistence key; wire identity remains in the retained header.
class RequestContext {
public:
  RequestContext() = default;

  // The node must have completed checked decoding as this entry.
  template <typename Entry>
  RequestContext(Entry *, uint128_t session, ZmRef<const RxQueue::Node> message) :
    m_message{ZuMv(message)} {
    if (!m_message) return;
    using Key = typename Entry::Discriminator;
    m_tag = Key::tag; m_key = Key::value;
    const auto &input = m_message->data();
    m_context = {.session = session, .connection = input.connection,
      .epoch = input.epoch, .seqNo = input.header.seqNo,
      .replay = input.header.possDup, .possDup = input.header.possDup,
      .version = input.header.beginString};
  }

  explicit operator bool() const { return m_message.ptr(); }

  template <typename Entry> bool is() const {
    using Key = typename Entry::Discriminator;
    return m_message && m_message->data().header.msgType == ZuCSpan{Entry::msgType} &&
      m_tag == Key::tag && m_key == ZuCSpan{Key::value};
  }

  const Context &context() const { return m_context; }
  const Fmt::Header &header() const { ZmAssert(m_message); return m_message->data().header; }
  const Fmt::Trailer &trailer() const { ZmAssert(m_message); return m_message->data().trailer; }
  const Fmt::FrameView &frame() const { ZmAssert(m_message); return m_message->data().frame; }

  uint64_t memory() const {
    if (!m_message) return 0;
    const auto &input = m_message->data();
    return input.nativeMemory ? input.charge : 0;
  }

  ZuTime received() const { ZmAssert(m_message); return m_message->data().received; }
  ZuDateTime utc() const { ZmAssert(m_message); return m_message->data().utc; }

private:
  ZmRef<const RxQueue::Node>	m_message;
  Context			m_context;
  // selected descriptor constant, not a per-field input retain
  ZuCSpan			m_key;
  unsigned			m_tag = 0;
};

namespace Context_ {

// Missing overloads are side-effect-safe rejects, including when a derived
// application supplies only some of its catalog's receive hooks.
template <typename Impl, typename Entry, typename = void> struct Receive {
  static Fmt::Error call(Impl *, Entry *, const typename Entry::Object &,
      const RequestContext &) { return {Fmt::ErrorCode::Unresolved, 35}; }
};

template <typename Impl, typename Entry>
struct Receive<Impl, Entry, decltype(ZuDeclVal<Impl *>()->fixReceiveRx_(
    ZuDeclVal<Entry *>(), ZuDeclVal<const typename Entry::Object &>(),
    ZuDeclVal<const RequestContext &>()), void())> {
  static Fmt::Error call(Impl *impl, Entry *type, const typename Entry::Object &object,
      const RequestContext &context) { return impl->fixReceiveRx_(type, object, context); }
};

// Reply policy belongs to the application. Missing overloads refuse locally;
// accepted contexts are reusable for any application-selected response count.
template <typename Impl, typename Request, typename Response, typename Ctx,
  typename = void> struct Reply {
  static Fmt::Error call(Impl *, const Ctx &) { return {Fmt::ErrorCode::Unresolved, 35}; }
};

template <typename Impl, typename Request, typename Response, typename Ctx>
struct Reply<Impl, Request, Response, Ctx, decltype(ZuDeclVal<Impl *>()->fixReplyRx_(
    ZuDeclVal<Request *>(), ZuDeclVal<Response *>(), ZuDeclVal<const Ctx &>()), void())> {
  static Fmt::Error call(Impl *impl, const Ctx &context) {
    return impl->fixReplyRx_(static_cast<Request *>(nullptr),
      static_cast<Response *>(nullptr), context);
  }
};

} // Context_

} // Zfix

#endif /* ZfixContext_HH */
