//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <string>

#include <zlib/ZuBox.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtHexDump.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include "zfbtest2_fbs.h"

namespace zfbtest2 {

using namespace Zfb;

struct Object {
  ZuID id;
  int price;
  ZuBitmap<100> flags;

  friend ZfStructPrint ZuPrintType(Object *);
};
struct Test {
  int foo = 42;
  ZtString<> bar;
  Object baz;

  friend ZfStructPrint ZuPrintType(Test *);
};

ZfbStruct(, Object,
  (((id), (Ctor<0>)),			String),
  (((price), (Ctor<1>)),		Int32),
  (((flags), (Ctor<2>, Deflt<"42"_z>)),	UDT));

ZfbStruct(, Test,
  (((foo), (Ctor<0>)),	Int32),
  (((bar), (Ctor<1>)),	String),
  (((baz), (Ctor<2>)),	UDT));

ZfbRoot(Test);

static auto vfields = ZfVFields<Test>();

} // zfbtest

using IOBuilder = Zfb::IOBuilder;
using IOBuf = ZiIOBuf;
std::vector<ZmRef<IOBuf>> bufs;

using namespace ZuTestUtil;

static bool passed = true;

template <bool Detach>
void build(IOBuilder &fbb, unsigned n)
{
  using namespace Zfb;
  ZmRef<IOBuf> buf;
  {
    zfbtest2::Test test{42, "Hello", {"id", 142, "1-3,5"}};
    fbb.Clear();
    fbb.Finish(ZfbStruct::save(fbb, test));
    fbb.PushElement(uint32_t(fbb.GetSize()));
    if constexpr (Detach) {
      buf = fbb.buf();
      bufs.push_back(buf);
      fbb.buf(new ZiIOBufAlloc<>());
    }
  }
  {
    uint8_t *ptr = Detach ? buf->data() : fbb.GetBufferPointer();
    int len = Detach ? buf->length : fbb.GetSize();

    uint32_t len_ = *reinterpret_cast<ZuLittleEndian<uint32_t> *>(ptr);
    auto test = zfbtest2::fbs::GetTest(ptr + 4);
    if (len_ != unsigned(len - 4)) passed = false;
    auto value = ZfbStruct::ctor<zfbtest2::Test>(test);
    if (value.foo != 42 || value.bar != "Hello" || value.baz.id != "id" ||
      value.baz.price != 142) passed = false;
    ZuPtr<zfbtest2::Test> allocated =
      ZfbStruct::alloc<zfbtest2::Test>(test);
    if (allocated->foo != 42 || allocated->bar != "Hello" ||
      allocated->baz.id != "id" || allocated->baz.price != 142)
      passed = false;
    ZuPtr<zfbtest2::Test> handled =
      ZfbStruct::Handler<zfbtest2::Test>{test}.alloc();
    if (handled->foo != 42 || handled->bar != "Hello" ||
      handled->baz.id != "id" || handled->baz.price != 142)
      passed = false;
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  auto fields = ZfVFields<zfbtest2::Object>();
  auto flags = static_cast<const ZuBitmap<100> *>(
    fields[ZuTypeIndex<zfbtest2::ZfField_Object_flags,
      ZuFields<zfbtest2::Object>>{}]->constant.get<ZfFieldTC::UDT>(
      ZfVField::cget(ZfVFieldConstant::Deflt)));
  ZuCheck(flags && flags->get(42) && !flags->get(41));
  unsigned n = 64;
  IOBuilder fbb(new ZiIOBufAlloc<>());
  build<false>(fbb, n);
  build<true>(fbb, n);
  build<true>(fbb, n);
  build<false>(fbb, n);
  build<true>(fbb, n);
  build<true>(fbb, n);

  ZuCHECK(passed, "serialized value round trips");
  ZuCHECK(zfbtest2::vfields.length() == 3, "field metadata");
  return 0;
}
