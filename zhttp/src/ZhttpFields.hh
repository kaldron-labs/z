//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP wire-neutral field dispatch and semantics

#ifndef ZhttpFields_HH
#define ZhttpFields_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/Zu_aton.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>

#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>

namespace Zhttp {

struct ZhttpAPI DefltHdrCatalog {
  using List = ZuTypeList<>;
  static int nameMatch(ZuBSpan);
  static int valueMatch(unsigned, ZuBSpan);
  static int fieldMatch(ZuBSpan);
};

ZtEnumNS(ZhttpAPI, FieldSection, int8_t, Informational, Final, Trailers);
namespace FieldSection { enum { Invalid = -1 }; }

template <typename Rx, typename L>
bool bodyEach(Rx &rx, L &&l) {
  while (rx) {
    int64_t n = rx.consume(
      [](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
      l);
    if (ZuUnlikely(n <= 0)) return false;
  }
  return true;
}

template <typename Rx>
bool bodyDrain(Rx &rx) {
  return bodyEach(rx, [](ZuSpan<uint8_t>) { });
}


namespace Fields {

using String = ZtBArray<ZtArrayHeapID<"Zhttp.Fields.String">>;

ZhttpAPI bool forbidden(ZuBSpan);
ZhttpAPI int pseudo(ZuBSpan);
ZhttpAPI FieldClass classify(ZuBSpan);

struct FieldNames {
  using Pseudo = ZuStringTL<
    ":method", ":path", ":scheme", ":authority", ":protocol">;
  using Forbidden = ZuStringTL<
    "connection", "proxy-connection", "keep-alive", "transfer-encoding", "upgrade">;
  using Keys = ZuTypeConcat<Pseudo, Forbidden>;
};

template <typename Name, typename Names> struct HasFieldName;
template <typename Name, typename ...N>
struct HasFieldName<Name, ZuTypeList<N...>> :
  ZuBool<((Name{}() == N{}()) || ... || false)> { };

template <typename Catalog, typename Names = FieldNames::Keys> struct WireNames;
template <typename Catalog, typename ...N>
struct WireNames<Catalog, ZuTypeList<N...>> {
  using Configured = typename HeaderList<Catalog>::Keys;
  using Extra = ZuTypeConcat<ZuIf<HasFieldName<N, Configured>{},
    ZuTypeList<>, ZuTypeList<N>>...>;
  using Keys = ZuTypeConcat<Configured, Extra>;
};

template <typename Catalog>
int fieldMatch(ZuBSpan name) {
  static constexpr auto matcher = ZuMatcher<WireNames<Catalog>>();
  return matcher.exact(name);
}

template <typename ...N>
ZuSpan<const int8_t> wireExtraMap(ZuTypeList<N...>) {
  if constexpr (sizeof...(N)) {
    static constexpr int8_t map[]{int8_t(ZuTypeIndex<N, FieldNames::Keys>{})...};
    return map;
  } else return {};
}

ZhttpAPI FieldClass wireClass(unsigned);

template <typename HdrCatalog>
struct NameIDs { using Keys = HeaderList<HdrCatalog>::Keys; };
template <typename HdrCatalog, unsigned I>
struct ValueIDs {
  using Keys = HeaderList<HdrCatalog>::template Value<I>;
};

template <typename HdrCatalog>
int nameMatch(ZuBSpan key) {
  using Keys = HeaderList<HdrCatalog>::Keys;
  if constexpr (!Keys::N) return -1;
  else {
    static constexpr auto matcher = ZuMatcher<NameIDs<HdrCatalog>>();
    return matcher.exact(key);
  }
}

template <typename HdrCatalog>
int valueMatch(unsigned key, ZuBSpan value) {
  using List = HeaderList<HdrCatalog>;
  if constexpr (!List::N) return -1;
  else return ZuSwitch::dispatch<List::N>(key,
      [&value](auto I) -> int {
    using Values = List::template Value<I>;
    if constexpr (!Values::N) return -1;
    else {
      static constexpr auto matcher = ZuMatcher<ValueIDs<HdrCatalog, I>>();
      return matcher.exact(value);
    }
  });
}

template <typename Impl, typename = void>
struct HasRuntime : public ZuFalse { };
template <typename Impl>
struct HasRuntime<Impl,
  decltype(ZuDeclVal<Impl *>()->header(
    FieldSection::Final, ZuDeclVal<ZuBSpan>(),
    ZuDeclVal<ZuSpan<uint8_t>>()), void())> :
    public ZuTrue { };

template <
  typename Catalog, typename Header, typename Static, typename Unknown>
void dispatch(
  ZuBSpan key, ZuSpan<uint8_t> value,
  Header &&header, Static &&static_, Unknown &&unknown) {
  using List = HeaderList<Catalog>;
  using Keys = List::Keys;
  if constexpr (Keys::N) {
    auto i = Catalog::nameMatch(key);
    if (i < 0) {
      unknown(key, value);
      return;
    }
    ZuSwitch::dispatch<Keys::N>(
      i, [&value, &header, &static_](auto i) {
	static constexpr unsigned I = i;
	using Key = ZuType<I, Keys>;
	using KeyValues = List::template Value<I>;
	if constexpr (KeyValues::N) {
	  auto j = Catalog::valueMatch(I, value);
	  if (j >= 0) {
	    ZuSwitch::dispatch<KeyValues::N>(
	      j, [&static_](auto j) {
		static_(Key{}, ZuType<j, KeyValues>{});
	      });
	    return;
	  }
	}
	header(Key{}, value);
      });
  } else
    unknown(key, value);
}

} // namespace Fields

template <typename> struct FieldNameTypes;
template <typename ...Name>
struct FieldNameTypes<ZuTypeList<Name...>> {
  using T = ZuTypeList<ZuStringT<Name{}()>...>;
};

template <typename List, typename Seq = ZuMkSeq<List::N>>
struct CatalogValueCount;
template <typename List, unsigned ...I>
struct CatalogValueCount<List, ZuSeq<I...>> {
  enum { N = (List::template Value<I>::N + ... + 0) };
};

// Configuration-owned, immutable after publication. Duplicate declarations
// borrow the same classification entry; names/values borrow static ZuStringT
// storage. Protocol seeds and dispatch share this view across all sessions.
template <typename Catalog>
class CatalogData {
  using List = HeaderList<Catalog>;
  using Names = ZuTypeUnique<typename FieldNameTypes<typename List::Keys>::T>;
  using Classes = ZtArray<FieldClass, ZtArrayHeapID<"Zhttp.Catalog.Class">>;
  using Values = ZtArray<ZuBSpan, ZtArrayHeapID<"Zhttp.Catalog.Value">>;
  using Fields = ZtArray<FieldMeta, ZtArrayHeapID<"Zhttp.Catalog.Field">>;

public:
  CatalogData() :
    m_classes{classes_()}, m_values{values_()},
    m_fields{fields_(m_classes, m_values)},
    m_view{m_fields, Catalog::nameMatch, Catalog::valueMatch, Catalog::fieldMatch,
      Zhttp::Fields::wireExtraMap(typename Zhttp::Fields::WireNames<Catalog>::Extra{})} { }

  // Called during endpoint/catalog configuration before processing messages.
  static const CatalogView &configure() {
    return ZmSingleton<CatalogData>::instance()->m_view;
  }

private:
  static Classes classes_() {
    Classes data;
    data.size(Names::N);
    ZuUnroll::all<Names>([&data]<typename Name>() {
      auto name = ZuBSpan{Name{}()};
      bool forbidden = Zhttp::Fields::forbidden(name);
      int pseudo = Zhttp::Fields::pseudo(name);
      constexpr int policy =
	Name{}() == "alt-svc" ? FieldClass::AltSvc :
	Name{}() == "connection" ? FieldClass::Connection :
	Name{}() == "location" ? FieldClass::Location : FieldClass::None;
      new (data.push()) FieldClass{int8_t(pseudo), forbidden, int8_t(policy)};
    });
    return data;
  }

  static Values values_() {
    Values data;
    data.size(CatalogValueCount<List>::N);
    ZuUnroll::all<List::N>([&data](auto I) {
      using V = typename List::template Value<I>;
      ZuUnroll::all<V>([&data]<typename Value>() {
	data.push(ZuBSpan{Value{}()});
      });
    });
    return data;
  }

  static Fields fields_(const Classes &classes, const Values &values) {
    Fields data;
    data.size(List::N);
    unsigned offset = 0;
    ZuUnroll::all<List::N>([&data, &classes, &values, &offset](auto I) {
      using Name = typename List::template Key<I>;
      using V = typename List::template Value<I>;
      constexpr unsigned nameID = ZuTypeIndex<ZuStringT<Name{}()>, Names>{};
      ZuSpan<const ZuBSpan> declared;
      if constexpr (V::N) declared = {values.data() + offset, V::N};
      new (data.push()) FieldMeta{
	ZuBSpan{Name{}()}, declared, &classes[nameID]};
      offset += V::N;
    });
    return data;
  }

  const Classes	m_classes;
  const Values	m_values;
  const Fields	m_fields;
  const CatalogView m_view;
};


class Channel;

// Rx-owned. The typed session/operation owns ctx and its actual parser object.
// Completion does not destroy that storage; its owner destroys it after drain.
struct ParserOps {
  void (*init)(void *, void *);
  void (*bodyMax)(void *, uint64_t);
  void (*destroy)(void *);
  bool (*enable1xx)(const void *);
  bool (*operation)(void *, Method::T, Target &);
  void (*status)(void *, unsigned);
  void (*header)(void *, FieldSection::T, int, int,
    ZuBSpan, ZuSpan<uint8_t>);
  bool (*bodyInfo)(void *, Channel *, BodyType::T, uint64_t);
  bool (*body)(void *, Input &);
  void (*complete)(void *, Channel *, bool);
  void (*reset)(void *);
  void (*connectedInfo)(void *, void *, Channel *, const ConnectedInfo &);
  void (*disconnected)(void *, Channel *, bool);
};

struct ParserBinding {
  void *ctx;
  const ParserOps *ops;
  const CatalogView *catalog;
  Channel *channel = nullptr;

  void init(void *context, uint64_t max) {
    ops->init(ctx, context);
    ops->bodyMax(ctx, max);
  }
  void destroy() { ops->destroy(ctx); }
  void connectedInfo(void *owner, const ConnectedInfo &info) {
    if (ops->connectedInfo) ops->connectedInfo(ctx, owner, channel, info);
  }
  void disconnected(bool peer) {
    if (ops->disconnected) ops->disconnected(ctx, channel, peer);
  }
  bool enable1xx() const { return ops->enable1xx(ctx); }
  bool operation(Method::T method, Target &target) {
    return ops->operation(ctx, method, target);
  }
  void status(unsigned value) { ops->status(ctx, value); }
  void header(FieldSection::T section, int key,
      ZuBSpan name, ZuSpan<uint8_t> value) {
    int fixed = key < 0 ? -1 : catalog->valueMatch(key, value);
    ops->header(ctx, section, key, fixed, name, value);
  }
  bool bodyInfo(BodyType::T type, uint64_t length) {
    return ops->bodyInfo(ctx, channel, type, length);
  }
  bool body(Input &input) { return ops->body(ctx, input); }
  void complete(Channel *channel, bool ok) { ops->complete(ctx, channel, ok); }
  void reset() { ops->reset(ctx); }
};

// Small compiled dispatch adapter. The concrete wire machine supplies IDs from
// the catalog matcher; no second lookup or response-context search occurs here.
template <typename Parser>
void parserHeader(Parser &parser, FieldSection::T section,
    int key, int fixed, ZuBSpan name, ZuSpan<uint8_t> value) {
  using List = HeaderList<typename Parser::HdrCatalog>;
  if constexpr (List::N) {
    if (key >= 0) {
      ZuSwitch::dispatch<List::N>(key,
	[&parser, section, fixed, value](auto I) {
	  using Key = typename List::template Key<I>;
	  using Values = typename List::template Value<I>;
	  if constexpr (Values::N) {
	    if (fixed >= 0) {
	      ZuSwitch::dispatch<Values::N>(fixed, [&parser, section](auto J) {
		parser.template header<Key, ZuType<J, Values>>(section);
	      });
	      return;
	    }
	  }
	  parser.template header<Key>(section, value);
	});
      return;
    }
  }
  if constexpr (Fields::HasRuntime<Parser>{})
    parser.header(section, name, value);
}


} // namespace Zhttp

#define ZhttpHdrCatalogDerive(Name, Headers) \
  struct Name { \
    using List = Headers; \
    static int nameMatch(ZuBSpan); \
    static int valueMatch(unsigned, ZuBSpan); \
    static int fieldMatch(ZuBSpan); \
  }
#define ZhttpHdrCatalogImpl(Name) \
  int Name::nameMatch(ZuBSpan key) { \
    return Zhttp::Fields::nameMatch<Name>(key); \
  } \
  int Name::valueMatch(unsigned key, ZuBSpan value) { \
    return Zhttp::Fields::valueMatch<Name>(key, value); \
  } \
  int Name::fieldMatch(ZuBSpan key) { \
    return Zhttp::Fields::fieldMatch<Name>(key); \
  }

#endif /* ZhttpFields_HH */
