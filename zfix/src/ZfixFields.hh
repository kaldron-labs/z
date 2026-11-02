//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Reusable ordinary FIX session field declarations; no runtime aggregate

#ifndef ZfixFields_HH
#define ZfixFields_HH

#ifndef ZfixFmtLib_HH
#include <zlib/ZfixFmtLib.hh>
#endif

#include <zlib/ZfixFmt.hh>

namespace Zfix {

constexpr bool sessionTag(int32_t tag) {
  switch (tag) {
    case 49: case 56: case 50: case 57: case 142: case 143: case 1128:
    case 34: case 347: case 43: case 52: case 122: return true;
    default: return false;
  }
}

namespace Field_ {

template <int32_t> struct Session;
template <typename O, int32_t Tag, typename = void> struct Member {
  using T = typename Session<Tag>::T;
  static T get(const O &) { return {}; }
  static void set(O &, T) { }
};

// These are field declarations, reused in the actual message's ZuFields_ list.
// Optional native members retain normal object storage. Models which obtain
// session metadata through RequestContext need no duplicate per-object storage.
#define ZfixSessionField(Tag, Name, Code, Type) \
  template <> struct Session<Tag> { \
    using T = Type; \
    static constexpr auto &id() { return #Name; } \
    template <typename Base> using Field = ZfField_##Code<Base>; \
  }; \
  template <typename O> struct Member<O, Tag, \
      decltype(void(ZuDeclVal<O &>().Name))> { \
    using T = Type; \
    static T get(const O &o) { return T(o.Name); } \
    static void set(O &o, T value) { o.Name = ZuMv(value); } \
  }

ZfixSessionField(49, sender, String, ZuCSpan);
ZfixSessionField(56, target, String, ZuCSpan);
ZfixSessionField(50, senderSub, String, ZuCSpan);
ZfixSessionField(57, targetSub, String, ZuCSpan);
ZfixSessionField(142, senderLocation, String, ZuCSpan);
ZfixSessionField(143, targetLocation, String, ZuCSpan);
ZfixSessionField(1128, appVerID, String, ZuCSpan);
ZfixSessionField(34, seqNo, UInt64, uint64_t);
ZfixSessionField(347, encoding, String, ZuCSpan);
ZfixSessionField(43, possDup, Bool, bool);
ZfixSessionField(52, sendingTime, DateTime, ZuDateTime);
ZfixSessionField(122, origSendingTime, DateTime, ZuDateTime);

#undef ZfixSessionField

template <typename O_, int32_t Tag> struct Base {
  using O = O_;
  using T = typename Session<Tag>::T;
  using Props = ZuTypeList<ZuFieldProp::FIX::ID<Tag>,
    ZuIf<Tag == 49 || Tag == 56 || Tag == 34,
      ZuFieldProp::Required, ZuFieldProp::FIX::Opt>>;
  enum { ReadOnly = 0 };
  static constexpr auto &id() { return Session<Tag>::id(); }
  static T get(const O &o) { return Member<O, Tag>::get(o); }
  static void set(O &o, T value) { Member<O, Tag>::set(o, ZuMv(value)); }
};

} // Field_

template <typename O, int32_t Tag>
using SessionField = typename Field_::Session<Tag>::template Field<Field_::Base<O, Tag>>;

// This is an ordinary field typelist, not an injected Schema prefix. A message
// explicitly concatenates/reorders these field declarations with its own fields.
template <typename O, int32_t ...Tags>
using SessionFieldsT = ZuTypeList<SessionField<O, Tags>...>;
template <typename O>
using SessionFields = SessionFieldsT<O, 49, 56, 50, 57, 142, 143, 1128,
  34, 347, 43, 52, 122>;

template <typename F, bool = ZuFieldProp::HasValue<
  ZuFieldProp::FIX::Props<typename F::Props>, ZuFieldProp::FIX::ID>{}>
struct ArchiveField_ : Fmt::SaveField<F> { };
template <typename F> struct ArchiveField_<F, true> : ZuBool<
  Fmt::SaveField<F>{} && !sessionTag(Fmt::tag<F>())> { };
template <typename F> using ArchiveField = ArchiveField_<F>;

} // Zfix

namespace Zfix::Fmt {
template <> struct NestedFilter<ArchiveField> {
  template <typename F> using T = SaveField<F>;
};
}

#endif /* ZfixFields_HH */
