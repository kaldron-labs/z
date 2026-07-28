//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZhttpPut_HH
#define ZhttpPut_HH

#include <zlib/ZfJSON.hh>

namespace ZhttpPut {

ZuDerive(String, ZtString<ZtStringHeapID<"Zhttp.Put.String">>);

struct Record {
  uint64_t	id = 0;
  String	text;
};

ZfStruct((Record, JSON),
  (((id)),	(UInt64)),
  (((text)),	(String)));

inline bool equals(const Record &a, const Record &b)
{
  return a.id == b.id && a.text == b.text;
}

inline bool load(Record &record, String &json)
{
  auto scan = ZfJSON::scan(json);
  if (scan.template p<0>() < 0) return false;
  record = ZfJSON::handler<Record>(scan.template p<1>()).ctor();
  return true;
}

} // namespace ZhttpPut

#endif /* ZhttpPut_HH */
