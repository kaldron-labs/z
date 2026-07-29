//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP borrowed logical-stream facade

#ifndef ZhttpStream_HH
#define ZhttpStream_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZmAssert.hh>

#include <zlib/ZiRxStream.hh>

namespace Zhttp {

namespace Stream_ {

struct Frame {
  int64_t operator ()(ZuBSpan) const;
};
struct Data {
  void operator ()(ZuBSpan) const;
};
struct Tx {
  template <typename Stream>
  auto operator ()(Stream &stream) const ->
    decltype(stream.flush(), void());
};

template <typename Tx_, typename = void>
struct IsTx : public ZuFalse { };
template <typename Tx_>
struct IsTx<Tx_, decltype(
  ZuDeclVal<Tx_ &>().flush(), void())> : public ZuTrue { };

template <typename Link, typename = void>
struct IsLink : public ZuFalse { };
template <typename Link>
struct IsLink<Link, decltype(
  ZuDeclVal<Link &>().streamLocalCap(),
  ZuDeclVal<Link &>().streamPeerCap(),
  ZuDeclVal<Link &>().streamTx(Tx{}),
  ZuDeclVal<Link &>().streamTxEnd(),
  ZuDeclVal<Link &>().streamTxReset(),
  void())> : public ZuTrue { };

template <typename Rx, typename = void>
struct IsRx : public ZuFalse { };
template <typename Rx>
struct IsRx<Rx, decltype(
  ZuDeclVal<Rx &>().input(),
  ZuDeclVal<Rx &>().events(),
  ZuDeclVal<Rx &>().consume(Frame{}, Data{}),
  ZuDeclVal<Rx &>().empty(),
  ZuDeclVal<Rx &>().complete(),
  ZuDeclVal<Rx &>().failed(),
  ZuDeclVal<Rx &>().available(),
  void())> : public ZuTrue { };

template <typename Link>
class Base {
public:
  Base(Link &link) : m_link{&link} {
    static_assert(IsLink<Link>{}, "invalid Zhttp logical-stream link");
  }

  bool localCap() const { return m_link->streamLocalCap(); }
  bool peerCap() const { return m_link->streamPeerCap(); }

  template <typename L>
  void txStream(L &&l) {
    m_link->streamTx([&l](auto &tx) {
      static_assert(
	Stream_::IsTx<ZuDecay<decltype(tx)>>{},
	"invalid Zhttp logical-stream Tx stream");
      ZuFwd<L>(l)(tx);
    });
  }

  void end() { m_link->streamTxEnd(); }
  void reset() { m_link->streamTxReset(); }

private:
  Link	*m_link;
};

} // namespace Stream_

// Non-owning, shard-affine facade.  Link and callback-scoped Tx objects
// outlive each call only; txStream() is not transport-send completion.
template <typename Link>
class Stream : public Stream_::Base<Link> {
  using Base = Stream_::Base<Link>;

public:
  Stream(Link &link) : Base{link} { }
};

template <typename Link>
Stream(Link &) -> Stream<Link>;

// Rx-owner adapter.  Disable ingress on the Rx shard before disable_(), drain
// pending Rx work, then call final_() from that drain continuation.
template <typename Link, typename Consumer>
class StreamDispatch {
public:
  void init(Link &link, Consumer &consumer) {
    ZmAssert(!m_link && !m_consumer);
    m_link = &link;
    m_consumer = &consumer;
  }

  template <typename Rx>
  int process(Rx &rx) {
    static_assert(
      Stream_::IsRx<Rx>{}, "invalid Zhttp logical-stream Rx layer");
    if (!m_link || !m_consumer) return 0;
    auto link = m_link;
    int rc = m_consumer->process(Stream{*link}, rx);
    if (rc < 0) Stream{*link}.reset();
    return rc;
  }

  void disable_() { m_link = nullptr; }
  void final_() {
    ZmAssert(!m_link);
    m_consumer = nullptr;
  }

private:
  Link		*m_link = nullptr;
  Consumer	*m_consumer = nullptr;
};

} // namespace Zhttp

#endif /* ZhttpStream_HH */
