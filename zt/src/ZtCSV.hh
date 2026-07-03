//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZtStruct CSV parser/generator
// - streaming functional-style API (can handle very large datasets)
// - column binding with ZtStruct

#ifndef ZtCSV_HH
#define ZtCSV_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtStruct.hh>
#include <zlib/ZtBytesFmt.hh>

ZuStructFacet(CSV); // canonical CSV facet, others can be defined

namespace ZtCSV {

// bytes format
using namespace ZtBytesFmt;

} // ZtCSV

namespace ZuFieldProp::CSV { // ZuStruct field properties

template <ZuString ID_> struct ID { }; // defaults to field ID
template <uint8_t I> struct BytesFmt { };

// shorthand
using Base64 = BytesFmt<ZtBytesFmt::Base64>;
using Base64URL = BytesFmt<ZtBytesFmt::Base64URL>;
using Base32 = BytesFmt<ZtBytesFmt::Base32>;
using Hex = BytesFmt<ZtBytesFmt::Hex>;
using Raw = BytesFmt<ZtBytesFmt::Raw>;

// GetID<Field> - ZuStringT
// - gets the JSON-specific ID for the field
// - the Field is passed because the value defaults to Field::id()
template <
  typename Field,
  bool = HasValue<typename Field::Props, ID>{}>
struct GetID_ {
  using T = ZuStringT<Field::id()>;
};
template <typename Field>
struct GetID_<Field, true> {
  using T = GetValue<typename Field::Props, ID>;
};
template <typename Field>
using GetID = typename GetID_<Field>::T;

// obtain a consteval typelist of field IDs suitable for use with ZuMatcher_<>
template <typename> struct GetIDs_;
template <typename ...Field>
struct GetIDs_<ZuTypeList<Field...>> {
  using T = ZuTypeList<GetID<Field>...>;
};
template <typename O>
struct GetIDs_ : public GetIDs_<ZuFields<O>> { };
template <typename U>
using GetIDs = typename GetIDs_<U>::T;

// GetBytesFmt - ZuConstant<uint8_t>
template <typename Props, bool = HasValue<Props, BytesFmt>{}>
struct GetBytesFmt_ {
  using T = ZuConstant<uint8_t, ZtCSV::Base64>; // default
};
template <typename Props>
struct GetBytesFmt_<Props, true> {
  using T = GetValue<Props, BytesFmt>;
};
template <typename Props>
using GetBytesFmt = typename GetBytesFmt_<Props>::T;

} // ZuFieldProp::CSV

namespace ZtCSV {

// CSV-specific formatting
struct Fmt : public ZtFmt::Default {
  static ZuDateTimeScan::CSV &DateScan_() {
    return ZmTLS([]{ return ZuDateTimeScan::CSV{}; });
  }
  static ZuDateTimeFmt::CSV &DatePrint_() {
    return ZmTLS([]{ return ZuDateTimeFmt::CSV{}; });
  }
  static constexpr ZuCSpan FlagsDelim() { return "|"; }

  // vector formatting
  static constexpr ZuCSpan VecPrefix() { return "={"; }
  static constexpr ZuCSpan VecDelim() { return ";"; }
  static constexpr ZuCSpan VecSuffix() { return "}"; }
};

namespace Quote {

using namespace ZtQuote;

// C string quoting (CSV style)
struct CString {
  const char *v;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const CString &print) {
    const char *v = print.v;
    s << '"';
    if (v) {
      char c;
      for (unsigned i = 0; c = v[i]; i++) {
	if (ZuUnlikely(c == '"')) s << '"';
	s << c;
      }
    }
    return s << '"';
  }
};

// string quoting (CSV style)
struct String {
  ZuCSpan v;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const String &print) {
    const auto &v = print.v;
    s << '"';
    for (unsigned i = 0, n = v.length(); i < n; i++) {
      char c = v[i];
      if (ZuUnlikely(c == '"')) s << '"';
      s << c;
    }
    return s << '"';
  }
};

} // Quote

