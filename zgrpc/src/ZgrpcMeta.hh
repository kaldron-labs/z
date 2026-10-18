//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// gRPC metadata wire utilities

#ifndef ZgrpcMeta_HH
#define ZgrpcMeta_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZuArray.hh>
#include <zlib/Zhttp.hh>
#include <zlib/Zgrpc.hh>

namespace Zgrpc {

// grpc-timeout has at most eight decimal digits and one unit byte.
using Timeout = ZuCArray<9>;
ZgrpcAPI Timeout encodeTimeout(int64_t nanos);
ZgrpcAPI bool decodeTimeout(ZuCSpan, int64_t &nanos);
ZgrpcAPI bool decodeCode(ZuCSpan, Code::T &);
ZgrpcAPI Code::T httpCode(unsigned);
ZgrpcAPI bool contentType(ZuCSpan);

ZgrpcAPI String encodeText(ZuBSpan);
// Malformed percent escapes are retained literally, as required by the wire
// protocol's resilient status-message decoding rule. '+' is always literal.
ZgrpcAPI String decodeText(ZuCSpan);

// Validate the whole value before invoking the permissive framework decoder.
// One value only: the metadata field collector splits comma-joined fields.
// Output remains unchanged on malformed input or size refusal.
ZgrpcAPI bool decodeBinary(ZuCSpan, Bytes &, unsigned limit);
ZgrpcAPI String encodeBinary(ZuBSpan);

// Borrowed printables render directly into Zhttp's final queued field storage.
struct BinaryValue;
struct ZgrpcAPI BinaryPrint : ZuPrintBuffer {
  static unsigned length(const BinaryValue &);
  static unsigned print(char *, unsigned, const BinaryValue &);
};
struct BinaryValue {
  ZuBSpan bytes;
  friend BinaryPrint ZuPrintType(BinaryValue *);
};
struct TextValue;
struct ZgrpcAPI TextPrint : ZuPrintBuffer {
  static unsigned length(const TextValue &);
  static unsigned print(char *, unsigned, const TextValue &);
};
struct TextValue {
  ZuBSpan bytes;
  friend TextPrint ZuPrintType(TextValue *);
};

// Structural field provider for initial sections and BodyEmitter::trailers.
// Fixed catalog defaults and dynamic values both emit, including equal names.
// The name-based policy uses the caller's ordinary catalog matching; it applies
// to matching fixed seeds as well as runtime values. Metadata remains borrowed
// only through synchronous field rendering.
template <typename Catalog = Zhttp::DefltHdrCatalog, typename Policy = Zhttp::Builder>
struct MetaFields : Zhttp::Builder {
  using HdrCatalog = Catalog;
  using Headers = Catalog::List;
  using Zhttp::Builder::header;

  const Metadata *metadata;
  const Policy *policy;

  explicit MetaFields(const Metadata &metadata_, const Policy *policy_ = nullptr) :
    metadata{&metadata_}, policy{policy_} { }
  bool neverIndex(ZuBSpan name) const { return policy && policy->neverIndex(name); }

  template <typename L> void header(L &&emit) const {
    for (const auto &field : metadata->fields()) {
      auto name = metadata->name(field), value = metadata->value(field);
      if (field.binary) emit(name, BinaryValue{value});
      else emit(name, value);
    }
  }
};

// One ID per protocol control, matched once for undeclared incoming names.
// Selected header callbacks can dispatch with a compile-time Protocol::ID.
struct Protocol {
  using Keys = ZuTypeConcat<AppReserved, ZuStringTL<"grpc-timeout", "grpc-status",
    "grpc-message", "grpc-status-details-bin", "grpc-encoding", "grpc-accept-encoding">>;
  enum { ContentType, TE, ContentLength, Timeout, Status, Message, Details,
    Encoding, AcceptEncoding, N };
  static ZgrpcAPI int match(ZuBSpan);
};

class ZgrpcAPI MetaRx {
public:
  explicit MetaRx(bool response = false, unsigned limit = Zhttp::DefltMaxHeaderSection) :
    m_initial{limit}, m_trailers{limit}, m_limit{limit}, m_response{response} { }
  void reset(bool response, unsigned limit);
  void httpStatus(unsigned status) { m_httpStatus = status; }
  unsigned httpStatus() const { return m_httpStatus; }
  bool header(Zhttp::FieldSection::T, ZuBSpan name, ZuBSpan value);

  template <typename Key, int ID = -1>
  bool header(Zhttp::FieldSection::T section, ZuBSpan value) {
    if constexpr (ZuTypeIn<Key, Protocol::Keys>{})
      return control_(section, ZuTypeIndex<Key, Protocol::Keys>{}, value);
    else
      return application_(section, Key{}(), value, true, ID);
  }

  // Called at accepted initial fields and normal HTTP receive completion,
  // respectively. Merely receiving grpc-status never certifies completion.
  bool initial();
  bool end();
  bool trailersOnly() const { return m_statusSection == Zhttp::FieldSection::Final; }
  bool finished() const { return m_finished; }
  bool failed() const { return m_error != MetaError::OK; }
  MetaError::T error() const { return m_error; }
  const Status &status() const { return m_status; }
  Status takeStatus() { return ZuMv(m_status); }
  Metadata takeMetadata() { return ZuMv(m_initial); }
  Metadata takeTrailers() { return ZuMv(m_trailers); }
  const Metadata &metadata() const { return m_initial; }
  const Metadata &trailers() const { return m_trailers; }
  int64_t timeout() const { return m_timeout; }
  bool timed() const { return m_seen & (1U<<Protocol::Timeout); }

private:
  bool section_(Zhttp::FieldSection::T);
  bool application_(Zhttp::FieldSection::T, ZuBSpan, ZuBSpan, bool, int = -1);
  bool control_(Zhttp::FieldSection::T, unsigned, ZuBSpan);
  bool fail(MetaError::T);

  using Section = Zhttp::FieldSection::T;
  Metadata		m_initial;
  Metadata		m_trailers;
  Status		m_status;
  int64_t		m_timeout = 0;
  unsigned		m_limit;
  unsigned		m_httpStatus = 0;
  unsigned		m_seen = 0;
  unsigned		m_controlBytes = 0;
  Section		m_terminalSection = Zhttp::FieldSection::Invalid;
  Section		m_statusSection = Zhttp::FieldSection::Invalid;
  MetaError::T		m_error = MetaError::OK;
  bool			m_response;
  bool			m_accepted = false;
  bool			m_trailing = false;
  bool			m_finished = false;
};

} // Zgrpc

#endif /* ZgrpcMeta_HH */
