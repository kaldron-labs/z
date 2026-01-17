//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <string>

#include <zlib/ZuBox.hh>
#include <zlib/ZuByteSwap.hh>

#include <zlib/ZtHexDump.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <flatbuffers/reflection.h>
#include <flatbuffers/reflection_generated.h>

#include "zfbtest_fbs.h"

namespace zfbtest {

using namespace Zfb;

struct Test {
  int foo = 42;
  ZtString<> bar;
  ZtArray<ZtString<>> baz;
  uint8_t *zero;
  unsigned n;

  friend ZtStructPrint ZuPrintType(Test *);
};

ZfbStruct(Test,
  (((foo), (Ctor<0>)), (Int32)),
  (((bar), (Ctor<1>)), (String, "bar")),
  (((baz), (Ctor<2>)), (StringVec)));

ZfbRoot(Test);

} // zfbtest

using IOBuilder = Zfb::IOBuilder;
using IOBuf = ZiIOBuf;
std::vector<ZmRef<IOBuf>> bufs;

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

template <bool Detach>
void build(IOBuilder &fbb, unsigned n)
{
  using namespace Zfb;
  ZmRef<IOBuf> buf;
  {
    zfbtest::Test test{42, "Hello", {"hello", "world", "42"}};
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
    CHECK((fooField->type()->base_type(), reflection::Int));

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
      << "ptr=" << ZuBoxPtr(ptr).hex() << " len=" << len
      << " len_=" << len_ << " type_=" << type_ << '\n' << std::flush;
    std::cout << ZtHexDump(ZtString<>{} << ZuBoxPtr(ptr).hex(), ptr, len);
    auto test = zfbtest::fbs::GetTest(ptr + 8);
    std::cout << ZfbStruct::ctor<zfbtest::Test>(test) << '\n' << std::flush;
  }
}

int main(int argc, char **argv)
{
  if (argc != 2) { std::cerr << "usage N\n" << std::flush; exit(1); }
  unsigned n = ZuBox<unsigned>(argv[1]);
  IOBuilder fbb(new ZiIOBufAlloc<>());
  build<false>(fbb, n);
  build<true>(fbb, n);
  build<true>(fbb, n);
  build<false>(fbb, n);
  build<true>(fbb, n);
  build<true>(fbb, n);
}