// save/load handler for string-formatted types "..."
// - custom handler skeleton:
// struct Fmt {
//   template <typename O>
//   struct Handler {
//     template <typename S>
//     static void save(S &s, const O &o) { ...; }
//     static O load(ZuCSpan span) { return O{...}; }
//   }
// };
// class A {
//   ...
//   friend inline Fmt ZtCSV_StringFmt(A *); // bind Fmt to A
// };

ZuDerive(QuoteBuf, // temporary on-stack string buffer for quoting
  (ZtString<
    ZtStringBuiltin<128,
      ZtStringHeapID<"ZtCSV.Quote",
	ZtStringSharded<true>>>>));

struct AsStringDeflt {	// default string formatter
  template <typename O>
  struct Handler {
    template <typename S>
    ZuInline static void save(S &s, const O &o) {
      QuoteBuf buf;
      buf << o;
      s << Quote::String{buf.span()};
    }
    ZuInline static O load(ZuCSpan span) { // string is already unquoted
      return O(span);
    }
  };
};

} // ZtCSV

ZtCSV::AsStringDeflt ZtCSV_StringFmt(...);

namespace ZtCSV {

// --- output functions

template <
  unsigned TypeCode, typename Props,
  typename S, typename T_>
inline void saveValue_(S &s, const T_ &v_)
{
  using T = ZuDecay<T_>;
  if constexpr (TypeCode == ZtFieldTC::CString)
    s << Quote::CString{v_};
  else if constexpr (TypeCode == ZtFieldTC::String)
    s << Quote::String{v_};
  else if constexpr (TypeCode == ZtFieldTC::Bytes) {
    constexpr unsigned Fmt = ZuFieldProp::CSV::GetBytesFmt<Props>{};
    if constexpr (Fmt == ZtCSV::Base64) {
      ZuBSpan v{v_};
      auto n = ZuBase64::enclen(v.length());
      auto buf_ = ZmAlloc(uint8_t, n);
      ZuSpan<uint8_t> buf(&buf_[0], n);
      buf.trunc(ZuBase64::encode(buf, v));
      s << ZuCSpan(buf);
    } else if constexpr (Fmt == ZtCSV::Base64URL) {
      ZuBSpan v{v_};
      auto n = ZuBase64URL::enclen(v.length());
      auto buf_ = ZmAlloc(uint8_t, n);
      ZuSpan<uint8_t> buf(&buf_[0], n);
      buf.trunc(ZuBase64URL::encode(buf, v));
      s << ZuCSpan(buf);
    } else if constexpr (Fmt == ZtCSV::Base32) {
      ZuBSpan v{v_};
      auto n = ZuBase32::enclen(v.length());
      auto buf_ = ZmAlloc(uint8_t, n);
      ZuSpan<uint8_t> buf(&buf_[0], n);
      buf.trunc(ZuBase32::encode(buf, v));
      s << ZuCSpan(buf);
    } else if constexpr (Fmt == ZtCSV::Hex) {
      ZuBSpan v{v_};
      auto n = ZuHex::enclen(v.length());
      auto buf_ = ZmAlloc(uint8_t, n);
      ZuSpan<uint8_t> buf(&buf_[0], n);
      buf.trunc(ZuHex::encode(buf, v));
      s << ZuCSpan(buf);
    } else if constexpr (Fmt == ZtCSV::Raw) {
      s << Quote::String{v_};
    }
  } else if constexpr (TypeCode == ZtFieldTC::Bool) {
    bool v = v_;
    s << (v ? '1' : '0');
  } else if constexpr (
      TypeCode == ZtFieldTC::Int8 ||
      TypeCode == ZtFieldTC::UInt8 ||
      TypeCode == ZtFieldTC::Int16 ||
      TypeCode == ZtFieldTC::UInt16 ||
      TypeCode == ZtFieldTC::Int32 ||
      TypeCode == ZtFieldTC::UInt32 ||
      TypeCode == ZtFieldTC::Int64 ||
      TypeCode == ZtFieldTC::UInt64 ||
      TypeCode == ZtFieldTC::Int128 ||
      TypeCode == ZtFieldTC::UInt128) {
    using B = ZuBox<ZtFieldTC::Type<TypeCode>>;
    auto v = B{v_};
    if (!*v) return;
    if constexpr (
	bool(ZuFieldProp::HasEnum<Props>{}) ||
	bool(ZuFieldProp::HasFlags<Props>{})) {
      QuoteBuf buf;
      buf << ZtFieldPrintInt<Props, Fmt, typename B::T>(v);
      s << Quote::String{buf.span()};
    } else
      s << ZtFieldPrintInt<Props, Fmt, typename B::T>(v);
  } else if constexpr (TypeCode == ZtFieldTC::Float) {
    double v = v_;
    if (ZuUnlikely(ZuNull(v))) return;
    bool negative = v < 0;
    if (negative) v = -v;
    if (ZuUnlikely(v == ZuCmp<double>::inf())) {
      s << "#NUM!";
      return;
    }
    if (negative) s << '-';
    int e = 0;
    if (ZuUnlikely(v < 1.0e-9 || v >= 1.0e18)) {
      e = log10(v);
      if (e < 0) --e;
      v = v * pow(10, -e);
      if (v >= 10.0) { v /= 10.0; ++e; }
    }
    s << ZuBoxed(v).fmt<Fmt>();
    if (e) { s << 'E'; if (e > 0) s << '+'; s << e; }
  } else if constexpr (TypeCode == ZtFieldTC::Fixed) {
    ZuFixed v = v_;
    if (ZuUnlikely(!*v)) return;
    s << v.fmt<Fmt>();
  } else if constexpr (TypeCode == ZtFieldTC::Decimal) {
    ZuDecimal v = v_;
    if (ZuUnlikely(!*v)) return;
    s << v.fmt<Fmt>();
  } else if constexpr (
      TypeCode == ZtFieldTC::Time ||
      TypeCode == ZtFieldTC::DateTime) {
    ZuDateTime v{v_};
    if (!*v) return;
    auto &fmt = ZmTLS<ZuDateTimeFmt::CSV, (int Fmt::*){}>();
    s << v.fmt(fmt);
  } else if constexpr (TypeCode == ZtFieldTC::UDT) {
    using Fmt = decltype(ZtCSV_StringFmt(ZuDeclVal<T *>()));
    using Handler = typename Fmt::template Handler<T>;
    Handler::save(s, v_);
  }
}

template <
  unsigned TypeCode, typename Props,
  typename S, typename T_>
inline void saveValue(S &s, const T_ &v)
{
  using T = ZuDecay<T_>;
  if constexpr (!ZtFieldTC::IsVec<TypeCode>{}) {
    saveValue_<TypeCode, Props>(s, v);
  } else {
    unsigned n = ZuTraits<T>::length(v);
    enum { ElemCode = ZtFieldTC::Elem<TypeCode>{} };
    s << Fmt::VecPrefix();
    for (unsigned i = 0; i < n; i++) {
      if (i) s << Fmt::VecDelim();
      saveValue_<ElemCode, Props>(s, v[i]);
    }
    s << Fmt::VecSuffix();
  }
}

template <typename Field, typename S, typename O>
inline void saveField(S &s, const O &o)
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  saveValue<TypeCode, Props>(s, Field::get(o));
}

