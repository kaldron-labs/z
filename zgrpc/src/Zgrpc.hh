//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Native gRPC contracts

#ifndef Zgrpc_HH
#define Zgrpc_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZuAssert.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/Zpb.hh>
#include <zlib/ZhttpFields.hh>

namespace Zgrpc {

ZtEnumNS(ZgrpcAPI, Kind, int8_t, Unary, ServerStream, ClientStream, Duplex);
ZtEnumNS(ZgrpcAPI, Code, int8_t,
  OK, CANCELLED, UNKNOWN, INVALID_ARGUMENT, DEADLINE_EXCEEDED,
  NOT_FOUND, ALREADY_EXISTS, PERMISSION_DENIED, RESOURCE_EXHAUSTED,
  FAILED_PRECONDITION, ABORTED, OUT_OF_RANGE, UNIMPLEMENTED, INTERNAL,
  UNAVAILABLE, DATA_LOSS, UNAUTHENTICATED);

// Match the existing endpoint defaults. Message bytes are bounded separately
// from serialized output and pending command counts.
namespace Default {
  enum {
    MaxMessageBytes = 16U << 20, MaxPending = 4096, MaxQueue = 1024,
    MaxQueueBytes = 64U << 20, WorkBatch = 64
  };
}

struct Limits {
  unsigned maxMessageBytes = Default::MaxMessageBytes;
  unsigned maxPending = Default::MaxPending;
  unsigned maxQueue = Default::MaxQueue;
  unsigned maxQueueBytes = Default::MaxQueueBytes;
  unsigned workBatch = Default::WorkBatch;
};

inline bool valid(const Limits &limits) {
  return limits.maxMessageBytes && limits.maxPending && limits.maxQueue &&
    limits.maxQueueBytes && limits.workBatch;
}

struct Method {
  using ReqFacet = ZuFacet::PB;
  using ResFacet = ZuFacet::PB;
  using ReqHeaders = ZuTypeList<>;
  using ResHeaders = ZuTypeList<>;
  using Trailers = ZuTypeList<>;
  enum {
    Kind = Zgrpc::Kind::Unary, MessageLimit = 0, ReqBorrowed = 0, ResBorrowed = 0
  };
};

ZuDerive(String, (ZtString<ZtStringHeapID<"Zgrpc.String">>));
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"Zgrpc.Bytes">>));

template <typename T, bool = ZuIsBase<T, ZmObject>{}>
struct ObjectValue_ { using T_ = T; };
template <typename T>
struct ObjectValue_<T, true> { using T_ = ZmRef<T>; };
template <typename T> using ObjectValue = ObjectValue_<T>::T_;

template <typename T>
struct Message {
  using Value = ObjectValue<T>;

  Value value;
  ZmRef<ZiIOBuf> backing;

  Message(Value value_, ZmRef<ZiIOBuf> backing_ = nullptr) :
    value{ZuMv(value_)}, backing{ZuMv(backing_)} { }

  T &object() {
    if constexpr (ZuIsBase<T, ZmObject>{}) return *value;
    else return value;
  }
  const T &object() const {
    if constexpr (ZuIsBase<T, ZmObject>{}) return *value;
    else return value;
  }
};

template <typename T> using Decoded = ZuUnion<void, Message<T>>;

ZtEnumNS(ZgrpcAPI, MetaError, int8_t,
  OK, Name, Reserved, Limit, Binary, Section, Duplicate, ContentType,
  Encoding, Timeout, Status, MissingStatus, MissingTE);

// A field stores offsets into its collection's owned arena. Selected names
// borrow compile-time literals; dynamic names are copied once per input field,
// including when a comma-joined binary field yields several values.
struct MetaField {
  ZuBSpan fixedName;
  unsigned namePos = 0;
  unsigned nameLen = 0;
  unsigned valuePos = 0;
  unsigned valueLen = 0;
  int key = -1;
  bool binary = false;
};

