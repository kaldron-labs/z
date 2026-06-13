//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP/3 session helpers

#ifndef ZhttpH3Session_HH
#define ZhttpH3Session_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

#include <zlib/ZiTransport.hh>

namespace Zhttp { namespace H3 {

template <typename Link, typename StreamRef, typename = void>
struct HasStreamSend : public ZuFalse { };
template <typename Link, typename StreamRef>
struct HasStreamSend<Link, StreamRef,
  decltype(
    ZuDeclVal<Link *>()->send(
      ZuDeclVal<StreamRef &>(), ZuDeclVal<ZuBSpan>(), false),
    void())> : public ZuTrue { };

template <typename Link, typename StreamRef>
struct QPackStreamTx : public QPackEncoderTx {
  QPackStreamTx() = default;
  QPackStreamTx(Link *link_, StreamRef *stream_) :
    link{link_}, stream{stream_} { }

  void init(Link *link_, StreamRef *stream_) {
    link = link_;
    stream = stream_;
  }

  bool write(ZuBSpan span) override {
    if (!link || !stream || !*stream || !span) return false;
    if constexpr (HasStreamSend<Link, StreamRef>{})
      return link->send(*stream, span, false);
    else
      return false;
  }

  Link		*link = nullptr;
  StreamRef	*stream = nullptr;
};

template <typename Link, typename StreamRef>
struct Cxn {
  using State = CxnState;

  bool openLocal(Link &link, const Params &params = Params{}) {
    control = link.stream(Zi::StreamType::Simplex);
    enc = link.stream(Zi::StreamType::Simplex);
    dec = link.stream(Zi::StreamType::Simplex);
    if (!control || !enc || !dec) return false;

    using Scratch = ZtArray<char, ZtArrayHeapID<"Zhttp.H3.Cxn">>;
    auto payload = ZtLocalArray(Scratch, 64);
    CountBytes count;
    if (putVar(count, 0x01) < 0 ||
	putVar(count, params.qpackTableCapacity()) < 0 ||
	putVar(count, 0x06) < 0 ||
	putVar(count, params.maxHeaderListSize()) < 0 ||
	putVar(count, 0x07) < 0 ||
	putVar(count, params.qpackBlockedStreams()) < 0)
      return false;
    if (putVar(payload, 0x00) < 0 ||
	putVar(payload, 0x04) < 0 ||
	putVar(payload, count.length()) < 0 ||
	putVar(payload, 0x01) < 0 ||
	putVar(payload, params.qpackTableCapacity()) < 0 ||
	putVar(payload, 0x06) < 0 ||
	putVar(payload, params.maxHeaderListSize()) < 0 ||
	putVar(payload, 0x07) < 0 ||
	putVar(payload, params.qpackBlockedStreams()) < 0)
      return false;
    if (!link.send(control, payload, false)) return false;
    {
      auto tx = enc->txStream();
      TxBytes out{tx};
      if (putVar(out, 0x02) < 0) return false;
      tx.flush();
    }
    {
      auto tx = dec->txStream();
      TxBytes out{tx};
      if (putVar(out, 0x03) < 0) return false;
      tx.flush();
    }
    qpackEncoder.init(&link, &enc);
    qpackDecoder.init(&link, &dec);
    state = State::Ready;
    return true;
  }

  bool peerControlStream() {
    if (peerControl) {
      state = State::Error;
      return false;
    }
    peerControl = true;
    return true;
  }
  bool peerEncoderStream() {
    if (peerEncoder) {
      state = State::Error;
      return false;
    }
    peerEncoder = true;
    return true;
  }
  bool peerDecoderStream() {
    if (peerDecoder) {
      state = State::Error;
      return false;
    }
    peerDecoder = true;
    return true;
  }
  QPackRxTable *qpackRx() { return &qpackRxTable; }
  QPackTxTable *qpackTx() { return &qpackTxTable; }

  State::T		state = State::Init;
  StreamRef		control;
  StreamRef		enc;
  StreamRef		dec;
  bool			peerControl = false;
  bool			peerEncoder = false;
  bool			peerDecoder = false;
  QPackRxTable		qpackRxTable;
  QPackTxTable		qpackTxTable;
  QPackStreamTx<Link, StreamRef> qpackEncoder;
  QPackStreamTx<Link, StreamRef> qpackDecoder;
};

template <typename Impl, typename Cxn_>
struct CxnStream : public CxnParser<Impl> {
  using Cxn = Cxn_;
  using Base = CxnParser<Impl>;
  using State = CxnState;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  State::T h3State() const { return impl()->h3Cxn().state; }
  void h3State(State::T state) { impl()->h3Cxn().state = state; }
  bool peerControlStream() { return impl()->h3Cxn().peerControlStream(); }
  bool peerEncoderStream() { return impl()->h3Cxn().peerEncoderStream(); }
  bool peerDecoderStream() { return impl()->h3Cxn().peerDecoderStream(); }
  QPackRxTable *qpackRx() { return impl()->h3Cxn().qpackRx(); }
  QPackTxTable *qpackTx() { return impl()->h3Cxn().qpackTx(); }
};

template <typename Stream, typename Builder>
bool sendReq(Stream &stream, Builder &builder, bool fin = true) {
  auto tx = stream.txStream();
  builder.request(tx);
  builder.finish(tx);
  if (fin) stream.link()->send(stream.link()->findStream(stream.id()), "", true);
  return true;
}

template <typename Stream, typename Builder>
bool sendResp(Stream &stream, Builder &builder, bool fin = true) {
  auto tx = stream.txStream();
  builder.response(tx);
  builder.finish(tx);
  if (fin) stream.link()->send(stream.link()->findStream(stream.id()), "", true);
  return true;
}

}} // Zhttp::H3

#endif /* ZhttpH3Session_HH */