// --- input functions

ZuInline constexpr bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

// returns the number of columns in a CSV header line
// - returns -1 if the line is incomplete
ZtExtern int scan(ZuSpan<char> span);

// array of spans within a CSV header
ZuDerive(Header, (ZtArray<ZuSpan<char>, ZtArrayHeapID<"ZtCSV.Header">>));
// array of spans within a single cell (yes, Excel has that capability)
ZuDerive(ArrayCell, (ZtArray<ZuSpan<char>, ZtArrayHeapID<"ZtCSV.ArrayCell">>));
// individual cell (either a single value span, or an array value)
ZuDerive(Cell, (ZuUnion<ZuSpan<char>, ArrayCell>));
// array of spans within a CSV row (body line)
ZuDerive(Row, (ZtArray<Cell, ZtArrayHeapID<"ZtCSV.Row">>));
// field ID -> column index (used for reading)
// - each row is split into a Row
// - each object is loaded from the Row as indexed by Lookup
template <typename Fields>
struct Lookup : ZuArray<int, Fields::N> {
  using Base = ZuArray<int, Fields::N>;
  using Base::Base;
  using Base::operator =;

  unsigned ncols;

  Lookup(const Header &header) : ncols(header.length()) {
    this->length(Fields::N);
    for (unsigned i = 0; i < Fields::N; i++) (*this)[i] = -1;
    constexpr auto matcher = ZuMatcher<ZuFieldProp::CSV::GetIDs<Fields>>();
    for (unsigned i = 0; i < ncols; i++) {
      auto j = matcher.match(header[i]);
      if (j >= 0) (*this)[j] = i;
    }
  }
};