class ZgrpcAPI Metadata {
friend class MetaRx;
public:
  // Match the common HTTP field-section bound. The endpoint can pass its
  // effective HTTP header limit; RFC field overhead also bounds empty entries.
  explicit Metadata(unsigned limit = Zhttp::DefltMaxHeaderSection) : m_limit{limit} { }
  void reset(unsigned limit);
  bool add(ZuBSpan name, ZuBSpan value);
  bool receive(ZuBSpan name, ZuBSpan value);

  template <typename Key, int ID = -1>
  bool add(ZuBSpan value) { return add_(Key{}(), value, ID, true, false); }
  template <typename Key, int ID = -1>
  bool receive(ZuBSpan value) { return add_(Key{}(), value, ID, true, true); }

  ZuSpan<const MetaField> fields() const { return m_fields; }
  ZuBSpan name(const MetaField &field) const {
    return field.fixedName.length() ? field.fixedName :
      ZuBSpan{m_bytes.data() + field.namePos, field.nameLen};
  }
  ZuBSpan value(const MetaField &field) const {
    return field.valueLen ? ZuBSpan{m_bytes.data() + field.valuePos, field.valueLen} : ZuBSpan{};
  }
  bool failed() const { return m_error != MetaError::OK; }
  MetaError::T error() const { return m_error; }
  unsigned size() const { return m_size; }
  unsigned limit() const { return m_limit; }

private:
  bool add_(ZuBSpan, ZuBSpan, int, bool, bool, bool checked = false);
  bool fail(MetaError::T error) { m_error = error; return false; }

  using Fields = ZtArray<MetaField, ZtArrayHeapID<"Zgrpc.Metadata">>;
  Fields		m_fields;
  Bytes			m_bytes;
  unsigned		m_limit;
  unsigned		m_size = 0;
  MetaError::T		m_error = MetaError::OK;
};

struct Status {
  Code::T code = Code::OK;
  String message;
  Bytes details;
};

template <typename T>
struct Reply {
  Decoded<T> message;
  Status status;
  Metadata trailers;

  Reply() = default;
  Reply(Message<T> message_, Status status_ = {}, Metadata trailers_ = Metadata{}) :
    message{ZuMv(message_)}, status{ZuMv(status_)}, trailers{ZuMv(trailers_)} { }
  Reply(Status status_, Metadata trailers_ = Metadata{}) :
    status{ZuMv(status_)}, trailers{ZuMv(trailers_)} { }
};

// Protobuf identifiers: each component starts with a letter or underscore,
// then letters/digits/underscores. Services may carry a dotted package name.
template <typename S>
constexpr bool identifier(const S &value, bool qualified)
{
  bool first = true;
  for (unsigned i = 0, n = value.length(); i < n; i++) {
    auto c = value[i];
    if (qualified && c == '.' && !first) { first = true; continue; }
    bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    if (!alpha && (first || c < '0' || c > '9')) return false;
    first = false;
  }
  return !first;
}

template <typename M>
using MethodPath = ZuStringT<"/"_z + typename M::Service{}() + "/"_z + typename M::Name{}()>;

using ::Zpb_Fmt;
using Zpb::Zpb_Fmt;
template <typename T>
using PBFormat = decltype(Zpb_Fmt(ZuDeclVal<T *>()));
template <typename T, typename = void>
struct PBMessage : public ZuFalse { };
template <typename T>
struct PBMessage<T, decltype(PBFormat<T>::name(), void())> :
  public ZuBool<!PBFormat<T>::Repeated && !PBFormat<T>::Union> { };

using AppReserved = ZuStringTL<"content-type", "te", "content-length">;

template <typename Key>
consteval bool metaKeyValid()
{
  constexpr auto name = Key{}();
  constexpr unsigned n = name.length();
  if (!n || ZuTypeIn<Key, AppReserved>{}) return false;
  if constexpr (n >= 5)
    if (name[0] == 'g' && name[1] == 'r' && name[2] == 'p' && name[3] == 'c' && name[4] == '-')
      return false;
  for (unsigned i = 0; i < n; i++) {
    auto c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
    switch (c) {
      case '_': case '-': case '.': break;
      default: return false;
    }
  }
  return true;
}

