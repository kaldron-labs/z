//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <string>

#include <zlib/ZuBox.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtHexDump.hh>
#include <zlib/ZtArray.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <flatbuffers/reflection.h>
#include <flatbuffers/reflection_generated.h>

#include "zfbtest_fbs.h"

namespace zfbtest {

using namespace Zfb;

struct Elem {
  int bah = 42;

  friend ZfStructPrint ZuPrintType(Elem *);
};

ZuDerive(ElemVec, (ZtArray<Elem>));

ZfbStruct(, Elem,
  (((bah), (Ctor<0>)), (Int32)));

} // zfbtest

namespace ZfbTransform {

struct ElemVec {
  enum { IsInline = 0 };
  using Vec = Zfb::Vector<Zfb::Offset<zfbtest::fbs::Elem>>;

  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder>
  static auto save(Builder &fbb, const zfbtest::ElemVec &a) {
    return Zfb::Save::vectorIter<zfbtest::fbs::Elem>(fbb, a.length(),
      [&a](Builder &fbb, uint64_t i) mutable {
	return ZfbStruct::save(fbb, a[i]);
      });
  }

  template <typename O = zfbtest::ElemVec>
  static O load(const Vec *v) {
    O a;
    if (!v) return a;
    for (uint64_t i = 0; i < v->size(); i++)
      a.push(ZfbStruct::ctor<zfbtest::Elem>(v->Get(i)));
    return a;
  }
};

} // ZfbTransform

namespace zfbtest {

ZfbTransform::ElemVec ZfbTransformer_(ElemVec *);

struct Test {
  int foo = 42;
  ZtString<> bar;
  ZtArray<ZtString<>> baz;
  ElemVec elems;
  uint8_t *zero;
  unsigned n;

  friend ZfStructPrint ZuPrintType(Test *);
};

ZfbStruct(, Test,
  (((foo), (Ctor<0>)), (Int32)),
  (((bar), (Ctor<1>)), (String, "bar")),
  (((baz), (Ctor<2>)), (StringVec)),
  (((elems), (Ctor<3>)), (UDT)));

ZfbRoot(Test);
ZfbStructImpl(Elem);
ZfbStructImpl(Test);

} // zfbtest

using IOBuilder = Zfb::IOBuilder;
using IOBuf = ZiIOBuf;
std::vector<ZmRef<IOBuf>> bufs;

using namespace ZuTestUtil;

static bool passed = true;
#define CHECK(x) do { if (!(x)) passed = false; } while (0)

template <bool Detach>
void build(IOBuilder &fbb, unsigned n)
{
  using namespace Zfb;
  ZmRef<IOBuf> buf;
  {
    zfbtest::Test test{42, "Hello", {"hello", "world", "42"}};
    test.elems.push(zfbtest::Elem{43});
    test.elems.push(zfbtest::Elem{44});
    test.zero = reinterpret_cast<uint8_t *>(::malloc(n));
    test.n = n;
    memset(test.zero, 0, test.n);
    fbb.Clear();
    fbb.Finish(ZfbStruct::save(fbb, test));
    fbb.PushElement(uint32_t(42));
    fbb.PushElement(uint32_t(fbb.GetSize()));
    ::free(test.zero);
    if constexpr (Detach) {
      buf = fbb.buf();
      bufs.push_back(buf);
      fbb.buf(new ZiIOBufAlloc<>());
    }
  }
  {
    auto schema = reflection::GetSchema(ZfbSchema<zfbtest::Test>::data());
    auto rootTbl = schema->root_table();
    auto fields = rootTbl->fields();
    auto fooField = fields->LookupByKey("foo");
    CHECK((fooField != nullptr));
    CHECK((fooField->type()->base_type() == reflection::Int));

    uint8_t *ptr = Detach ? buf->data() : fbb.GetBufferPointer();
    unsigned len = Detach ? buf->length : fbb.GetSize();
    ZuBSpan data = {ptr, len};
    data.offset(8); // skip header

    CHECK((Verify(*schema, *rootTbl, data.data(), data.length())));
    auto root = GetAnyRoot(data.data());
    auto foo = GetFieldI<int32_t>(*root, *fooField);
    CHECK(foo == 42);
  }
  {
    using namespace Load;

    uint8_t *ptr = Detach ? buf->data() : fbb.GetBufferPointer();
    unsigned len = Detach ? buf->length : fbb.GetSize();
    uint32_t len_ = *reinterpret_cast<ZuLittleEndian<uint32_t> *>(ptr);
    uint32_t type_ = *reinterpret_cast<ZuLittleEndian<uint32_t> *>(ptr + 4);
    std::cout
      << "# ptr=" << ZuBoxPtr(ptr).hex() << " len=" << len
      << " len_=" << len_ << " type_=" << type_ << '\n' << std::flush;
    auto test = zfbtest::fbs::GetTest(ptr + 8);
    CHECK(test->foo() == 42);
    auto value = ZfbStruct::ctor<zfbtest::Test>(test);
    CHECK(value.elems.length() == 2);
    CHECK(value.elems[0].bah == 43);
    CHECK(value.elems[1].bah == 44);
  }
}