// splits a header line into comma-separated spans
// - fills header with scanned spans
// - returns +ve offset to the next line if a full line was scanned
// - returns -1 if the line is incomplete
ZtExtern int split(ZuSpan<char> span, Header &header);
// splits a body line into comma-separated spans
// - fills row with scanned spans
// - splits array cells into elements
// - returns +ve offset to the next row if a full line was scanned
// - returns -1 if the line is incomplete
ZtExtern int split(ZuSpan<char> span, Row &row);
// idempotently unquote a span
// - un-quotes strings in-place, mutating the span
// - uses the byte following the span (the , or \n delimiter) as
//   a guaranteed null-terminator and an idempotence check
ZtExtern void unquote(ZuSpan<char> &span);

template <unsigned TypeCode, typename Props, typename T>
inline T loadValue_(ZuSpan<char> span)
{
  unquote(span); // idempotent, null-terminates
  if constexpr (TypeCode == ZtFieldTC::CString) {
    return T(&span[0]);
  } else if constexpr (TypeCode == ZtFieldTC::String) {
    return T(span);
  } else if constexpr (TypeCode == ZtFieldTC::Bytes) {
    unsigned n = span.length();
    if (ZuUnlikely(!n)) return ZuCmp<T>::null();
    ZuSpan<uint8_t> bytes(span);
    constexpr unsigned Fmt = ZuFieldProp::CSV::GetBytesFmt<Props>{};
    // encodings are idempotently decoded in-place
    // - zero-fill trailing bytes are used for idempotence
    // - the final trailing byte is used to stash the number of
    //   padding bytes from the original base32/64 encoding
    if constexpr (Fmt == ZtCSV::Base64) {
      unsigned m = ZuBase64::declen(n), l;
      if (bytes[n - 1] >= 4) {
	l = ZuBase64::decode({&bytes[0], m}, bytes);
	memset(&bytes[m], 0, (n - 1) - l);
	bytes[n - 1] = m - l; // guaranteed to be < 4
      } else {
	l = m - bytes[n - 1];
      }
      bytes.trunc(l);
      return T(bytes);
    } else if constexpr (Fmt == ZtCSV::Base64URL) {
      unsigned m = ZuBase64URL::declen(n), l;
      if (bytes[n - 1] >= 4) {
	l = ZuBase64URL::decode({&bytes[0], m}, bytes);
	memset(&bytes[m], 0, (n - 1) - l);
	bytes[n - 1] = m - l; // guaranteed to be < 4
      } else {
	l = m - bytes[n - 1];
      }
      bytes.trunc(l);
      return T(bytes);
    } else if constexpr (Fmt == ZtCSV::Base32) {
      unsigned m = ZuBase32::declen(n), l;
      if (bytes[n - 1] >= 8) {
	l = ZuBase32::decode({&bytes[0], m}, bytes);
	memset(&bytes[m], 0, (n - 1) - l);
	bytes[n - 1] = m - l; // guaranteed to be < 8
      } else {
	l = m - bytes[n - 1];
      }
      bytes.trunc(l);
      return T(bytes);
    } else if constexpr (Fmt == ZtCSV::Hex) {
      unsigned m = ZuHex::declen(n);
      if (bytes[n - 1]) {
	m = ZuHex::decode({const_cast<uint8_t *>(&bytes[0]), m}, bytes);
	memset(const_cast<uint8_t *>(&bytes[m]), 0, n - m);
      }
      bytes.trunc(m);
      return T(bytes);
    } else if constexpr (Fmt == ZtCSV::Raw) {
      return T(bytes);
    }
  } else if constexpr (TypeCode == ZtFieldTC::Bool) {
    return T(ZuBox<uint8_t>{span}.val());
  } else if constexpr (
      TypeCode == ZtFieldTC::Int8 ||
      TypeCode == ZtFieldTC::Int16 ||
      TypeCode == ZtFieldTC::Int32 ||
      TypeCode == ZtFieldTC::Int64 ||
      TypeCode == ZtFieldTC::Int128 ||
      TypeCode == ZtFieldTC::UInt8 ||
      TypeCode == ZtFieldTC::UInt16 ||
      TypeCode == ZtFieldTC::UInt32 ||
      TypeCode == ZtFieldTC::UInt64 ||
      TypeCode == ZtFieldTC::UInt128) {
    using Scan = ZtFieldScanInt<Props, Fmt, ZtFieldTC::Type<TypeCode>>;
    return T(Scan{span}.value.val());
  } else if constexpr (TypeCode == ZtFieldTC::Float) {
    return ZuBox<double>{span}.val();
  } else if constexpr (
      TypeCode == ZtFieldTC::Fixed ||
      TypeCode == ZtFieldTC::Decimal) {
    ZuDecimal d{span};
    if constexpr (TypeCode == ZtFieldTC::Decimal)
      return d;
    else {
      if (!*d) return ZuFixed{};
      if constexpr (ZuFieldProp::HasNDP<Props>{})
	return ZuFixed{d, ZuFieldProp::GetNDP<Props>{}};
      else
	return ZuFixed{d};
    }
  } else if constexpr (
      TypeCode == ZtFieldTC::Time ||
      TypeCode == ZtFieldTC::DateTime) {
    auto &fmt = ZmTLS<ZuDateTimeScan::CSV, (int Fmt::*){}>();
    ZuDateTime v;
    if (!v.scan(fmt, span)) return ZuCmp<T>::null();
    if constexpr (ZuIs_<T, ZuTime>{})
      return v.as_time();
    else
      return v;
  } else if constexpr (TypeCode == ZtFieldTC::UDT) {
    using Fmt = decltype(ZtCSV_StringFmt(ZuDeclVal<T *>()));
    using Handler = typename Fmt::template Handler<T>;
    return Handler::load(span);
  }
}

