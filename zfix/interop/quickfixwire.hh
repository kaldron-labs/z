//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef quickfixwire_HH
#define quickfixwire_HH

#include <string>
#include <quickfix/config.h>
#include <quickfix/Message.h>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include "../util/load.hh"

#ifdef ZfixFmtLib_HH
#error Independent fixture must not use the native FIX codec
#endif
#ifdef ZfixLib_HH
#error Independent fixture must not use the native FIX session engine
#endif

namespace QfLoad {

using namespace ZfixLoad;
// Full scalar-domain/FIX<9> bounds, not expected-message capacities. Builtin
// storage avoids heap formatting and stack-address ownership inference.
using NumberText = ZtBuiltin<ZtArray<char, ZtArrayHeapID<"Zfix.Qf.Number">>,
  sizeof("-999999999999999999.999999999999999999") - 1>;
using TimeText = ZtBuiltin<ZtArray<char, ZtArrayHeapID<"Zfix.Qf.Time">>,
  sizeof("00000000-00:00:00.000000000") - 1>;
// One identified arena owns only selected field values across the SDK callback.
// Group containers retain their normal Z allocation IDs; no SDK message is copied.
class Arena {
public:
  explicit Arena(unsigned bytes) { m_bytes.length(bytes); }

  ZuCSpan put(const std::string &value, int tag);

  uint64_t bytes() const { return m_bytes.size(); }
private:
  Text		m_bytes;
  unsigned	m_used = 0;
};

template <typename T> struct Input : public ZmNHeap<Input<T>>, public ZmObject {
friend inline ZuStringT<"Zfix.Qf.Input"> ZmNHeapID(Input *);
  T	value;
  Arena	arena;

  explicit Input(unsigned bytes) : arena{bytes} { }
};

void encode(FIX::Message &, const Order &);
void encode(FIX::Message &, const Cancel &);
void encode(FIX::Message &, const Replace &);
void encode(FIX::Message &, const Report &);
void decode(const FIX::Message &, Input<Order> &);
void decode(const FIX::Message &, Input<Cancel> &);
void decode(const FIX::Message &, Input<Replace> &);
void decode(const FIX::Message &, Input<Report> &);
} // QfLoad

#endif /* quickfixwire_HH */