template <typename ...Fields> struct MetaDecls : ZuFalse { };
template <> struct MetaDecls<> : ZuTrue { };
template <typename Key, typename Values, typename ...Rest>
struct MetaDecls<Key, Values, Rest...> :
  ZuBool<metaKeyValid<Key>() && Values::N <= 1 && MetaDecls<Rest...>{}> { };
template <typename Headers>
using MetaDeclsValid = ZuTypeApply<MetaDecls, Headers>;

template <typename M, typename = void>
struct MethodValid : public ZuFalse { };
template <typename M>
struct MethodValid<M, decltype(
  ZuDeclVal<typename M::Service *>(), ZuDeclVal<typename M::Name *>(),
  ZuDeclVal<typename M::Request *>(), ZuDeclVal<typename M::Response *>(), void())> {
  static constexpr bool value =
    identifier(typename M::Service{}(), true) && identifier(typename M::Name{}(), false) &&
    M::Kind >= Kind::Unary && M::Kind <= Kind::Duplex &&
    PBMessage<typename M::Request>{} &&
    PBMessage<typename M::Response>{} &&
    !Zpb::Config<typename M::ReqFacet>::SizePrefixed &&
    !Zpb::Config<typename M::ResFacet>::SizePrefixed &&
    MetaDeclsValid<typename M::ReqHeaders>{} &&
    MetaDeclsValid<typename M::ResHeaders>{} &&
    MetaDeclsValid<typename M::Trailers>{};
  constexpr bool operator ()() const { return value; }
  constexpr operator bool() const { return value; }
};

template <typename ...Methods>
constexpr bool methodsValid(ZuTypeList<Methods...> *) {
  return (MethodValid<Methods>{} && ...);
}
template <typename Methods>
using CatalogValid = ZuBool<methodsValid(static_cast<Methods *>(nullptr)) &&
  ZuTypeUnique<ZuTypeMap<MethodPath, Methods>>::N == Methods::N>;

template <typename M> using ReqHdrs = M::ReqHeaders;
template <typename M> using ResHdrs = M::ResHeaders;
template <typename M> using EndHdrs = M::Trailers;
template <typename Methods, template <typename> class Get>
using MergeHdrs = Zhttp::MergeHeaders<ZuTypeApply<ZuTypeConcat,
  ZuTypeMap<Get, Methods>>>;

template <typename Methods>
int methodMatch(ZuBSpan path)
{
  if constexpr (!Methods::N) return -1;
  else {
    struct IDs { using Keys = ZuTypeMap<MethodPath, Methods>; };
    static constexpr auto matcher = ZuMatcher<IDs>();
    return matcher.exact(path);
  }
}

} // Zgrpc

#define ZgrpcCatalogDerive(Name, Methods) \
  using Name##ReqHdrs = Zgrpc::MergeHdrs<Methods, Zgrpc::ReqHdrs>; \
  using Name##ResHdrs = Zgrpc::MergeHdrs<Methods, Zgrpc::ResHdrs>; \
  using Name##EndHdrs = Zgrpc::MergeHdrs<Methods, Zgrpc::EndHdrs>; \
  ZhttpHdrCatalogDerive(Name##ReqHeaders, Name##ReqHdrs); \
  ZhttpHdrCatalogDerive(Name##ResHeaders, Name##ResHdrs); \
  ZhttpHdrCatalogDerive(Name##Trailers, Name##EndHdrs); \
  struct Name { \
    using List = Methods; \
    using ReqHeaders = Name##ReqHeaders; \
    using ResHeaders = Name##ResHeaders; \
    using Trailers = Name##Trailers; \
    ZuAssert((Zgrpc::CatalogValid<Methods>{})); \
    static int methodMatch(ZuBSpan); \
  }
#define ZgrpcCatalogImpl(Name) \
  ZhttpHdrCatalogImpl(Name##ReqHeaders) \
  ZhttpHdrCatalogImpl(Name##ResHeaders) \
  ZhttpHdrCatalogImpl(Name##Trailers) \
  int Name::methodMatch(ZuBSpan path) { return Zgrpc::methodMatch<List>(path); }

#endif /* Zgrpc_HH */
