//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Catalog-wide HTTP adapters with one active method

#ifndef ZgrpcMulti_HH
#define ZgrpcMulti_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZgrpcHTTP.hh>

namespace Zgrpc {

// A header shared by different method defaults is runtime-valued in the HTTP
// Builder catalog. The active method still supplies its compile-time literal;
// a singleton shared default retains the ordinary fixed-field/seed path.
template <typename ...T> struct BuilderDecls;
template <> struct BuilderDecls<> { using T = ZuTypeList<>; };
template <typename K, typename V, typename ...Rest>
struct BuilderDecls<K, V, Rest...> {
  using Values = ZuIf<(V::N <= 1), V, ZuTypeList<>>;
  using T = ZuTypeConcat<ZuTypeList<K, Values>, typename BuilderDecls<Rest...>::T>;
};
template <typename H>
using BuilderHeaders = ZuTypeApply<BuilderDecls, Zhttp::MergeHeaders<H>>::T;

template <typename Impl, typename Catalog, bool Response>
class MultiBuilder : public ZuIf<Response, Zhttp::ResBuilder, Zhttp::ReqBuilder> {
  using Base = ZuIf<Response, Zhttp::ResBuilder, Zhttp::ReqBuilder>;
  using Methods = Catalog::List;
  using Decl = ZuTypeConcat<ZuIf<Response, ResControl, ReqControl>,
    typename ZuIf<Response, typename Catalog::ResHeaders, typename Catalog::ReqHeaders>::List,
    ZuIf<Response, ZhttpHeaders("grpc-status", "grpc-message", "grpc-status-details-bin"),
      ZuTypeList<>>>;

  template <typename M>
  struct Alt : ZuIf<Response, ResBuilder<Alt<M>, M>, ReqBuilder<Alt<M>, M>> {
    MultiBuilder *owner;
    explicit Alt(MultiBuilder *value) : owner{value} { }
    void producerStarted() { owner->impl()->producerStarted(static_cast<M *>(nullptr), this); }
    void producerQueued() { owner->impl()->producerQueued(static_cast<M *>(nullptr), this); }
    void productionFailed(Code::T code) { owner->impl()->productionFailed(static_cast<M *>(nullptr), code); }
    void signMessage(ZuBSpan bytes) { owner->impl()->signMessage(static_cast<M *>(nullptr), bytes); }
    bool neverIndex(ZuBSpan name) const { return owner->impl()->neverIndex(name); }
  };
  using Alternatives = ZuTypeConcat<ZuTypeList<void>, ZuTypeMap<Alt, Methods>>;
  using Active = ZuTypeApply<ZuUnion, Alternatives>;

public:
  using Headers = BuilderHeaders<Decl>;
  using HdrCatalog = WireCatalog<Headers>;
  using Base::header;
  ZuAssert((CatalogValid<Methods>{}));

  uint64_t sequence = 0;
  uint64_t key() const { return sequence; }
  bool replayable() const { return false; }
  bool idempotent(Zhttp::Method::T) const { return false; }
  Zhttp::Method::T method() const { return Zhttp::Method::POST; }
  unsigned status() const { return 200; }

  template <typename M> auto &init(Limits limits = {}) {
    ZuAssert((ZuTypeIn<M, Methods>{}));
    auto ptr = new (m_active.template new_<Alt<M>>()) Alt<M>{this};
    ptr->limits(limits);
    return *ptr;
  }
  template <typename M> auto *builder() {
    ZuAssert((ZuTypeIn<M, Methods>{}));
    return m_active.template ptr<Alt<M>>();
  }
  int methodID() const { return int(m_active.type()) - 1; }

  template <typename M, typename B> void producerStarted(M *, B *) { }
  template <typename M, typename B> void producerQueued(M *, B *b) { (void)b->drain_(); }
  template <typename M> void productionFailed(M *, Code::T) { }
  template <typename M> void signMessage(M *, ZuBSpan) { }

