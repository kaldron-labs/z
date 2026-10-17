//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP wire-neutral field matchers

#include <zlib/ZuMatcher.hh>

#include <zlib/ZhttpFields.hh>

namespace Zhttp {

int DefltHdrCatalog::nameMatch(ZuBSpan) { return -1; }
int DefltHdrCatalog::valueMatch(unsigned, ZuBSpan) { return -1; }
int DefltHdrCatalog::fieldMatch(ZuBSpan name) {
  return Fields::fieldMatch<DefltHdrCatalog>(name);
}

ZtEnumImplNS(FieldSection);

namespace Fields {

FieldClass wireClass(unsigned id)
{
  constexpr unsigned N = FieldNames::Pseudo::N;
  return {int8_t(id < N ? int(id) : -1), id >= N, FieldClass::None};
}

FieldClass classify(ZuBSpan name)
{
  int id = DefltHdrCatalog::fieldMatch(name);
  return id < 0 ? FieldClass{-1, false, FieldClass::None} : wireClass(unsigned(id));
}

bool forbidden(ZuBSpan name) { return classify(name).forbidden; }
int pseudo(ZuBSpan name) { return classify(name).pseudo; }


} // namespace Fields
FieldName CatalogView::match(ZuBSpan name) const
{
  int id = fieldMatch(name);
  if (id < 0) return {-1, {-1, false, FieldClass::None}};
  if (unsigned(id) < fields.length()) return {id, *fields[id].classification};
  return {-1, Fields::wireClass(unsigned(extra[unsigned(id) - fields.length()]))};
}

} // namespace Zhttp
