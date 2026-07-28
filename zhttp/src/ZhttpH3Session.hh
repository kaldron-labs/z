//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP/3 session utilities

#ifndef ZhttpH3Session_HH
#define ZhttpH3Session_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

namespace Zhttp {

namespace H3 {

template <typename Link, typename StreamRef, typename = void>
struct HasStreamSend : public ZuFalse { };
template <typename Link, typename StreamRef>
struct HasStreamSend<Link, StreamRef,
  decltype(
    ZuDeclVal<Link *>()->send(
      ZuDeclVal<StreamRef &>(), ZuDeclVal<ZuBSpan>(), false),
    void())> : public ZuTrue { };

// HTTP/3 connection streams and connection-level QPACK state
template <typename Link, typename StreamRef>
struct Cxn {
  using State = CxnState;

  // Rx thread exclusive
  State::T		state = State::Init;
  Link			*link_ = nullptr;
  StreamRef		control;
  StreamRef		enc;
  StreamRef		dec;
  bool			peerControl = false;
  bool			peerEncoder = false;
  bool			peerDecoder = false;
  uint64_t		errorCode = 0;
  QPackRxTable		qpackRxTable;

  bool openLocal(Link &link, const Params &params = Params{}) {
    using StreamType = typename Link::StreamType;
    control = link.stream(StreamType::Simplex);
    enc = link.stream(StreamType::Simplex);
    dec = link.stream(StreamType::Simplex);
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
    link_ = &link;
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
  QPackTxTable *qpackTx() { return link_ ? link_->qpackTx() : nullptr; }
  bool qpackTxInsn(QPackInsn::T type, uint64_t value) {
    return qpackTx_(QPackDecoderError,
      [type, value](QPackTxTable &tx) {
	return tx.applyDecoder(type, value);
      });
  }
  bool qpackTxMaxCapacity(uint64_t capacity) {
    return qpackTx_(SettingsError, [capacity](QPackTxTable &tx) {
      return capacity <= uint32_t(-1) &&
	tx.setMaxCapacity(uint32_t(capacity));
    });
  }
  bool qpackEncoderWrite(ZuBSpan span) { return writeQPack_(enc, span); }
  bool qpackDecoderWrite(ZuBSpan span) { return writeQPack_(dec, span); }
  void error(uint64_t code) {
    if (errorCode) return;
    errorCode = code;
    state = State::Error;
    if (link_) link_->disconnect(code);
  }

  template <typename PathInfo>
  void pathUpdate(const PathInfo &) { }
  template <typename MigrationResult>
  void migrationStarted(const MigrationResult &) { }
  template <typename MigrationResult>
  void migrationPromoted(const MigrationResult &) { }
  template <typename MigrationResult>
  void migrationFailed(const MigrationResult &) { }

  template <typename L>
  bool qpackTx_(uint64_t error, L &&l) {
    if (!link_) return false;
    auto link = link_;
    link->app()->txRun([
      link,
      error,
      l = ZuFwd<L>(l)
    ]() mutable {
      auto tx = link->qpackTx();
      if (!tx || link->closed()) return;
      if (l(*tx)) return;
      link->disconnect(error);
    });
    return true;
  }

  bool writeQPack_(StreamRef &stream, ZuBSpan span) {
    if (!link_ || !stream || !span) return false;
    if constexpr (HasStreamSend<Link, StreamRef>{})
      return link_->send(stream, span, false);
    else
      return false;
  }
};

// Peer unidirectional connection stream adapter
template <typename Impl, typename Cxn_>
struct CxnStream : public CxnParser<Impl> {
  using Cxn = Cxn_;
  using Base = CxnParser<Impl>;
  using State = CxnState;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  State::T h3State() const { return impl()->h3Cxn().state; }
  void h3State(State::T state) { impl()->h3Cxn().state = state; }
  bool h3Server() const { return impl()->link()->isServer(); }
  void h3Error(uint64_t error) { impl()->h3Cxn().error(error); }
  bool peerControlStream() { return impl()->h3Cxn().peerControlStream(); }
  bool peerEncoderStream() { return impl()->h3Cxn().peerEncoderStream(); }
  bool peerDecoderStream() { return impl()->h3Cxn().peerDecoderStream(); }
  QPackRxTable *qpackRx() { return impl()->h3Cxn().qpackRx(); }
  QPackTxTable *qpackTx() { return impl()->h3Cxn().qpackTx(); }
  bool qpackDecoderWrite(ZuBSpan span) {
    return impl()->h3Cxn().qpackDecoderWrite(span);
  }
  bool qpackTxInsn(QPackInsn::T type, uint64_t value) {
    return impl()->h3Cxn().qpackTxInsn(type, value);
  }
  bool qpackTxMaxCapacity(uint64_t capacity) {
    return impl()->h3Cxn().qpackTxMaxCapacity(capacity);
  }
};

// Request/response transmit helpers
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

// Server request stream parser adapter
template <typename Impl, typename Parser_>
struct ServerStream {
  using Parser = Parser_;
  using State = typename Parser::State;

  // Rx thread exclusive
  Parser	parser;
  bool		complete_ = false;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename Stream, typename Rx>
  int processReq(Stream &stream, Rx &rx) {
    if (complete_) return 1;
    auto state = parser.process(rx);
    if (state == State::Error) return impl()->error(stream, parser);
    if (state == State::Complete) {
      complete_ = true;
      return impl()->request(stream, parser);
    }
    return 0;
  }

  template <typename Stream>
  int error(Stream &, Parser &) { return -1; }
  template <typename Stream>
  int request(Stream &, Parser &) { return 0; }
};

} // namespace H3

} // namespace Zhttp

#endif /* ZhttpH3Session_HH */
