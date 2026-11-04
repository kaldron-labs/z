//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixTxCapture_HH
#define ZfixTxCapture_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixTxModel.hh>

namespace Zfix {

// A queued typed model owns unformatted values. Every attempt prints directly
// into its destination; the model has no encoded whole-message payload.
template <typename Entry> class TxCaptured :
    public ZmNHeap<TxCaptured<Entry>>, public TxBody {
friend inline ZuStringT<"Zfix.TxModel"> ZmNHeapID(TxCaptured *);
  using Facet = typename Entry::Mapping;
  using Model = TxModel_::Model<typename Entry::Object, Facet, ArchiveField>;
  struct Header {
    const TxConfig &config;
    const TxContext &context;
    template <typename F, typename Out> bool field(Out &out) const {
      if constexpr (sessionTag(Fmt::tag<F>())) {
	saveSessionField<F, Facet>(out, config, context); return true;
      }
      return false;
    }
  };
public:
  using ZmNHeap<TxCaptured>::operator new;
  using ZmNHeap<TxCaptured>::operator delete;

  TxCaptured(const typename Entry::Object &source, uint64_t id, bool admin,
      Fmt::Limits limits) : TxBody{nullptr, 0, nullptr, 0}, m_arena{limits} {
    this->m_modelType = Entry::MsgType;
    this->m_id = id; this->m_admin = admin;
    this->m_emit = [](TxFrame &frame, const TxBody &body,
	const TxConfig &config, TxContext context) {
      auto &model = static_cast<const TxCaptured &>(body);
      auto &out = frame.body();
      out.field(35); out << Entry::MsgType; out.end();
      model.m_model.emit(out, model.m_arena, Header{config, context});
      model.selector_(out);
    };
    this->m_archive = [](Fmt::Sink &out, const TxBody &body) {
      auto &model = static_cast<const TxCaptured &>(body);
      model.m_model.emit(out, model.m_arena);
      model.selector_(out);
    };
    using Key = typename Entry::Discrim;
    if constexpr (Fmt::hasSignature<typename Entry::Object, Facet>() || Key::Tag)
      this->m_validate = [](const TxBody &, const TxFrame_::Check &frame, Fmt::Limits limits) {
	return TxFrame_::signedCheck<Entry>(frame, limits);
      };
    m_model.capture(source, m_arena);
    using Key_ = typename Entry::Discrim;
    this->m_modelEmpty = m_model.empty() && !(Key_::Tag &&
      !Fmt::reserved(Key_::Tag) && !sessionTag(Key_::Tag) && !TxFrame_::bodyContains<Entry>());
  }
  Fmt::Error error() const { return m_arena.error; }
  uint64_t memory() const override {
    return ZmHeapAllocSize<sizeof(TxCaptured)>{} + m_arena.memory() + m_model.memory();
  }
private:
  void selector_(Fmt::Sink &out) const {
    using Key = typename Entry::Discrim;
    if constexpr (Key::Tag && !Fmt::reserved(Key::Tag) && !sessionTag(Key::Tag) &&
	!TxFrame_::bodyContains<Entry>()) {
      out.field(Key::Tag); out << Key::Value; out.end();
    }
  }
  TxModel_::Arena m_arena;
  Model m_model;
};

// Validation retains root component/group declarations. Duplicate scalar or
// identical group declarations share one wire field and retain both required
// obligations; conflicting binary/group wire declarations are schema errors.
template <typename Entry, typename O, typename Facet> struct TxJoined {
  using Base = typename Fmt::Schema<typename Entry::Object, Facet>::Fields;
  using Extra = typename Fmt::Schema<O, Facet>::Fields;

  template <typename F, typename G> static consteval bool same() {
    if constexpr (Fmt::Component<F, Facet>{} || Fmt::Component<G, Facet>{}) return false;
    else return Fmt::tag<F>() == Fmt::tag<G>();
  }
  template <typename F> static consteval bool added() {
    bool found = false;
    ZuUnroll::all<Extra>([&found]<typename G>() { if (same<F, G>()) found = true; });
    return found;
  }
  template <typename F> using Keep = ZuBool<!added<F>()>;

  template <typename F> static consteval bool required() {
    bool required = ZuTypeIn<ZuFieldProp::Required, typename F::Props>{};
    ZuUnroll::all<Base>([&required]<typename G>() {
      if constexpr (same<F, G>()) {
	ZuAssert((F::Type::Code == G::Type::Code));
	ZuAssert((Fmt::payloadTag<F>() == Fmt::payloadTag<G>()));
	if constexpr (Fmt::GroupField<F, Facet>{} || Fmt::GroupField<G, Facet>{})
	  ZuAssert((ZuIsSame<typename F::T, typename G::T>{}));
	if constexpr (ZuTypeIn<ZuFieldProp::Required, typename G::Props>{}) required = true;
      }
    });
    return required;
  }
  template <typename F> struct Field : F {
    using Props = ZuIf<required<F>() && !ZuTypeIn<ZuFieldProp::Required, typename F::Props>{},
      ZuTypeConcat<typename F::Props, ZuTypeList<ZuFieldProp::Required>>, typename F::Props>;
  };
  using Fields = ZuTypeConcat<ZuTypeGrep<Keep, Base>, ZuTypeMap<Field, Extra>>;
friend Fields ZuFields_(TxJoined *, Facet *);
};

// Typed extra fields retain their owning semantic snapshot alongside the base.
// Authentication adds values here without materializing a second FIX body.
template <typename Entry, typename O, typename Facet> class TxExtended :
    public ZmNHeap<TxExtended<Entry, O, Facet>>, public TxBody {
friend inline ZuStringT<"Zfix.TxExtra"> ZmNHeapID(TxExtended *);
public:
  using ZmNHeap<TxExtended>::operator new;
  using ZmNHeap<TxExtended>::operator delete;
  TxExtended(TxBody::Ref base, const O &source, Fmt::Limits limits) :
      TxBody{nullptr, 0, nullptr, 0}, m_base{ZuMv(base)}, m_arena{limits} {
    this->m_id = m_base->id(); this->m_admin = m_base->administrative();
    this->m_modelType = m_base->msgType();
    this->m_validate = [](const TxBody &, const TxFrame_::Check &frame, Fmt::Limits limits) {
      using Joined = TxJoined<Entry, O, Facet>;
      using Combined = MT<Joined, Entry::MsgType, Facet, typename Entry::Discrim>;
      return TxFrame_::signedCheck<Combined>(frame, limits);
    };
    this->m_emit = [](TxFrame &frame, const TxBody &body,
	const TxConfig &config, TxContext context) {
      auto &extra = static_cast<const TxExtended &>(body);
      extra.m_base->emit(frame, config, context);
      extra.m_model.emit(frame.body(), extra.m_arena);
    };
    this->m_archive = [](Fmt::Sink &out, const TxBody &body) {
      auto &extra = static_cast<const TxExtended &>(body);
      if (extra.m_base->m_archive) extra.m_base->m_archive(out, *extra.m_base);
      else out << extra.m_base->body();
      extra.m_model.emit(out, extra.m_arena);
    };
    m_model.capture(source, m_arena);
    this->m_modelEmpty = m_base->empty() && m_model.empty();
  }
  Fmt::Error error() const { return m_arena.error; }
  uint64_t memory() const override {
    return ZmHeapAllocSize<sizeof(TxExtended)>{} + m_base->memory() +
      m_arena.memory() + m_model.memory();
  }
private:
  TxBody::Ref m_base;
  TxModel_::Arena m_arena;
  TxModel_::Model<O, Facet, ArchiveField> m_model;
};

template <typename Entry, typename O, typename Facet>
TxBody::Ref TxBody::extend(Ref base, const O &source,
    Fmt::Error &error, Fmt::Limits limits) {
  error = {};
  ZuAssert((ZuIsSame<Facet, typename Entry::Mapping>{}));
  if (!base || base->msgType() != ZuCSpan{Entry::MsgType}) {
    error = {Fmt::ErrorCode::Unresolved, 35}; return {};
  }
  ZuUnroll::all<typename Fmt::Schema<O, Facet>::Leaves>([]<typename L>() {
    using F = L::Field;
    ZuAssert(!sessionTag(Fmt::tag<F>()) && !Fmt::reserved(Fmt::tag<F>()));
  });
  ZmRef<TxExtended<Entry, O, Facet>> model =
    new TxExtended<Entry, O, Facet>{ZuMv(base), source, limits};
  if ((error = model->error())) return {};
  return model;
}

template <typename Entry>
TxBody::Ref TxBody::capture(const typename Entry::Object &object, uint64_t id,
    Fmt::Error &error, Fmt::Limits limits) {
  if (Session::type(Entry::MsgType)) {
    error = {Fmt::ErrorCode::Reserved, 35}; return {};
  }
  return captureRaw<Entry>(object, id, false, error, limits);
}

template <typename Entry>
TxBody::Ref TxBody::captureRaw(const typename Entry::Object &object, uint64_t id,
    bool admin, Fmt::Error &error, Fmt::Limits limits) {
  error = {};
  if (!Entry::MsgType.length()) {
    error = {Fmt::ErrorCode::Required, 35}; return {};
  }
  if (Entry::MsgType.length() > limits.fieldBytes ||
      Entry::MsgType.length() + 4 > limits.frameBytes) {
    error = {Fmt::ErrorCode::Size, 35}; return {};
  }
  ZmRef<TxCaptured<Entry>> model = new TxCaptured<Entry>{object, id, admin, limits};
  if ((error = model->error())) return {};
  return model;
}
} // Zfix

#endif /* ZfixTxCapture_HH */