// LoadVec wraps an array of sub-spans, parsing each sub-span on demand
template <unsigned TypeCode, typename Props, typename T>
struct LoadVec :
  public ZuMArray<LoadVec<TypeCode, Props, T>, ArrayCell, T>
{
  ZuDerive_(LoadVec, (ZuMArray<LoadVec, ArrayCell, T>));
  using Base::underlying;
  T get(unsigned i) const & {
    return loadValue<TypeCode, Props, T>(underlying[i]);
  }
  template <typename V> void set(unsigned, const V &) { } // unused
};

template <unsigned TypeCode, typename Props, typename T>
inline auto loadValue(Cell &cell)
{
  if constexpr (!ZtFieldTC::IsVec<TypeCode>{}) {
    if (cell.is<ZuSpan<char>>())
      return loadValue_<TypeCode, Props, T>(
	cell.p<ZuSpan<char>>());
    else
      return loadValue_<TypeCode, Props, T>(
	cell.p<ArrayCell>()[0]);
  } else {
    // coerce cell to an array
    if (cell.is<ZuSpan<char>>())
      cell = ArrayCell{cell.p<ZuSpan<char>>()};
    enum { ElemCode = ZtFieldTC::Elem<TypeCode>{} };
    using Elem = ZtFieldTC::Type<ElemCode>;
    using LoadVec_ = LoadVec<ElemCode, Props, Elem>;
    return LoadVec_(cell.p<ArrayCell>());
  }
}

