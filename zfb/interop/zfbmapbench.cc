//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Functional preflight plus native map kernels. Canonical timing is deferred.

#include <stdlib.h>
#include <limits.h>
#include <iostream>

#include <flatbuffers/flatbuffer_builder.h>
#include "maps_generated.h"

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zfb.hh>

namespace MapBench {

struct Hash_ : public ZmNHeap<Hash_>,
  public ZmHashKV<int, int, ZmHashHeapID<"zfbmapbench.Hash">> {
friend inline ZuStringT<"zfbmapbench.Hash.Obj"> ZmNHeapID(Hash_ *);

};

ZuDerive(Hash, Hash_);
ZuDerive(Tree, (ZmRBTreeKV<int, int, ZmRBTreeHeapID<"zfbmapbench.Tree">>));
using Fmt = Zfb::AsMap<"maps.IntMap", ZfFieldTC::Int32, ZfFieldTC::Int32,
  ZuTypeList<>, ZuTypeList<ZuFieldProp::Deflt<-11>>>;
Fmt Zfb_Fmt(Hash *);
Fmt Zfb_Fmt(Tree *);

using Offsets = ZtArray<flatbuffers::Offset<maps::IntMapEntry>,
  ZtArrayHeapID<"zfbmapbench.Offsets">>;

static bool preflight(const Hash &hash, const Tree &tree, unsigned n,
  Zfb::Buffer &native)
{
  flatbuffers::FlatBufferBuilder canonical;
  Offsets offsets;
  offsets.length(n);
  for (unsigned i = 0; i < n; ++i)
    offsets[i] = maps::CreateIntMapEntry(canonical, i, i * 3);
  auto sorted = canonical.CreateVectorOfSortedTables(offsets.data(), n);
  canonical.Finish(maps::CreateIntMap(canonical, sorted));
  ZuBSpan other{canonical.GetBufferPointer(), canonical.GetSize()};
  if (!Zfb::verify<Tree>(other)) { std::cerr << "canonical native verify\n"; return false; }
  Tree loaded;
  Zfb::handler<Tree>(other).load(loaded);
  if (loaded.count_() != n) { std::cerr << "canonical count\n"; return false; }
  for (unsigned i = 0; i < n; ++i)
    if (loaded.findVal(int(i)) != int(i * 3)) { std::cerr << "canonical value\n"; return false; }

  if (Zfb::save(native, hash) != Zfb::Error::OK) { std::cerr << "native save\n"; return false; }
  flatbuffers::Verifier verifier{native.data(), native.length()};
  if (!verifier.VerifyBuffer<maps::IntMap>(nullptr)) { std::cerr << "native canonical verify\n"; return false; }
  auto entries = flatbuffers::GetRoot<maps::IntMap>(native.data())->entries();
  if (!entries || entries->size() != n) { std::cerr << "native count\n"; return false; }
  for (unsigned i = 0; i < n; ++i) {
    auto found = entries->LookupByKey(int(i));
    if (!found || found->value() != int(i * 3)) { std::cerr << "native lookup\n"; return false; }
  }
  Zfb::Buffer treeBytes;
  if (Zfb::save(treeBytes, tree) != Zfb::Error::OK) { std::cerr << "tree save\n"; return false; }
  if (!Zfb::verify<Tree>(treeBytes)) { std::cerr << "tree verify\n"; return false; }
  return true;
}

template <typename O>
static void save(const char *name, const O &object, unsigned n, unsigned rounds)
{
  Zfb::Buffer storage;
  Zfb::ArrayStorage<> arena{storage};
  Zfb::Builder<decltype(arena)> builder{arena};
  unsigned bytes = 0, capacity = 0, scratch = 0;
  auto start = Zm::now();
  for (unsigned i = 0; i < rounds; ++i) {
    builder.clear();
    auto image = builder.finish(Zfb::save(builder, object));
    if (builder.failed()) Zm::exit(1);
    bytes += image.length();
    capacity = builder.capacity();
    if (scratch < builder.head()) scratch = builder.head();
  }
  auto elapsed = Zm::now() - start;
  std::cout << name << ',' << n << ',' << rounds << ',' << elapsed.as_decimal()
    << ',' << bytes << ',' << capacity << ',' << scratch << ','
    << n * sizeof(Zfb::Offset) << ',' << bytes << '\n';
}

static void lookup(ZuBSpan bytes, unsigned n, unsigned rounds)
{
  auto entries = Zfb::handler<Tree>(bytes).entries();
  int64_t sum = 0;
  auto start = Zm::now();
  for (unsigned r = 0; r < rounds; ++r)
    for (unsigned i = 0; i < n; ++i)
      sum += entries.find(int(i)).template get<decltype(entries)::Entry::VF>();
  auto elapsed = Zm::now() - start;
  std::cout << "lookup," << n << ',' << rounds << ',' << elapsed.as_decimal()
    << ',' << bytes.length() << ",0,0,0," << sum << '\n';
}

static void materialize(ZuBSpan bytes, unsigned n, unsigned rounds, bool update)
{
  auto handler = Zfb::handler<Tree>(bytes);
  Tree dest;
  auto start = Zm::now();
  for (unsigned i = 0; i < rounds; ++i) {
    if (update) handler.update(dest);
    else handler.load(dest);
  }
  auto elapsed = Zm::now() - start;
  if (dest.count_() != n) Zm::exit(1);
  std::cout << (update ? "update" : "load") << ',' << n << ',' << rounds << ','
    << elapsed.as_decimal() << ',' << bytes.length() << ",0,0,0," << dest.count_()
    << '\n';
}

} // MapBench

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "usage: zfbmapbench entries rounds\n";
    return 1;
  }
  unsigned n = strtoul(argv[1], nullptr, 10);
  unsigned rounds = strtoul(argv[2], nullptr, 10);
  if (!rounds || n > INT_MAX / 3) return 1;
  ZmRef<MapBench::Hash> hash = new MapBench::Hash();
  MapBench::Tree tree;
  for (unsigned i = n; i; --i) {
    hash->add(int(i - 1), int((i - 1) * 3));
    tree.add(int(i - 1), int((i - 1) * 3));
  }
  Zfb::Buffer native;
  if (!MapBench::preflight(*hash, tree, n, native)) {
    std::cerr << "map preflight failed\n";
    return 1;
  }
  std::cout << "mode,entries,rounds,seconds,total_bytes,capacity,head,"
    << "offset_scratch_bytes,checksum\n";
  MapBench::save("save_hash", *hash, n, rounds);
  MapBench::save("save_tree", tree, n, rounds);
  MapBench::lookup(native, n, rounds);
  MapBench::materialize(native, n, rounds, false);
  MapBench::materialize(native, n, rounds, true);
}
