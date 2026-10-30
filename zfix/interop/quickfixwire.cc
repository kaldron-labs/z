//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <quickfix/config.h>
#include <quickfix/fix44/NewOrderSingle.h>
#include <quickfix/fix44/OrderCancelReplaceRequest.h>
#include <zlib/ZuBox.hh>
#include <zlib/ZuPtr.hh>
#include "quickfixwire.hh"

namespace QfLoad {

// SDK-owned groups use its pointer-adoption API; parent insertion must not
// clone an already-built subgroup tree. FieldMap's virtual destructor returns
// each concrete generated-group node to its identified pool.
template <typename T> struct Group : ZmNHeap<Group<T>>, T {
friend inline ZuStringT<"Zfix.Qf.Group"> ZmNHeapID(Group *);
};

ZuCSpan Arena::put(const std::string &value, int tag) {
  if (value.length() > m_bytes.length() - m_used) throw FIX::IncorrectDataFormat(tag);
  auto p = m_bytes.data() + m_used;
  memcpy(p, value.data(), value.length()); m_used += value.length();
  return {p, unsigned(value.length())};
}
static std::string string(ZuCSpan value) { return {value.data(), value.length()}; }
static void put(FIX::FieldMap &m, int tag, ZuCSpan value) {
  m.setField(FIX::StringField{tag, string(value)});
}
template <typename T> static void put(FIX::FieldMap &m, int tag, const T &value) {
  NumberText text;
  text << value; put(m, tag, ZuCSpan{text});
}
static void put(FIX::FieldMap &m, int tag, const ZuDateTime &value) {
  TimeText text;
  text << value.fmt(ZuDateTimeFmt::FIX<9>{}); put(m, tag, ZuCSpan{text});
}
static ZuCSpan field(const FIX::FieldMap &m, Arena &arena, int tag) {
  return arena.put(m.getField(tag), tag);
}
template <typename T> static T number(const FIX::FieldMap &m, int tag, bool required = true) {
  if (!required && !m.isSetField(tag)) return T{0};
  const auto &text = m.getField(tag);
  ZuBox<T> value;
  if (!text.length() ||
      value.scan(ZuCSpan{text.data(), unsigned(text.length())}) != text.length())
    throw FIX::IncorrectDataFormat(tag);
  return value.val();
}
static ZuDecimal decimal(const FIX::FieldMap &m, int tag, bool required = true) {
  if (!required && !m.isSetField(tag)) return ZuDecimal{0};
  const auto &text = m.getField(tag);
  ZuDecimal value;
  if (!text.length() ||
      value.scan(ZuCSpan{text.data(), unsigned(text.length())}) != text.length())
    throw FIX::IncorrectDataFormat(tag);
  return value;
}
static ZuDateTime time(const FIX::FieldMap &m, int tag) {
  const auto &text = m.getField(tag);
  ZuDateTime value{ZuDateTimeScan::FIX{}, ZuCSpan{text.data(), unsigned(text.length())}};
  if (!*value) throw FIX::IncorrectDataFormat(tag);
  return value;
}
static void extra(FIX::FieldMap &m, ZuCSpan text, ZuBSpan binary) {
  if (text.length()) put(m, 58, text);
  if (binary.length()) {
    put(m, 354, binary.length());
    put(m, 355, ZuCSpan{binary});
  }
}
template <typename T> static void extra(const FIX::FieldMap &m, Arena &arena, T &out) {
  if (m.isSetField(58)) out.text = field(m, arena, 58);
  if (m.isSetField(355)) {
    auto bytes = field(m, arena, 355);
    if (number<unsigned>(m, 354) != bytes.length()) throw FIX::IncorrectTagValue(354);
    out.encoded = bytes;
  }
}
template <typename MT> static void order(MT &m, const Order &o) {
  put(m, 11, o.key); put(m, 21, o.handling); put(m, 55, o.symbol);
  put(m, 54, o.side); put(m, 60, o.time); put(m, 38, o.qty);
  put(m, 40, o.type); put(m, 44, o.price); put(m, 453, o.parties.length());
  for (const auto &p : o.parties) {
    using Party = Group<typename MT::NoPartyIDs>;
    ZuPtr<Party> party{new Party};
    put(*party, 448, p.id); put(*party, 447, p.source); put(*party, 452, p.role);
    put(*party, 802, p.subs.length());
    for (const auto &s : p.subs) {
      using Sub = Group<typename MT::NoPartyIDs::NoPartySubIDs>;
      ZuPtr<Sub> sub{new Sub};
      put(*sub, 523, s.id); put(*sub, 803, s.type);
      // Counts are already encoded; disabling setCount leaves only SDK vector
      // insertion before ownership transfers, with its strong throw guarantee.
      party->addGroupPtr(802, sub.ptr(), false); ZuMv(sub).release();
    }
    m.addGroupPtr(453, party.ptr(), false); ZuMv(party).release();
  }
  extra(m, o.text, o.encoded);
}
static void order(const FIX::FieldMap &m, Arena &arena, Order &o) {
  o.key = number<uint64_t>(m, 11); o.handling = field(m, arena, 21);
  o.symbol = field(m, arena, 55); o.side = field(m, arena, 54); o.time = time(m, 60);
  o.qty = number<unsigned>(m, 38); o.type = field(m, arena, 40); o.price = decimal(m, 44);
  auto count = number<unsigned>(m, 453, false);
  if (m.groupCount(453) != count) throw FIX::IncorrectTagValue(453);
  o.parties.size(count);
  for (unsigned i = 0; i < count; ++i) {
    const auto &p = m.getGroupRef(i + 1, 453);
    Party party{field(p, arena, 448), field(p, arena, 447), number<unsigned>(p, 452), {}};
    auto count = number<unsigned>(p, 802, false);
    if (p.groupCount(802) != count) throw FIX::IncorrectTagValue(802);
    party.subs.size(count);
    for (unsigned j = 0; j < count; ++j) {
      const auto &s = p.getGroupRef(j + 1, 802);
      party.subs.push(SubParty{field(s, arena, 523), number<unsigned>(s, 803)});
    }
    o.parties.push(ZuMv(party));
  }
  extra(m, arena, o);
}
void encode(FIX::Message &m, const Order &o) {
  FIX44::NewOrderSingle typed; order(typed, o); m = ZuMv(typed);
}
void encode(FIX::Message &m, const Replace &r) {
  FIX44::OrderCancelReplaceRequest typed; order(typed, r.order);
  put(typed, 41, r.original); m = ZuMv(typed);
}
void encode(FIX::Message &m, const Cancel &c) {
  m.getHeader().setField(FIX::MsgType{"F"});
  put(m, 41, c.original); put(m, 11, c.key); put(m, 55, c.symbol);
  put(m, 54, c.side); put(m, 60, c.time); put(m, 38, c.qty);
}
void encode(FIX::Message &m, const Report &r) {
  m.getHeader().setField(FIX::MsgType{"8"});
  put(m, 37, r.order); put(m, 11, r.key); put(m, 17, r.exec);
  if (r.original) put(m, 41, r.original);
  put(m, 150, r.kind); put(m, 39, r.status); put(m, 55, r.symbol); put(m, 54, r.side);
  put(m, 38, r.qty); put(m, 44, r.price); put(m, 151, r.leaves); put(m, 14, r.cum);
  put(m, 6, r.avg); put(m, 32, r.lastQty); put(m, 31, r.lastPx); put(m, 60, r.time);
  extra(m, r.text, r.encoded);
}
void decode(const FIX::Message &m, Input<Order> &out) { order(m, out.arena, out.value); }
void decode(const FIX::Message &m, Input<Replace> &out) {
  out.value.original = number<uint64_t>(m, 41); order(m, out.arena, out.value.order);
}
void decode(const FIX::Message &m, Input<Cancel> &out) {
  auto &c = out.value;
  c.original = number<uint64_t>(m, 41); c.key = number<uint64_t>(m, 11);
  c.symbol = field(m, out.arena, 55); c.side = field(m, out.arena, 54);
  c.time = time(m, 60); c.qty = number<unsigned>(m, 38);
}
void decode(const FIX::Message &m, Input<Report> &out) {
  auto &r = out.value; auto &arena = out.arena;
  r.order = number<uint64_t>(m, 37); r.key = number<uint64_t>(m, 11);
  r.exec = number<uint64_t>(m, 17); r.original = number<uint64_t>(m, 41, false);
  r.kind = field(m, arena, 150); r.status = field(m, arena, 39);
  r.symbol = field(m, arena, 55); r.side = field(m, arena, 54);
  r.qty = number<unsigned>(m, 38, false); r.price = decimal(m, 44, false);
  r.leaves = number<unsigned>(m, 151); r.cum = number<unsigned>(m, 14); r.avg = decimal(m, 6);
  r.lastQty = number<unsigned>(m, 32, false); r.lastPx = decimal(m, 31, false);
  if (m.isSetField(60)) r.time = time(m, 60);
  extra(m, arena, r);
}
} // QfLoad