  Zhttp::BodyPolicy::T bodyPolicy() const {
    if (!m_active.type()) return Zhttp::BodyPolicy::None;
    return m_active.cdispatch([](auto, const auto &b) { return b.bodyPolicy(); });
  }
  template <typename L> void operation(L &&l) const {
    if (m_active.type())
      m_active.cdispatch([&l](auto, const auto &b) { b.operation(ZuFwd<L>(l)); });
  }
  template <typename Key, typename Value, typename L> void header(L &&l) const {
    if (!m_active.type()) return;
    m_active.cdispatch([&l](auto, const auto &b) {
      using Values = Zhttp::HeaderValues<Key, typename ZuDecay<decltype(b)>::Headers>;
      if constexpr (ZuTypeIn<Value, Values>{}) b.template header<Key, Value>(ZuFwd<L>(l));
    });
  }
  template <typename Key, typename L> void header(L &&l) const {
    if (!m_active.type()) return;
    m_active.cdispatch([&l](auto, const auto &b) {
      using Global = Zhttp::HeaderValues<Key, Headers>;
      using Values = Zhttp::HeaderValues<Key, typename ZuDecay<decltype(b)>::Headers>;
      if constexpr (!Global::N && Values::N) {
	using Value = Zhttp::HeaderValue<Values>;
	b.template header<Key, Value>([&l]() { l(Value{}()); });
      }
      b.template header<Key>(ZuFwd<L>(l));
    });
  }
  template <typename L> void header(L &&l) const {
    if (m_active.type())
      m_active.cdispatch([&l](auto, const auto &b) { b.header(ZuFwd<L>(l)); });
  }
  void body(Zhttp::BodyEmitter emit) {
    if (m_active.type()) m_active.dispatch([&emit](auto, auto &b) { b.body(ZuMv(emit)); });
  }
  void close() {
    if (m_active.type()) m_active.dispatch([](auto, auto &b) { b.close(); });
  }
  template <typename M> bool send_(Message<ZuIf<Response,
      typename M::Response, typename M::Request>> message) {
    auto b = builder<M>();
    return b && b->send_(ZuMv(message));
  }
  template <typename M> bool sendRaw_(ZuBSpan bytes) {
    auto b = builder<M>();
    return b && b->sendRaw_(bytes);
  }
  template <typename M> bool sendRaw_(ZmRef<ZiIOBuf> buf, bool writerOwned) {
    auto b = builder<M>();
    return b && b->sendRaw_(ZuMv(buf), writerOwned);
  }
  template <bool R = Response, typename = ZuIfT<!R>> bool end_() {
    return m_active.type() && m_active.dispatch([](auto, auto &b) { return b.end_(); });
  }
  template <bool R = Response, typename = ZuIfT<R>>
  bool finish_(Status status = {}, Metadata trailers = Metadata{}) {
    return m_active.type() && m_active.dispatch([&status, &trailers](auto, auto &b) {
      return b.finish_(ZuMv(status), ZuMv(trailers));
    });
  }

private:
  auto impl() { return static_cast<Impl *>(this); }
  Active m_active;
};

template <typename Catalog, bool Response>
using MultiRxHeaders = Zhttp::MergeHeaders<ZuTypeConcat<RxControl,
  typename ZuIf<Response, typename Catalog::ResHeaders, typename Catalog::ReqHeaders>::List,
  ZuIf<Response, typename Catalog::Trailers::List, ZuTypeList<>>>>;

template <typename Impl, typename Catalog, bool Response, bool Raw = false>
class MultiParser : public ParserCore<WireCatalog<MultiRxHeaders<Catalog, Response>>> {
  using Base = ParserCore<WireCatalog<MultiRxHeaders<Catalog, Response>>>;
  using Methods = Catalog::List;
public:
  ZuAssert((CatalogValid<Methods>{}));
  template <typename M, typename Receiver>
  bool init(ZmRef<Receiver> receiver, Limits limits = {},
      unsigned headerMax = Zhttp::DefltMaxHeaderSection) {
    ZuAssert((ZuTypeIn<M, Methods>{}));
    return Base::input_(makeInput<M, Response, Raw>(ZuMv(receiver), limits, headerMax));
  }
  Limits limits() const { return {}; }
  unsigned headerMax() const { return Zhttp::DefltMaxHeaderSection; }
  bool unknownMethod(Zhttp::Method::T, Zhttp::Target &) { return false; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if constexpr (Response) return true;
    else {
    auto self = static_cast<Impl *>(this);
    int id = Catalog::methodMatch(target.path);
    if (method != Zhttp::Method::POST || id < 0) return self->unknownMethod(method, target);
    return ZuSwitch::dispatch<Methods::N>(id, [this, self](auto i) {
      using M = ZuType<i, Methods>;
      return this->template init<M>(self->receiver(static_cast<M *>(nullptr)),
	self->limits(), self->headerMax());
    });
    }
  }
};

template <typename Impl, typename Catalog>
struct MReqBuilder : MultiBuilder<Impl, Catalog, false> { };
template <typename Impl, typename Catalog>
struct MResBuilder : MultiBuilder<Impl, Catalog, true> { };
template <typename Impl, typename Catalog>
struct MReqParser : MultiParser<Impl, Catalog, false> { };
template <typename Impl, typename Catalog>
struct MResParser : MultiParser<Impl, Catalog, true> { };

} // Zgrpc

#endif /* ZgrpcMulti_HH */
