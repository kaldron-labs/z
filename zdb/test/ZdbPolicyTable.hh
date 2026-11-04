//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Table-policy fixture access to actual store recovery, without changing Zdb's API.

#ifndef ZdbPolicyTable_HH
#define ZdbPolicyTable_HH

#include <zlib/Zdb.hh>

namespace zdbtest {

template <typename T> class PolicyTable;
template <typename T> T *ZdbTableModel(PolicyTable<T> *);

template <typename T>
class PolicyTable : public Zdb_::Table<T, PolicyTable<T>> {
  using Base = Zdb_::Table<T, PolicyTable<T>>;
public:
  using Base::Base;
  void recover(Zdb_::Shard shard, Zdb_::UN un, Zdb_::RowFn fn) {
    this->storeTbl()->recover(shard, un, ZuMv(fn));
  }
};

} // zdbtest

#endif /* ZdbPolicyTable_HH */