using Columns = ZuSpan<unsigned>;

template <typename O_, typename Facet>
struct Writer {
  using O = O_;

  // CSVs are used for reporting and data export, so permit all fields
  // to be written including synthetic fields
  using AllFields = ZuFields<O, Facet>;

  ZuArray<unsigned, AllFields::N>	columns;

  Writer() {
    columns.length(AllFields::N);
    for (unsigned i = 0; i < AllFields::N; i++) columns[i] = i;
  }
  Writer(Columns columns_) {
    unsigned n = columns_.length();
    if (ZuUnlikely(n > AllFields::N)) n = AllFields::N;
    columns.length(n);
    for (unsigned i = 0; i < n; i++) columns[i] = columns_[i];
  }

  template <typename S>
  void saveHdr(S &s) const {
    for (unsigned i = 0, n = columns.length(); i < n; i++) {
      ZuSwitch::dispatch<AllFields::N>(columns[i], [&s, i](auto I) {
	if (i) s << ',';
	using Field = ZuType<I, AllFields>;
	ZuCSpan fieldID = ZuFieldProp::CSV::GetID<Field>{}().cspan();
	s << fieldID;
      });
    }
    s << '\n';
  }

  // Match the same column set and adopt the header order for appends.
  bool matchHdr(Header &header) {
    unsigned n = columns.length();
    if (header.length() != n) return false;

    ZuArray<uint8_t, AllFields::N> matched;
    matched.length(n);
    for (unsigned i = 0; i < n; i++) matched[i] = 0;
    ZuArray<unsigned, AllFields::N> columns_;
    columns_.length(n);

    for (unsigned i = 0; i < n; i++) {
      ZuSpan<char> hdr = header[i];
      unquote(hdr);
      int found = -1;
      for (unsigned j = 0; j < n; j++) {
	ZuSwitch::dispatch<AllFields::N>(columns[j], [&hdr, &found, j](auto I) {
	  using Field = ZuType<I, AllFields>;
	  ZuCSpan fieldID = ZuFieldProp::CSV::GetID<Field>{}().cspan();
	  if (hdr == fieldID) found = j;
	});
	if (found >= 0) break;
      }
      if (found < 0 || matched[found]) return false;
      matched[found] = 1;
      columns_[i] = columns[found];
    }

    for (unsigned i = 0; i < n; i++) columns[i] = columns_[i];
    return true;
  }

  template <typename S>
  void save(S &s, const O &o) const {
    for (unsigned i = 0, n = columns.length(); i < n; i++) {
      ZuSwitch::dispatch<AllFields::N>(columns[i], [&s, &o, i](auto I) {
	if (i) s << ',';
	using Field = ZuType<I, AllFields>;
	saveField<Field>(s, o);
      });
    }
    s << '\n';
  }
};

// there are two styles of write interface - push and pull
// - push write(output) takes a single parameter
//   and relies on the app repeatedly calling the returned writer;
// - the returned writer evaluates as true in a bool context if ok;
//   writer.overflow is true if the maximum row length was exceeded
// - pull write(output, fn) repeatedly calls
//   fn(auto save) -> bool, where save is a void(const O &) lambda
//   that should be called by the app at most once to write the next
//   row; fn should return true to continue, false to stop

template <
  typename O,
  typename Facet,
  unsigned MaxRowLen,
  typename Out>
struct Push : public Writer<O, Facet> {
  using Base = Writer<O, Facet>;

  Out		&out;
  bool		overflow = false;	// maximum row length exceeded

private:
  void writeHeader() {
    auto buf_ = ZmAlloc(char, MaxRowLen);
    ZuStream buf{&buf_[0], MaxRowLen};
    this->saveHdr(buf);
    if (ZuUnlikely(buf.overflow())) {
      overflow = true;
      return;
    }
    auto n = &buf[0] - &buf_[0];
    out << ZuCSpan(&buf[0], n);
  }

