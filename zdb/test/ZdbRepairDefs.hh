//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZdbRepairDefs_HH
#define ZdbRepairDefs_HH

#include <zlib/Zdb.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiFBReflect.hh>

namespace zdbtest {

struct RepairRow {
  uint64_t id;
  ZtArray<float> reals;
  ZtArray<ZtString<>> names;
  ZtString<> stamp;
  float real;
  ZiIP ip;
  double wide;
  ZtArray<ZtString<>> strings;

  const auto &cstrings() const & { return names; }
  auto &cstrings() & { return names; }
  auto &&cstrings() && { return ZuMv(names); }
  void cstrings(ZfStruct_::CStringVec v) {
    names.length(0);
    names.size(v.length());
    v.all([this](const char *s) { new (names.push()) ZtString<>{s}; });
  }
};
ZfStruct(, (RepairRow, FB),
  (id,		(Ctor<0>, (Keys<0, 2>)),		UInt64),
  (reals,	(Ctor<1>, Mutable),		FloatVec),
  ((names, AliasFn, cstrings, cstrings), (Ctor<2>, Mutable), CStringVec),
  (stamp,	(Ctor<3>, Required, Keys<2>, Group<2>), String),
  (real,	(Ctor<4>, Keys<1>, Mutable, Deflt<1.5>), Float),
  (ip,		(Ctor<5>, Mutable),		UDT),
  (wide,	(Ctor<6>, Mutable),		Float),
  (strings,	(Ctor<7>, Mutable),		StringVec));
ZiFB::AsTable<"repair.Row"> ZiFB_Fmt(RepairRow *);
struct RepairDB : public ZdbSagaDB {
  ZdbTable<RepairRow> *rows = nullptr;
  ZmSemaphore *paused = nullptr;
  Zdb_::SagaCompleteFn resume;
  unsigned inserts = 0, updates = 0, deletes = 0;
  bool valid = true;
};

inline bool repairValues(const RepairRow &row, bool updated)
{
  return row.real == (updated ? -2.5f : 1.5f) &&
    row.reals.length() == 2 && row.reals[0] == (updated ? 3.5f : -0.0f) &&
    row.reals[1] == -7.25f && row.names.length() == 2 &&
    ZuCSpan{row.names[0]} == (updated ? "updated" : "initial") &&
    ZuCSpan{row.names[1]} == "a long CStringVec element retained in the store" &&
    row.stamp == "required immutable value";
}

ZmRef<ZdbAnySaga<RepairDB>> repairDef(uint64_t id = 0, bool fail = false);
Zdb_::SagaDef repairSagaDef();

} // zdbtest

#endif /* ZdbRepairDefs_HH */
