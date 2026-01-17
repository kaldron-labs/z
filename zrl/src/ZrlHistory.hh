//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface - in-memory history

#ifndef ZrlHistory_HH
#define ZrlHistory_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZtWindow.hh>

#include <zlib/ZrlApp.hh>

namespace Zrl {

// in-memory history with maximum number of entries
ZuDerive(HistoryLine, (ZtArray<uint8_t, ZtArrayHeapID<"Zrl.History">>));
struct ZrlAPI History : public ZtWindow<HistoryLine> {
  using Base = ZtWindow<HistoryLine>;

public:
  History() = default;
  History(const History &) = default;
  History &operator =(const History &) = default;
  History(History &&) = default;
  History &operator =(History &&) = default;

  History(unsigned max) : Base{max} { }

  void save(unsigned i, ZuBSpan s);
  bool load(unsigned i, HistFn) const;

  auto saveFn() { return HistSaveFn{this, ZmFnPtr<&History::save>{}}; }
  auto loadFn() { return HistLoadFn{this, ZmFnPtr<&History::load>{}}; }
};

} // Zrl

#endif /* ZrlHistory_HH */