  Push(Out &out_) : out{out_} { writeHeader(); }
  Push(Columns columns, Out &out_) : Base{columns}, out{out_} {
    writeHeader();
  }
  Push(Push &&) = default;
  Push &operator =(Push &&) = default;

  bool operator ()(const O &o) {
    if (ZuUnlikely(overflow)) return false;
    auto buf_ = ZmAlloc(char, MaxRowLen);
    ZuStream buf{&buf_[0], MaxRowLen};
    this->save(buf, o);
    if (ZuUnlikely(buf.overflow())) {
      overflow = true;
      return false;
    }
    auto n = &buf[0] - &buf_[0];
    out << ZuCSpan(&buf[0], n);
    return true;
  }

  bool operator !() { return overflow; }
  ZuOpBool
};

template <
  typename O,
  typename Facet = ZuFacet::CSV,
  unsigned MaxRowLen = 4096,
  typename Out>
inline auto write(Out &out) {
  return Push<O, Facet, MaxRowLen, Out>{out};
}
template <
  typename O,
  typename Facet = ZuFacet::CSV,
  unsigned MaxRowLen = 4096,
  typename Out>
inline auto write(Columns columns, Out &out) {
  return Push<O, Facet, MaxRowLen, Out>{columns, out};
}

template <
  typename O,
  typename Facet,
  unsigned MaxRowLen>
struct Pull : public Writer<O, Facet> {
  using Base = Writer<O, Facet>;

  bool		overflow = false;

private:
  template <typename Out, typename L>
  void write(Out &out, char *buf_, L l) {
    {
      ZuStream buf{&buf_[0], MaxRowLen};
      this->saveHdr(buf);
      if (ZuUnlikely(buf.overflow())) { overflow = true; return; }
      out << ZuCSpan(&buf[0], &buf[0] - buf_);
    }
    for (;;) {
      ZuStream buf{&buf_[0], MaxRowLen};
      if (!l([this, &buf](const O &o) { this->save(buf, o); })) return;
      if (ZuUnlikely(buf.overflow())) { overflow = true; return; }
      out << ZuCSpan(&buf[0], &buf[0] - buf_);
    }
  }

public:
  template <typename Out, typename L>
  Pull(Out &out, char *buf_, L l) { write(out, buf_, ZuMv(l)); }
  template <typename Out, typename L>
  Pull(Columns columns, Out &out, char *buf_, L l) : Base{columns} {
    write(out, buf_, ZuMv(l));
  }

  bool operator !() { return overflow; }
  ZuOpBool
};

// lambda l is of the form:
// [...](auto l) -> bool {
//   ...;			// next object
//   if (!EOF) l(o);		// write object
//   return EOF;		// return true if no more data
// }
template <
  typename O,
  typename Facet = ZuFacet::CSV,
  unsigned MaxRowLen = 4096,
  typename Out, typename L>
inline ZuUnion<void, bool> write(Out &out, L l) {
  auto buf_ = ZmAlloc(char, MaxRowLen);
  Pull pull(out, &buf_[0], ZuMv(l));
  if (pull) return {};
  return pull.overflow;
}

template <
  typename O,
  typename Facet = ZuFacet::CSV,
  unsigned MaxRowLen = 4096,
  typename Out, typename L>
inline ZuUnion<void, bool> write(Columns columns, Out &out, L l) {
  auto buf_ = ZmAlloc(char, MaxRowLen);
  Pull pull(columns, out, &buf_[0], ZuMv(l));
  if (pull) return {};
  return pull.overflow;
}

template <typename O_, typename Facet = ZuFacet::CSV>
struct Reader {
  using O = O_;

  template <typename Field>
  using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;

