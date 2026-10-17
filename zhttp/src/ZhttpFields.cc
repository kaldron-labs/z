//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP wire-neutral field matchers

#include <string.h>

#include <zlib/ZuMatcher.hh>

#include <zlib/ZhttpFields.hh>

#include "ZhttpNative.hh"
#include "ZhttpFieldImpl.hh"

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

namespace Native_ {

const WriterOps &FinalFields::writerOps_()
{
  static const WriterOps ops{
    [](void *ctx, ZuBSpan value) {
      static_cast<FinalFields *>(ctx)->append_(value);
    },
    [](void *ctx, const BufferPrint &value) {
      auto &self = *static_cast<FinalFields *>(ctx);
      auto span = self.reserve_(value.size);
      if (!self.m_failed)
	self.commit_(value.put(value.ctx,
	  reinterpret_cast<char *>(span.data()), span.length()));
    },
    [](void *ctx, ZmRef<ZiIOBuf> buf) {
      static_cast<FinalFields *>(ctx)->append_(buf->cspan());
    },
    [](void *ctx) { return !static_cast<FinalFields *>(ctx)->m_failed; },
    [](const void *ctx) { return static_cast<const FinalFields *>(ctx)->m_failed; },
    [](const void *ctx) -> uint64_t {
      auto &self = *static_cast<const FinalFields *>(ctx);
      return self.m_bytes ? self.m_bytes->length : 0;
    },
    [](const void *ctx) { return static_cast<const FinalFields *>(ctx)->m_limit; },
    [](const void *) { return 0U; },
    [](const void *) { return 0U; },
    [](void *, unsigned head) -> ZmRef<ZiIOBuf> {
      auto buf = ZmRef<ZiIOBuf>{new Alloc{}};
      buf->skip = head;
      return buf;
    },
    [](void *ctx, unsigned length) {
      return static_cast<FinalFields *>(ctx)->reserve_(length);
    },
    [](void *ctx, unsigned length) {
      static_cast<FinalFields *>(ctx)->commit_(length);
    }};
  return ops;
}

ZuSpan<uint8_t> FinalFields::reserve_(unsigned length)
{
  if (m_failed) return {};
  if (length > m_limit - m_cost) { m_failed = true; return {}; }
  if (!m_bytes) m_bytes = new Alloc{};
  auto data = m_bytes->ensure(m_bytes->length + length);
  if (!data) { m_failed = true; return {}; }
  m_reserved = length;
  return {data + m_bytes->length, length};
}

void FinalFields::commit_(unsigned length)
{
  if (m_failed) return;
  if (length > m_reserved) { m_failed = true; return; }
  m_reserved = 0;
  m_bytes->length += length;
  m_cost += length;
}

void FinalFields::append_(ZuBSpan value)
{
  auto span = reserve_(value.length());
  if (m_failed) return;
  if (value) memcpy(span.data(), value.data(), value.length());
  commit_(value.length());
}

void FinalFields::field_(const CatalogView &catalog, FieldForm::T form,
    int id, const FieldClass *classification,
    PrintView key, PrintView value, bool never)
{
  if (m_failed) return;
  // The same per-field overhead used by the HTTP header list limit bounds
  // descriptor storage even when providers supply empty values.
  if (m_limit - m_cost < 32) { m_failed = true; return; }
  m_cost += 32;
  unsigned name = m_bytes ? m_bytes->length : 0;
  Writer out{this, writerOps_()};
  key.write(out);
  if (!out.flush()) return;
  unsigned valueOffset = m_bytes->length;
  value.write(out);
  if (!out.flush()) return;
  unsigned valueLen = m_bytes->length - valueOffset;
  ZuBSpan nameSpan{m_bytes->data() + name, valueOffset - name};
  ZuSpan<uint8_t> valueSpan{m_bytes->data() + valueOffset, valueLen};
  Fields::Semantics<false> semantics;
  semantics.trailers(true);
  if (!semantics.field(catalog, nameSpan, valueSpan,
      [](int, ZuBSpan, ZuSpan<uint8_t>) { })) {
    m_failed = true;
    return;
  }
  auto fieldClass = classification ? *classification :
    catalog.match(nameSpan).classification;
  if (form == FieldForm::Mutable) form = FieldForm::Name;
  new (m_fields.push()) Field{name, unsigned(nameSpan.length()),
    valueOffset, valueLen, id, fieldClass, form, never};
}

bool FinalFields::render(unsigned limit,
    const CatalogView &catalog, FieldProvider &provider)
{
  m_fields.length(0);
  if (m_bytes) m_bytes->clear();
  m_limit = limit;
  m_cost = m_reserved = 0;
  m_failed = false;
  struct Context {
    FinalFields *self;
    const CatalogView *catalog;
  } context{this, &catalog};
  FieldOutput output{&context,
    [](void *ctx, FieldForm::T form, int id,
	const FieldClass *classification, PrintView key, PrintView value,
	bool never) -> FieldOffset {
      auto &context = *static_cast<Context *>(ctx);
      context.self->field_(*context.catalog, form, id, classification,
	key, value, never);
      return {};
    }};
  provider.fields(provider.ctx, output);
  return !m_failed;
}

void FinalFields::fields(FieldOutput &output) const
{
  ZmAssert(!m_failed);
  for (const auto &field : m_fields) {
    ZuBSpan name{m_bytes->data() + field.name, field.nameLen};
    ZuBSpan value{m_bytes->data() + field.value, field.valueLen};
    output.field(field.form, field.id, &field.classification,
      PrintView::borrow(name), PrintView::borrow(value), field.never);
  }
}

} // namespace Native_

} // namespace Zhttp
