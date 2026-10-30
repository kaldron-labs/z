//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixFmt.hh>
#include <zlib/ZfixFrame.hh>

// This consumer deliberately has neither session headers nor session linkage.
#if defined(ZfixLib_HH) || defined(ZiLib_HH) || defined(ZtcpLib_HH)
#error standalone FIX codec acquired a session or networking dependency
#endif

using namespace ZuTestUtil;

ZuStructFacet(PrivateFIX);

struct Scalar {
  int	value = 0;
};

ZfStruct(, (Scalar, FIX),
  (value, (FIX::ID<4200>),	Int32));
ZfStructRender(, Scalar, PrivateFIX,
  (value, FIX::ID<1000000>));
ZfStructImpl(Scalar);
ZfStructImpl(Scalar, FIX);
ZfStructImpl(Scalar, PrivateFIX);

struct Entries : public ZtArray<Scalar> {
  ZuDerive_(Entries, ZtArray<Scalar>);
friend Zfix::Fmt::AsArray<> Zfix_Fmt(Entries *);
};

struct Group {
  Entries	entries;
};

ZfStruct(, (Group, FIX),
  (entries, (FIX::ID<2000>),	UDT));
ZfStructImpl(Group);
ZfStructImpl(Group, FIX);

struct Groups : public ZtArray<Group> {
  ZuDerive_(Groups, ZtArray<Group>);
friend Zfix::Fmt::AsArray<> Zfix_Fmt(Groups *);
};

struct Nested {
  Groups	groups;
};

ZfStruct(, (Nested, FIX),
  (groups, (FIX::ID<2001>),	UDT));
ZfStructImpl(Nested);
ZfStructImpl(Nested, FIX);

struct Choice : public ZuUnion<Scalar, Group> {
  ZuDerive_(Choice, (ZuUnion<Scalar, Group>));
friend Zfix::Fmt::AsUnion Zfix_Fmt(Choice *);
};

struct Alternative {
  Choice	choice;
};

ZfStruct(, (Alternative, FIX),
  (choice, ,	UDT));
ZfStructImpl(Alternative);
ZfStructImpl(Alternative, FIX);

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuCheck(ZfixFmtLib[0] == '@');
  ZuCheck((ZuFieldProp::GetValue<ZuType<0, ZuFields<Scalar, ZuFacet::FIX>>::Props,
    ZuFieldProp::FIX::ID>{} == 4200));
  ZuCheck((ZuFieldProp::GetValue<ZuType<0, ZuFields<Scalar, ZuFacet::PrivateFIX>>::Props,
    ZuFieldProp::FIX::ID>{} == 1000000));
  ZuCheck((ZuIsSame<decltype(Zfix_Fmt(ZuDeclVal<Entries *>())),
    Zfix::Fmt::AsArray<>>{}));
  ZuCheck((ZuIsSame<decltype(Zfix_Fmt(ZuDeclVal<Choice *>())),
    Zfix::Fmt::AsUnion>{}));
  return 0;
}