  using AllFields = ZuFields<O, Facet>;
  using LoadFields = ZuTypeGrep<ZtFieldFilter::Load, AllFields>;
  using SaveFields = ZuTypeGrep<ZtFieldFilter::Save, AllFields>;
  using CtorFields_ = ZuTypeGrep<ZtFieldFilter::Ctor, AllFields>;
  using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
  using InitFields = ZuTypeGrep<ZtFieldFilter::Init, AllFields>;
  using UpdFields = ZuTypeGrep<ZtFieldFilter::Upd, AllFields>;
  using DelFields = ZuTypeGrep<ZtFieldFilter::Del, AllFields>;

  using Lookup = ZtCSV::Lookup<SaveFields>;

  ZuUnion<void, Lookup>	lookup;
  mutable Row		row;

  Reader() = default;

  // process a single line
  // - returns the number of bytes consumed
  template <typename L>
  int process_(ZuSpan<char> data, L &l) {
    if (lookup.template is<void>()) { // reading header line
      ZuSpan<char> header_[SaveFields::N];
      Header header(&header_[0], 0, SaveFields::N, false);
      auto n = split(data, header);
      if (n >= 0) new (lookup.template new_<Lookup, true>()) Lookup(header);
      return n < 0 ? 0 : n;
    } else { // reading body line
      const auto &lookup_ = lookup.template p<Lookup>();
      auto row_ = ZmAlloc(Cell, lookup_.ncols);
      row = Row(&row_[0], 0, lookup_.ncols, false);
      auto n = split(data, row);
      if (n >= 0) ZuFwd<L>(l)(*this);
      row = {};
      return n < 0 ? 0 : n;
    }
  }

  // process multiple lines, up to length
  // - returns the number of bytes consumed
  template <typename L>
  int process(ZuSpan<char> data, L &l) {
    auto begin = &data[0];
    while (data) {
      auto n = process_(data, l);
      if (n <= 0) break;
      data.offset(n);
    }
    return &data[0] - begin;
  }

  template <typename Field>
  auto loadField() const {
    enum { TypeCode = Field::Type::Code };
    using Props = typename Field::Props;
    using T = typename Field::T;
    using R = decltype(loadValue<TypeCode, Props, T>(ZuDeclVal<Cell &>()));
    {
      enum { I = ZuTypeIndex<Field, SaveFields>{} };
      auto j = lookup.template p<Lookup>()[I];
      if (j >= 0) return loadValue<TypeCode, Props, T>(row[j]);
    }
    if constexpr (ZtFieldTC::IsVec<TypeCode>{})
      return R();
    else
      return R{Field::deflt()};
  }

  template <typename ...Field>
  struct Ctor {
    template <typename ...Args>
    static O ctor(const Reader &reader, Args &&...args) {
      return O(ZuFwd<Args>(args)..., reader.loadField<Field>()...);
    }
    template <typename ...Args>
    static void new_(void *o, const Reader &reader, Args &&...args) {
      new (o) O(ZuFwd<Args>(args)..., reader.loadField<Field>()...);
    }
  };
  template <typename ...Args>
  O ctor(Args &&...args) const {
    if constexpr (!InitFields::N) // exploit guaranteed copy elision
      return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
    else {
      O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
      });
      return o;
    }
  }
  template <typename ...Args>
  void new_(void *o_, Args &&...args) const {
    ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
    O &o = *static_cast<O *>(o_);
    ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
      Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
    });
  }

  void load(O &o) const {
    ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
      Field::set(o, this->loadField<ZtFieldFilter::Load, Field>());
    });
  }
  void update(O &o) const {
    ZuUnroll::all<UpdFields>([this, &o]<typename Field>() {
      Field::set(o, this->loadField<ZtFieldFilter::Upd, Field>());
    });
  }

  template <typename L>
  unsigned read(ZuSpan<char> span, L l) {
    unsigned n = 0;
    unsigned r;
    while ((r = process(span, l)) > 0) {
      span.offset(r);
      n += r;
    }
    return n;
  }
};

template <typename O, typename Facet = ZuFacet::CSV>
inline auto reader() {
  return Reader<O, Facet>{};
}

} // ZtCSV

#endif /* ZtCSV_HH */