void frontReserve()
{
  using namespace Zfb;
  ZmRef<ZiIOBuf> buf_ = new ZiIOBufAlloc<>;
  buf_->skip = IOBuilder::Align;
  IOBuilder fbb{ZuMv(buf_)};
  zfbtest::Test test{42, "front", {"reserve"}};
  fbb.Finish(ZfbStruct::save(fbb, test));
  auto body = fbb.GetBufferPointer();
  auto length = fbb.GetSize();
  auto buf = fbb.buf();
  CHECK(buf->data() == body);
  CHECK(buf->length == length);
  CHECK(buf->skip >= 4);
  auto hdr = buf->prepend(4);
  CHECK(hdr != nullptr);
  CHECK(buf->data() + 4 == body);
  flatbuffers::Verifier verifier{body, length};
  CHECK(zfbtest::fbs::VerifyTestBuffer(verifier));

  enum { GrowthSize = 4096 };
  ZtString<> large;
  large.length(GrowthSize);
  memset(large.data(), 'x', GrowthSize);
  ZmRef<ZiIOBuf> storage =
    new ZiIOBufAlloc<64, 1U<<20, "Zfb.Test.FrontGrow">;
  auto identity = storage.ptr();
  storage->skip = IOBuilder::Align;
  IOBuilder growing{storage};
  zfbtest::Test largeTest{42, large, {}};
  growing.Finish(ZfbStruct::save(growing, largeTest));
  body = growing.GetBufferPointer();
  length = growing.GetSize();
  auto grown = growing.buf();
  CHECK(grown.ptr() == identity);
  CHECK(grown->data() == body);
  CHECK(grown->skip >= 4);
  hdr = grown->prepend(4);
  CHECK(hdr != nullptr);
  CHECK(grown->data() + 4 == body);
  flatbuffers::Verifier grownVerifier{body, length};
  CHECK(zfbtest::fbs::VerifyTestBuffer(grownVerifier));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  unsigned n = 64;
  IOBuilder fbb(new ZiIOBufAlloc<>());
  CHECK((ZfVFieldMatcher<zfbtest::Test>()("bar") ==
	ZuTypeIndex<zfbtest::ZfField_Test_bar,
	  ZuFields<zfbtest::Test>>{}));
  CHECK(ZfVFieldMatcher<zfbtest::Test>()("missing") < 0);
  build<false>(fbb, n);
  build<true>(fbb, n);
  build<true>(fbb, n);
  build<false>(fbb, n);
  build<true>(fbb, n);
  build<true>(fbb, n);
  frontReserve();
  ZuCHECK(passed, "reflection and buffer round trips");
  return 0;
}
