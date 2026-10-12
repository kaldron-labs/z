//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZFLATC_HEADER
#define ZFLATC_HEADER <zflatc-inc/flatcroot.hh>
#endif
#include ZFLATC_HEADER
#include ZFLATC_BUILTIN

#include <zlib/ZuTestUtil.hh>

using namespace ZuTestUtil;
using flatcdep::H0;
using flatcdep::O0;

ZuAssert((O0::Heap::HeapID{}() == "flatcdep.O0.heap"_z));
ZuAssert((O0::Base_::HeapID{}() == "flatcdep.O0.base"_z));
using Name = typename ZuTraits<O0::E0>::Elem;
ZuAssert((Name::HeapID{}() == "flatcdep.O0.e0"_z));

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  bool passed = true;
  {
    ZuTest(includes);
    ZuAssert((ZuIsSame<decltype(H0::amount), ZuFixed>{}));
    H0 row;
    row.amount = ZuFixed{12.25};
    row.axial.axes.push(3); row.axial.axes.push(6); row.axial.axes.push(9);
    row.child = new O0;
    row.child->value = 73;
    row.child->heap = "owned";
    row.child->e0.push("element");
    row.children.push(new O0);
    row.children[0]->value = 91;
    flatcdep::Point point;
    point.x = -19;
    point.axes.push(2); point.axes.push(4); point.axes.push(8);
    row.body.p<2>(ZuMv(point));
    using H = ZiFB::As<H0>::Handler<H0, ZuFacet::FB>;
    ZiFB::Buffer buf;
    constexpr unsigned Align = ZiFB::BufferAlign<H0>::N;
    ZiFB::ArrayStorage<Align> storage{buf};
    ZiFB::Builder<decltype(storage)> builder{storage, Align};
    auto bytes = builder.finish(H::save<ZfFieldFilter::All>(builder, row));
    bool valid = ZiFB::verify<H0, ZuFacet::FB>(bytes);
    ZuCheck(valid, passed = false);
    if (!valid) return 1;
    ZuPtr<H0> loaded = ZiFB::handler<H0, ZuFacet::FB>(bytes).alloc();
    buf.null();
    ZuCheck(loaded->amount == ZuFixed{12.25}, passed = false);
    ZuCheck(loaded->axial.axes.length() == 3 && loaded->axial.axes[2] == 9, passed = false);
    Zf::Bytes root;
    root.push(7); root.push(9);
    ZiFB::Buffer rootBuf;
    ZiFB::ArrayStorage<> rootStorage{rootBuf};
    ZiFB::Builder<decltype(rootStorage)> rootBuilder{rootStorage};
    auto rootBytes = rootBuilder.finish(ZiFB::save<ZuFacet::FB>(rootBuilder, root));
    bool rootOK = ZiFB::verify<Zf::Bytes, ZuFacet::FB>(rootBytes);
    ZuCheck(rootOK, passed = false);
    if (!rootOK) return 1;
    ZuPtr<Zf::Bytes> rootCopy = ZiFB::handler<Zf::Bytes, ZuFacet::FB>(rootBytes).alloc();
    rootBuf.null();
    ZuCheck(rootCopy->length() == 2 && (*rootCopy)[1] == 9, passed = false);
    ZuCheck(loaded->child->value == 73 && loaded->child->heap == "owned", passed = false);
    ZuCheck(loaded->child->e0.length() == 1 && loaded->child->e0[0] == "element", passed = false);
    ZuCheck(loaded->children.length() == 1 && loaded->children[0]->value == 91, passed = false);
    ZuCheck(loaded->body.type() == 2 && loaded->body.p<2>().x == -19, passed = false);
    ZuCheck(loaded->body.p<2>().axes.length() == 3 && loaded->body.p<2>().axes[2] == 8, passed = false);
  }
  return passed ? 0 : 1;
}
