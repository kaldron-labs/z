//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmHash telemetry

#ifndef ZtcHash_HH
#define ZtcHash_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmFn_.hh>

namespace Ztc {

// display sequence:
//   id, addr, linear, bits, cBits, loadFactor, nodeSize,
//   count, effLoadFactor, resized
// derived display fields:
//   slots = 1<<bits
//   locks = 1<<cBits
struct HashTelemetry {
  ZuID		id;		// primary key
  uintptr_t	addr = 9;	// primary key
  double	loadFactor = 0.0; // (double)N / 16.0
  double	effLoadFactor = 0.0; // graphable (*)
  uint64_t	count = 0;	// graphable (*)
  uint32_t	nodeSize = 0;
  uint32_t	resized = 0;	// dynamic
  uint8_t	bits = 0;
  uint8_t	cBits = 0;
  uint8_t	linear = 0;
  uint8_t	shadow = 0;
};

// Note: ZtStruct metadata declaration is deferred

struct Hash {
  virtual ZuTuple<ZuID, uintptr_t> key() const = 0;
  virtual void telemetry(HashTelemetry &) const = 0;
};

struct HashMgr {
  using AllFn = ZmFn<void(Hash *), ZmFnHeapID<"Ztc.Hash.AllFn">>;

  static void all(AllFn);
};

// Hash CSV

template <class S> struct HashCSV_ {
  HashCSV_(S &stream) : m_stream(stream) { }
  void print() {
    m_stream <<
      "id,addr,shadow,linear,bits,cBits,loadFactor,nodeSize,"
      "count,effLoadFactor,resized\n";
    HashMgr::all({this, ZmFnPtr<&HashCSV_::print_>{}});
  }
  void print_(Hash *hash) {
    HashTelemetry data;
    hash->telemetry(data);
    m_stream
      << data.id << ','
      << ZuBoxPtr(data.addr).hex() << ','
      << unsigned(data.shadow) << ','
      << unsigned(data.linear) << ','
      << unsigned(data.bits) << ','
      << unsigned(data.cBits) << ','
      << ZuBoxed(data.loadFactor) << ','
      << data.nodeSize << ','
      << data.count << ','
      << ZuBoxed(data.effLoadFactor) << ','
      << data.resized << '\n';
  }

private:
  S	&m_stream;
};
struct HashCSV {
  template <typename S> void print(S &s) const {
    HashCSV_<S>(s).print();
  }
  friend ZuPrintFn ZuPrintType(HashCSV *);
};
static HashCSV hashCSV() { return HashCSV(); }

} // Ztc

#endif /* ZtcHash_HH */
