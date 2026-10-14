//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZfStruct FlatBuffers load/save
// - compile-time field mapping and canonical wire layout
// - borrowed field access and direct downward-buffer construction
// - explicit verification at external input boundaries
// - reflection and schema export in ZiFBReflect.hh

#ifndef ZiFB_HH
#define ZiFB_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <stdint.h>
#include <string.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuBitmap.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuIter.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuStruct.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBitmap.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtCase.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZiTxStream.hh>

ZuStructFacet(FB);

// --- facet configuration

struct ZiFB_DefltConfig {
  enum { SizePrefixed = false };
};

template <bool _, typename NTP = ZiFB_DefltConfig>
struct ZiFB_SizePrefixed_ : public NTP { enum { SizePrefixed = _ }; };
template <typename NTP = ZiFB_DefltConfig>
using ZiFB_SizePrefixed = ZiFB_SizePrefixed_<true, NTP>;

ZiFB_DefltConfig ZiFB_Config(...); // default

// ZiFBConfig(Facet, Config) - configure a facet at top-level namespace.
// Pass ZiFB::Config<ZuFacet::Base> as the tail NTP to extend a base facet.
// Custom overloads live in the facet namespace for ADL.
#define ZiFBConfig(Facet, Config) \
  namespace ZuFacet { ZuPP_Strip(Config) ZiFB_Config(Facet *); }

namespace ZiFB {

template <typename Facet>
using Config = decltype(ZiFB_Config(ZuDeclVal<Facet *>()));

}

// --- field properties

namespace ZuFieldProp::FB {

template <unsigned N> struct FieldID { };
template <ZuString Name> struct ID { };
template <unsigned N> struct Align { };
struct Key { };

template <typename Field>
consteval auto name() {
  if constexpr (HasValue<typename Field::Props, ID>{})
    return GetValue<typename Field::Props, ID>{}();
  else
    return ZtCase::camelSnake<Field::id()>();
}

template <typename Field>
consteval unsigned align() {
  if constexpr (HasValue<typename Field::Props, Align>{}) {
    constexpr unsigned N = GetValue<typename Field::Props, Align>{};
    // Canonical FlatBuffers' supported maximum alignment is 32 bytes.
    ZuAssert(N && !(N & (N - 1)) && N <= 32);
    return N;
  } else
    return 0;
}

} // ZuFieldProp::FB

namespace ZiFB {

// --- type metadata

namespace Kind { enum { Table, Inline, Union, Enum }; }

template <typename O, typename Facet> class View;
template <typename T> struct IsView : public ZuFalse { };
template <typename O, typename Facet>
struct IsView<View<O, Facet>> : public ZuTrue { };

template <ZuString Name, unsigned Kind_>
struct Named {
  enum { Kind = Kind_ };
  static constexpr auto name() { return Name; }
};
template <ZuString Name>
struct AsTable : public Named<Name, Kind::Table> {
  template <typename O, typename Facet = ZuFacet::FB> class Handler;
  template <typename O> struct Value;
  template <typename Fields> struct IDs;
  template <typename F> struct IsNestedField;
  template <unsigned N, typename F>
  static decltype(auto) withOffsets(F &&fn);
};
template <ZuString Name>
struct AsStruct : public Named<Name, Kind::Inline> {
  template <typename O, typename Facet = ZuFacet::FB> class Handler;
  template <typename O> struct Value;
  template <typename O> struct Layout;
};
// Name identifies the root adapter; entry tables append "Entry".
// Maps remain UDTs in ZfStruct. Native intent comes only from ADL AsMap selection.
template <typename Fmt, typename K, typename V> struct MapEntry;
template <ZuString Name, unsigned KeyCode, unsigned ValCode,
  typename KeyProps = ZuTypeList<>, typename ValProps = ZuTypeList<>>
struct AsMap : public Named<Name, Kind::Table> {
  template <typename O, typename Facet = ZuFacet::FB> class Handler;
  template <typename O> struct Value;
  enum { KC = KeyCode, VC = ValCode };
  using KP = KeyProps;
  using VP = ValProps;
  template <typename K, typename V> using Entry = MapEntry<AsMap, K, V>;
  template <typename O> struct Root;
};
template <typename O> class MapView;
template <typename T> struct IsMapView : public ZuFalse { };
template <typename O> struct IsMapView<MapView<O>> : public ZuTrue { };
template <typename V, typename = void> struct IsMapWire : public ZuFalse { };
template <typename V>
struct IsMapWire<V, decltype(ZuDeclVal<typename V::MapType *>(), void())> :
  public ZuTrue { };

// Enum names are attached to the existing Z enum map; its Names typelist
// remains the source of enumerator names and ordinals.
template <ZuString Name>
using AsEnum = Named<Name, Kind::Enum>;

// The ordinal in the alternative list is the tag minus one (zero is NONE).
// Distinct alternatives may name the same C++ payload type.
template <ZuString Name, typename T_>
struct Alt {
  using T = T_;
  static constexpr auto name() { return Name; }
};
template <ZuString Name, typename ...Alts>
struct AsUnion : public Named<Name, Kind::Union> {
  template <typename O, typename Facet = ZuFacet::FB> struct Handler;
  template <typename O> struct Value;
  using Types = ZuTypeList<Alts...>;
  ZuAssert(sizeof...(Alts) < 256);
  // ZuUnion<void, ...> gives NONE tag zero and positional alternatives.
  // Positions, not payload types, distinguish repeated payload alternatives.
  template <typename T> static unsigned tag(const T &v) { return v.type(); }
  template <unsigned I, typename T>
  static decltype(auto) value(const T &v) { return v.template p<I>(); }
};

} // ZiFB

// ADL provides type names/representation; fields stay in ordinary ZfStruct.
// Example: ZiFB::AsTable<"app.Row"> ZiFB_Fmt(Row *);
void ZiFB_Fmt(...);

namespace ZiFB {

template <typename T>
using As = decltype(ZiFB_Fmt(ZuDeclVal<ZuUnder<T> *>()));

template <typename T, typename = void>
struct IsUnion : public ZuFalse { };
template <typename T>
struct IsUnion<T, decltype(As<T>::Kind, void())> :
  public ZuBool<As<T>::Kind == Kind::Union> { };

template <typename T, typename = void>
struct IsStruct : public ZuFalse { };
template <typename T>
struct IsStruct<T, decltype(As<T>::Kind, void())> :
  public ZuBool<As<T>::Kind == Kind::Inline> { };

template <ZuString Name>
template <typename ...Field>
struct AsTable<Name>::IDs<ZuTypeList<Field...>> {
  template <typename F>
  using Slots = ZuUnsigned<1 + unsigned(IsUnion<typename F::T>{})>;
  using Fields = ZuTypeList<Field...>;
  // Vtables have two uint16_t header words followed by one word per slot.
  enum { MaxFields = (UINT16_MAX - (sizeof(uint16_t)<<1))/sizeof(uint16_t) };
  enum { N = (0U + ... + Slots<Field>{}) };
  enum { Explicit = (ZuFieldProp::HasValue<
    typename Field::Props, ZuFieldProp::FB::FieldID>{} && ...) };
  enum { Implicit = (!ZuFieldProp::HasValue<
    typename Field::Props, ZuFieldProp::FB::FieldID>{} && ...) };
  ZuAssert(Explicit || Implicit);
  ZuAssert(N <= MaxFields);

  template <typename F>
  static consteval unsigned id() {
    if constexpr (Explicit) {
      return ZuFieldProp::GetValue<
	typename F::Props, ZuFieldProp::FB::FieldID>{};
    } else {
      constexpr unsigned I = ZuTypeIndex<F, Fields>{};
      unsigned n = Slots<F>{} - 1;
      ZuUnroll::all<Fields>([&n]<typename G>() {
	if constexpr (ZuTypeIndex<G, Fields>{} < I) n += Slots<G>{};
      });
      return n;
    }
  }

  template <typename F> using Index = ZuUnsigned<id<F>()>;
  using Ordered = ZuTypeSort<Index, Fields>;

  static consteval bool valid() {
    unsigned next = 0;
    bool ok = true;
    ZuUnroll::all<Ordered>([&next, &ok]<typename F>() {
      next += Slots<F>{};
      if (id<F>() != next - 1) ok = false;
    });
    return ok;
  }
  ZuAssert(valid());
};

// --- input functions

using Offset = uint32_t;
using SOffset = int32_t;
using VOffset = uint16_t;

namespace Wire {
  // Ordinary FlatBuffers offsets, not the wider-offset format variants.
  enum { Align = 8, MaxAlign = 32, MaxSize = INT32_MAX };
  enum { VHeader = 2 * sizeof(VOffset), Identifier = 4 };

  // A received span need not have a naturally aligned base address. Packed
  // wire access preserves that contract without taking a local scalar's
  // address (and triggering stack protection for each read in debug builds).
#pragma pack(push, 1)
  template <typename T> struct Scalar { ZuLittleEndian<T> value; };
#pragma pack(pop)
}

template <typename T>
ZuInline T read(const void *p) {
  return static_cast<const Wire::Scalar<T> *>(p)->value;
}

template <typename T>
ZuInline void write(void *p, T v) {
  static_cast<Wire::Scalar<T> *>(p)->value = v;
}

ZuInline const uint8_t *follow(const uint8_t *p) {
  return p + read<Offset>(p);
}

// Borrowed views are trusted accessors. Verification belongs at ingress.
class VectorView {
public:
  VectorView() = default;
  explicit VectorView(const uint8_t *p) : m_data{p} { }

  bool operator !() const { return !m_data; }
  ZuOpBool
  const uint8_t *data() const {
    return m_data ? m_data + sizeof(Offset) : nullptr;
  }
  unsigned length() const { return m_data ? read<Offset>(m_data) : 0; }
  template <typename T>
  T scalar(unsigned i) const { return read<T>(data() + i * sizeof(T)); }
  const uint8_t *object(unsigned i) const {
    return follow(data() + i * sizeof(Offset));
  }
  ZuBSpan bytes() const { return {data(), length()}; }
  ZuCSpan string() const { return bytes(); }

private:
  const uint8_t *m_data = nullptr;
};

class TableView {
public:
  TableView() = default;
  explicit TableView(const uint8_t *p) : m_data{p} { }

  bool operator !() const { return !m_data; }
  ZuOpBool
  const uint8_t *data() const { return m_data; }
  ZuInline const uint8_t *vtable() const {
    return m_data - read<SOffset>(m_data);
  }
  const uint8_t *field(unsigned id) const {
    if (!m_data) return nullptr;
    auto vt = vtable();
    unsigned slot = Wire::VHeader + id * sizeof(VOffset);
    if (slot >= read<VOffset>(vt)) return nullptr;
    auto offset = read<VOffset>(vt + slot);
    return offset ? m_data + offset : nullptr;
  }
  bool has(unsigned id) const { return field(id); }
  template <typename T>
  ZuInline T scalar(unsigned id, T deflt) const {
    auto p = field(id);
    return p ? read<T>(p) : deflt;
  }
  const uint8_t *object(unsigned id) const {
    auto p = field(id);
    return p ? follow(p) : nullptr;
  }
  VectorView vector(unsigned id) const { return VectorView{object(id)}; }
  TableView table(unsigned id) const { return TableView{object(id)}; }

private:
  const uint8_t *m_data = nullptr;
};

template <bool SizePrefix = false>
TableView root(const ZuBSpan &bytes) {
  return TableView{follow(bytes.data() + (SizePrefix ? sizeof(Offset) : 0))};
}

// Retains only verified table bounds, not an owning schema representation.
struct TableBounds {
  unsigned table;
  unsigned vtable;
  VOffset size;
  VOffset vsize;
};

struct VerifyLimits {
  // Match the current canonical verifier's default traversal budgets.
  unsigned depth = 64;
  unsigned tables = 1000000;
};

class ZiAPI Verifier {
public:
  explicit Verifier(const ZuBSpan &bytes, VerifyLimits limits = {}) :
    m_bytes{bytes}, m_limits{limits} { }

  const uint8_t *data() const { return m_bytes.data(); }
  unsigned size() const { return m_bytes.length(); }

  bool range(unsigned at, unsigned n, unsigned align = 1) const {
    return !(at & (align - 1)) &&
      uint64_t(at) + n <= m_bytes.length();
  }
  template <typename T>
  bool scalar(unsigned at) const { return range(at, sizeof(T), sizeof(T)); }

  bool offset(unsigned at, unsigned &target) const;
  // field() or vector() has already checked the offset word's bounds/alignment.
  bool offset_(unsigned at, unsigned &target) const;
  bool table(unsigned at, TableBounds &bounds) const;
  // Width/stride/alignment come from resolved or already verified schema
  // metadata. Stride is nonzero; alignment is a nonzero power of two.
  bool field(const TableBounds &, unsigned id,
    unsigned width, unsigned align, unsigned &at) const;
  bool vector(unsigned at, unsigned stride, unsigned align,
    unsigned &count) const;
  bool string(unsigned at) const;

  template <bool SizePrefix = false>
  bool root(unsigned &at, const char *id = nullptr) {
    if (m_bytes.length() >= Wire::MaxSize) return false;
    if constexpr (SizePrefix) {
      if (!scalar<Offset>(0)) return false;
      unsigned n = read<Offset>(data());
      if (n > m_bytes.length() - sizeof(Offset)) return false;
      m_bytes = {data(), n + sizeof(Offset)};
    }
    constexpr unsigned start = SizePrefix ? sizeof(Offset) : 0;
    if (id && (!range(start + sizeof(Offset), Wire::Identifier) ||
	memcmp(data() + start + sizeof(Offset), id, Wire::Identifier)))
      return false;
    return offset(start, at);
  }

  bool enter() {
    if (m_depth >= m_limits.depth || m_tables >= m_limits.tables) return false;
    ++m_depth;
    ++m_tables;
    return true;
  }
  void leave() { --m_depth; }

private:
  ZuBSpan		m_bytes;
  VerifyLimits		m_limits;
  unsigned		m_depth = 0;
  unsigned		m_tables = 0;
};

inline bool Verifier::offset(unsigned at, unsigned &target) const
{
  return scalar<Offset>(at) && offset_(at, target);
}

inline bool Verifier::offset_(unsigned at, unsigned &target) const
{
  auto n = read<Offset>(data() + at);
  if (!n || n >= Wire::MaxSize || n >= m_bytes.length() - at) return false;
  target = at + n;
  return true;
}

inline bool Verifier::table(unsigned at, TableBounds &bounds) const
{
  if (!scalar<SOffset>(at)) return false;
  // Signed vtable displacements allow canonical forward-shared vtables.
  int64_t vt = int64_t(at) - read<SOffset>(data() + at);
  if (vt < 0 || vt > UINT32_MAX || !range(vt, Wire::VHeader, sizeof(VOffset)))
    return false;
  auto vsize = read<VOffset>(data() + vt);
  auto size = read<VOffset>(data() + vt + sizeof(VOffset));
  if (vsize < Wire::VHeader || (vsize & 1) ||
      !range(vt, vsize) || size < sizeof(SOffset) || !range(at, size))
    return false;
  bounds = {at, unsigned(vt), size, vsize};
  return true;
}

inline bool Verifier::field(const TableBounds &bounds, unsigned id,
  unsigned width, unsigned align, unsigned &at) const
{
  at = 0; // no field; a required-field check belongs to the schema visitor
  unsigned n = (bounds.vsize - Wire::VHeader)/sizeof(VOffset);
  if (id >= n) return true;
  auto slot = read<VOffset>(
    data() + bounds.vtable + Wire::VHeader + id * sizeof(VOffset));
  if (!slot) return true;
  if (slot < sizeof(SOffset) || uint64_t(slot) + width > bounds.size) return false;
  at = bounds.table + slot;
  // table() already verified the entire object; only alignment remains.
  return !(at & (align - 1));
}

inline bool Verifier::vector(unsigned at, unsigned stride, unsigned align,
  unsigned &count) const
{
  if (!scalar<Offset>(at)) return false;
  count = read<Offset>(data() + at);
  unsigned start = at + sizeof(Offset);
  if (uint64_t(count) * stride > m_bytes.length() - start) return false;
  // The count bound above already covers the complete element region.
  return !count || !(start & (align - 1));
}

inline bool Verifier::string(unsigned at) const
{
  unsigned n;
  if (!vector(at, 1, 1, n)) return false;
  unsigned end = at + sizeof(Offset) + n;
  return range(end, 1) && !data()[end];
}

// --- output functions

ZuDerive(Buffer, (ZtArray<uint8_t, ZtArrayHeapID<"ZiFB.Buffer">>));

// Pass a Buffer or its caller-scoped ZtScratch backing. The array owns any
// fallback allocation; the arena borrows it. ZiFB supplies the I/O policy.
template <unsigned Align = Wire::Align>
class ArrayStorage {
  ZuAssert(Align && !(Align & (Align - 1)) && Align <= Wire::MaxAlign);
public:
  explicit ArrayStorage(Buffer &buf) : m_buf{buf} { }

  uint8_t *data() const { return aligned(m_buf); }
  uint64_t size() const {
    if (!m_buf.data()) return 0;
    auto skip = uintptr_t(data()) - uintptr_t(m_buf.data());
    auto n = m_buf.size();
    return n > skip ? (n - skip) & ~(Align - 1) : 0;
  }
  // Storage policies reject ownership/capacity before infallible allocation.
  static bool canGrow(unsigned) { return true; }
  uint8_t *grow(unsigned oldSize, unsigned newSize,
    unsigned head, unsigned tail) {
    Buffer next(0, uint64_t(newSize) + Align - 1);
    auto dest = aligned(next);
    auto src = data();
    if (head) memcpy(dest, src, head);
    if (tail) memcpy(dest + newSize - tail, src + oldSize - tail, tail);
    m_buf = ZuMv(next);
    return dest;
  }

private:
  static uint8_t *aligned(Buffer &buf) {
    if (!buf.data()) return nullptr;
    auto p = uintptr_t(buf.data());
    return reinterpret_cast<uint8_t *>((p + Align - 1) & ~uintptr_t(Align - 1));
  }

  Buffer &m_buf;
};

namespace Error { enum { OK, Capacity, TableSize, Required, Union, Value }; }

template <typename Storage>
class Arena {
public:
  // The empty-arena initial size matches the current canonical builder.
  enum { Initial = 1024 };

  explicit Arena(Storage &storage, unsigned align = Wire::Align) :
    m_storage{storage}, m_align{align} { reset(); }

  Arena(const Arena &) = delete;
  Arena &operator =(const Arena &) = delete;

  unsigned size() const { return m_size; }
  unsigned head() const { return m_head; }
  unsigned capacity() const { return m_capacity; }
  unsigned alignment() const { return m_align; }
  unsigned minAlign() const { return m_minAlign; }
  void minAlign(unsigned n) { m_minAlign = n; }
  int error() const { return m_error; }
  bool failed() const { return m_error != Error::OK; }
  void fail(int error) { if (!m_error) m_error = error; }

  uint8_t *data() const { return m_data ? m_data + m_capacity - m_size : nullptr; }
  uint8_t *at(unsigned offset) const { return m_data + m_capacity - offset; }
  uint8_t *scratch(unsigned offset = 0) const { return m_data + offset; }
  ZuBSpan span() const { return {data(), m_size}; }

  bool ensure(unsigned n) {
    if (failed()) return false;
    if (n <= m_capacity - m_size - m_head) return true;
    return grow_(n);
  }

  uint8_t *push(unsigned n) {
    if (!ensure(n)) return nullptr;
    m_size += n;
    return data();
  }
  uint8_t *pushScratch(unsigned n) {
    if (!ensure(n)) return nullptr;
    auto p = scratch(m_head);
    m_head += n;
    return p;
  }
  void pop(unsigned n) { ZmAssert(n <= m_size); m_size -= n; }
  void popScratch(unsigned n) { ZmAssert(n <= m_head); m_head -= n; }

  void zero(unsigned n) {
    if (!n) return;
    if (auto p = push(n)) memset(p, 0, n);
  }
  void bytes(ZuBSpan span) {
    if (span.length() >= Wire::MaxSize) { fail(Error::Capacity); return; }
    if (!span.length()) return;
    if (auto p = push(span.length())) memcpy(p, span.data(), span.length());
  }
  void align(unsigned align, unsigned after = 0) {
    // Storage is provisioned for the schema's maximum alignment up front.
    ZmAssert(align && !(align & (align - 1)) && align <= m_align);
    if (align > m_minAlign) m_minAlign = align;
    zero((0U - m_size - after) & (align - 1));
  }

  template <typename T>
  ZuInline unsigned scalar(T value) {
    align(sizeof(T));
    if (auto p = push(sizeof(T))) {
      write<T>(p, value);
      return m_size;
    }
    return 0;
  }
  ZuInline unsigned offset(unsigned target) {
    if (failed()) return 0;
    align(sizeof(Offset));
    ZmAssert(target && target <= m_size);
    if (auto p = push(sizeof(Offset))) {
      write<Offset>(p, m_size - target);
      return m_size;
    }
    return 0;
  }

  // Reset retains the allocation. The storage owner handles destruction or
  // detach; no arena destructor may free transferred payload storage.
  void clear() {
    m_head = m_size = 0;
    m_minAlign = 1;
    m_error = Error::OK;
  }
  void reset() {
    clear();
    m_data = m_storage.data();
    auto n = m_storage.size();
    if (n >= Wire::MaxSize) {
      m_capacity = 0;
      fail(Error::Capacity);
    } else
      m_capacity = n & ~(m_align - 1);
  }

private:
  // Keep allocation/relocation out of each small scalar/field operation.
  ZuNoInline bool grow_(unsigned n) {
    uint64_t capacity;
    if (!m_size && !m_head) {
      // Canonical first allocation uses max(request, initial capacity).
      capacity = m_capacity || n > Initial ? n : Initial;
    } else {
      unsigned extra = m_capacity>>1;
      if (extra < n) extra = n;
      capacity = uint64_t(m_capacity) + extra;
    }
    capacity += m_align - 1;
    capacity &= ~uint64_t(m_align - 1);
    if (capacity >= Wire::MaxSize || !m_storage.canGrow(unsigned(capacity))) {
      fail(Error::Capacity);
      return false;
    }
    m_data = m_storage.grow(m_capacity, unsigned(capacity), m_head, m_size);
    m_capacity = capacity;
    return true;
  }

  Storage			&m_storage;
  uint8_t			*m_data = nullptr;
  unsigned		m_capacity = 0;
  unsigned		m_align;
  unsigned		m_head = 0;
  unsigned		m_size = 0;
  unsigned		m_minAlign = 1;
  int			m_error = Error::OK;
};

// --- table/vector construction

struct TableState {
  unsigned start;
  unsigned head;
  unsigned slots = 0;

  void omit(unsigned) { }
  template <typename B>
  bool record(B &b, unsigned id, unsigned offset) {
    if (auto p = b.pushScratch(2 * sizeof(Offset))) {
      write<Offset>(p, offset);
      write<Offset>(p + sizeof(Offset), id);
      return true;
    }
    return false;
  }
  template <typename B>
  void vtable(B &b, unsigned obj, unsigned size) {
    b.zero(size);
    if (b.failed()) return;
    unsigned end = b.head();
    for (unsigned i = head; i < end; i += 2 * sizeof(Offset)) {
      auto p = b.scratch(i);
      unsigned value = read<Offset>(p);
      unsigned id = read<Offset>(p + sizeof(Offset));
      unsigned slot = Wire::VHeader + id * sizeof(VOffset);
      ZmAssert(!read<VOffset>(b.data() + slot));
      write<VOffset>(b.data() + slot, obj - value);
    }
    b.popScratch(end - head);
  }
};

// Typed saving reuses its child offsets as final field positions. Generic
// composition keeps arena records; both share the same table finalizer.
struct TypedTable {
  Offset *fields;
  unsigned start;
  unsigned slots = 0;

  void omit(unsigned id) { fields[id] = 0; }
  template <typename B>
  bool record(B &, unsigned id, unsigned offset) {
    fields[id] = offset;
    return true;
  }
  template <typename B>
  void vtable(B &b, unsigned obj, unsigned size) {
    if (auto p = b.push(size))
      for (unsigned i = 0; i < slots; ++i) {
	auto value = fields[i];
	write<VOffset>(p + Wire::VHeader + i * sizeof(VOffset),
	  value ? obj - value : 0);
      }
  }
};

struct NestedState {
  unsigned base;
  unsigned cache;
  unsigned head;
  unsigned align;
};

template <typename Storage>
class Builder : public Arena<Storage> {
  using Base = Arena<Storage>;
public:
  using Base::Base;
  using Base::align;
  using Base::alignment;
  using Base::at;
  using Base::bytes;
  using Base::data;
  using Base::error;
  using Base::fail;
  using Base::failed;
  using Base::head;
  using Base::minAlign;
  using Base::pop;
  using Base::popScratch;
  using Base::push;
  using Base::pushScratch;
  using Base::scalar;
  using Base::scratch;
  using Base::size;
  using Base::zero;

  // Build children before starting their containing table. Scratch holds
  // cached vtable offsets followed by this table's (offset, ID) records.
  TableState startTable() { return {size(), head()}; }
  TypedTable startTable(Offset *fields) { return {fields, size()}; }

  template <typename T, typename Table>
  void field(Table &table, unsigned id, T value, T deflt,
    bool force = false) {
    if (!force && value == deflt) { table.omit(id); return; }
    fieldAt(table, id, this->template scalar<T>(value));
  }

  template <typename Table>
  void fieldAt(Table &table, unsigned id, unsigned offset) {
    if (failed()) return;
    if (table.record(*this, id, offset) && id >= table.slots) table.slots = id + 1;
  }

  unsigned offset(unsigned target) {
    if (failed()) return 0;
    ZmAssert(target > m_base); // references cannot escape a nested root
    return Base::offset(target);
  }

  template <typename Table>
  void fieldOffset(Table &table, unsigned id, unsigned target) {
    if (target) fieldAt(table, id, offset(target));
    else table.omit(id);
  }

  template <typename Table, typename Fill>
  void fieldInline(Table &table, unsigned id,
    unsigned width, unsigned align_, Fill &&fill) {
    align(align_, width);
    if (auto p = push(width)) {
      ZuFwd<Fill>(fill)(p);
      fieldAt(table, id, size());
    }
  }

  template <typename Table>
  unsigned endTable(Table table) {
    if (failed()) return 0;
    unsigned obj = this->template scalar<SOffset>(0);
    unsigned objectSize = obj - table.start;
    if (objectSize > UINT16_MAX ||
	table.slots > (UINT16_MAX - Wire::VHeader)/sizeof(VOffset)) {
      fail(Error::TableSize);
      return 0;
    }
    unsigned vtSize = Wire::VHeader + table.slots * sizeof(VOffset);
    table.vtable(*this, obj, vtSize);
    if (failed()) return 0;
    unsigned vt = size();
    write<VOffset>(data(), vtSize);
    write<VOffset>(data() + sizeof(VOffset), objectSize);
    // Both policies have released this table's records; only cache remains.
    unsigned cacheEnd = head();
    for (unsigned i = m_cache; i < cacheEnd; i += sizeof(Offset)) {
      unsigned prior = read<Offset>(scratch(i));
      auto p = at(prior);
      if (read<VOffset>(p) != vtSize || memcmp(p, data(), vtSize)) continue;
      pop(vtSize);
      vt = prior;
      break;
    }
    if (vt == size()) {
      if (auto p = pushScratch(sizeof(Offset))) write<Offset>(p, vt);
    }
    if (failed()) return 0;
    write<SOffset>(at(obj), SOffset(vt) - SOffset(obj));
    return obj;
  }

  unsigned string(ZuCSpan value) {
    if (value.length() >= Wire::MaxSize - sizeof(Offset) - 1) {
      fail(Error::Capacity);
      return 0;
    }
    align(sizeof(Offset), value.length() + 1);
    zero(1);
    bytes(value);
    return this->template scalar<Offset>(value.length());
  }

  // The fill callback writes the reserved region without growing the arena.
  // Scalar/inline vectors need no intermediate initialized container.
  template <typename Fill>
  unsigned vector(unsigned count, unsigned stride, unsigned align_, Fill &&fill) {
    uint64_t n = uint64_t(count) * stride;
    if (n >= Wire::MaxSize - sizeof(Offset)) {
      fail(Error::Capacity);
      return 0;
    }
    // Power-of-two alignments compose to their maximum.
    if (align_ < sizeof(Offset)) align_ = sizeof(Offset);
    align(align_, n);
    if (n)
      if (auto p = push(n)) ZuFwd<Fill>(fill)(p);
    return this->template scalar<Offset>(count);
  }

  template <typename T>
  unsigned vector(ZuSpan<const T> values) {
    if (values.length() >= Wire::MaxSize/sizeof(T)) {
      fail(Error::Capacity);
      return 0;
    }
    return vector(values.length(), sizeof(T), sizeof(T),
      [values](uint8_t *p) {
	for (unsigned i = 0, n = values.length(); i < n; ++i)
	  write<T>(p + i * sizeof(T), values[i]);
      });
  }

  unsigned offsets(ZuSpan<const Offset> values, unsigned align_ = sizeof(Offset)) {
    if (values.length() >= Wire::MaxSize/sizeof(Offset)) {
      fail(Error::Capacity);
      return 0;
    }
    return vector(values.length(), sizeof(Offset), align_,
      [this, values](uint8_t *p) {
	for (unsigned i = 0, n = values.length(); i < n; ++i) {
	  auto target = values[i];
	  unsigned from = size() - i * sizeof(Offset);
	  ZmAssert(target > m_base && target < from);
	  write<Offset>(p + i * sizeof(Offset), from - target);
	}
      });
  }

  // Preserve the original serializer's field selection and physical slots.
  unsigned copyRoot(ZuBSpan value, unsigned align_) {
    if (value.length() >= Wire::MaxSize) { fail(Error::Capacity); return 0; }
    return vector(value.length(), 1, align_, [value](uint8_t *p) {
      if (value.length()) memcpy(p, value.data(), value.length());
    });
  }

  template <bool SizePrefix = false>
  ZuBSpan finish(unsigned root, const char *id = nullptr) {
    if (failed()) return {};
    unsigned after = sizeof(Offset) + (SizePrefix ? sizeof(Offset) : 0) +
      (id ? Wire::Identifier : 0);
    align(minAlign(), after);
    if (id) bytes({id, Wire::Identifier});
    offset(root);
    if constexpr (SizePrefix) this->template scalar<Offset>(size() - m_base);
    return failed() ? ZuBSpan{} : ZuBSpan{data(), size() - m_base};
  }

  NestedState startNested() {
    // A root's end is aligned before its own layout starts. This lets its
    // relative alignment remain valid after extraction from the parent.
    align(alignment());
    NestedState state{m_base, m_cache, head(), minAlign()};
    m_base = size();
    m_cache = head();
    minAlign(1);
    return state;
  }

  template <bool SizePrefix = false>
  unsigned endNested(NestedState state, unsigned root, const char *id = nullptr) {
    auto span = finish<SizePrefix>(root, id);
    unsigned n = span.length();
    unsigned align_ = minAlign();
    popScratch(head() - state.head);
    m_base = state.base;
    m_cache = state.cache;
    if (state.align > align_) align_ = state.align;
    minAlign(align_);
    return this->template scalar<Offset>(n);
  }

  void clear() {
    Base::clear();
    m_base = m_cache = 0;
  }
  void reset() {
    Base::reset();
    m_base = m_cache = 0;
  }

private:
  unsigned m_base = 0;
  unsigned m_cache = 0;
};

} // ZiFB

// --- field save/load

namespace ZiFB::Values {

struct Int128 { uint64_t h, l; };
ZfStruct(ZiAPI, (Int128, FB),
  (h,	(Deflt<0>),	UInt64),
  (l,	(Deflt<0>),	UInt64));
AsStruct<"Zf.Int128"> ZiFB_Fmt(Int128 *);
struct UInt128 { uint64_t h, l; };
ZfStruct(ZiAPI, (UInt128, FB),
  (h,	(Deflt<0>),	UInt64),
  (l,	(Deflt<0>),	UInt64));
AsStruct<"Zf.UInt128"> ZiFB_Fmt(UInt128 *);
struct Fixed { int64_t mantissa; uint8_t ndp; };
ZfStruct(ZiAPI, (Fixed, FB),
  (mantissa,	(Deflt<0>),	Int64),
  (ndp,		(Deflt<0>),	UInt8));
AsStruct<"Zf.Fixed"> ZiFB_Fmt(Fixed *);
struct Decimal { uint64_t h, l; };
ZfStruct(ZiAPI, (Decimal, FB),
  (h,	(Deflt<0>),	UInt64),
  (l,	(Deflt<0>),	UInt64));
AsStruct<"Zf.Decimal"> ZiFB_Fmt(Decimal *);
struct Time { int64_t sec; int32_t nsec; };
ZfStruct(ZiAPI, (Time, FB),
  (sec,		(Deflt<0>),	Int64),
  (nsec,	(Deflt<0>),	Int32));
AsStruct<"Zf.Time"> ZiFB_Fmt(Time *);
struct DateTime { int32_t julian, sec, nsec; };
ZfStruct(ZiAPI, (DateTime, FB),
  (julian,	(Deflt<0>),	Int32),
  (sec,		(Deflt<0>),	Int32),
  (nsec,	(Deflt<0>),	Int32));
AsStruct<"Zf.DateTime"> ZiFB_Fmt(DateTime *);
struct Bytes { ZuBSpan data; };
ZfStruct(ZiAPI, (Bytes, FB), (data,, Bytes));
AsTable<"Zf.Bytes"> ZiFB_Fmt(Bytes *);
struct Bitmap { ZuSpan<const uint64_t> data; };
ZfStruct(ZiAPI, (Bitmap, FB), ((data, Rd), (Ctor<0>), UInt64Vec));
AsTable<"Zf.Bitmap"> ZiFB_Fmt(Bitmap *);

template <typename B>
struct BitmapMap {
  using T = Bitmap;
  enum { Code = ZfFieldTC::UDT };
  static T save(const B &v) { return {v.cspan()}; }
  template <typename V>
  static B load(const V &v) {
    auto data = v.template get<ZfField(Bitmap, data)>();
    unsigned n = data.length();
    B out;
    if constexpr (B::Data::Fixed) {
      // Fixed bitmaps initialize their omitted words to zero.
      if (n > B::Words) n = B::Words;
    } else out.data.length(n); // uninitialized storage, filled below
    for (unsigned i = 0; i < n; ++i) out.data[i] = data[i];
    return out;
  }
};

struct HwBitmapMap {
  using T = Bitmap;
  enum { Code = ZfFieldTC::UDT };
  // hwloc's limb is unsigned long: 32 bits on Windows, 64 on LP64.
  enum { Parts = sizeof(uint64_t)/sizeof(unsigned long) };
  ZuAssert(Parts == 1 || Parts == 2);
  template <typename B>
  static unsigned saveTo(B &b, const ZmBitmap &v);
  template <typename V>
  static ZmBitmap load(const V &v) {
    auto data = v.template get<ZfField(Bitmap, data)>();
    unsigned n = data.length() * Parts;
    ZmBitmap out;
    if (!n) return out;
    auto word = [data](unsigned i) {
      return static_cast<unsigned long>(data[i/Parts] >>
	((i % Parts) * (sizeof(unsigned long)<<3)));
    };
    --n;
    hwloc_bitmap_from_ith_ulong(out, n, word(n));
    while (n--) hwloc_bitmap_set_ith_ulong(out, n, word(n));
    return out;
  }
};

struct Int128Map {
  using T = Int128;
  enum { Code = ZfFieldTC::UDT };
  static constexpr T save(int128_t v) { return {uint64_t(v >> 64), uint64_t(v)}; }
  template <typename V>
  static int128_t load(const V &v) {
    return int128_t((uint128_t(v.template get<ZfField(Int128, h)>()) << 64) |
      v.template get<ZfField(Int128, l)>());
  }
};
struct UInt128Map {
  using T = UInt128;
  enum { Code = ZfFieldTC::UDT };
  static constexpr T save(uint128_t v) { return {uint64_t(v >> 64), uint64_t(v)}; }
  template <typename V>
  static uint128_t load(const V &v) {
    return (uint128_t(v.template get<ZfField(UInt128, h)>()) << 64) |
      v.template get<ZfField(UInt128, l)>();
  }
};
struct FixedMap {
  using T = Fixed;
  enum { Code = ZfFieldTC::UDT };
  static T save(const ZuFixed &v) { return {int64_t(v.mantissa), uint8_t(v.ndp)}; }
  template <typename V>
  static ZuFixed load(const V &v) {
    return {v.template get<ZfField(Fixed, mantissa)>(), v.template get<ZfField(Fixed, ndp)>()};
  }
};
struct DecimalMap {
  using T = Decimal;
  enum { Code = ZfFieldTC::UDT };
  static T save(const ZuDecimal &v) { return {uint64_t(v.value >> 64), uint64_t(v.value)}; }
  template <typename V>
  static ZuDecimal load(const V &v) {
    return ZuDecimal{ZuDecimal::Unscaled{int128_t(
      (uint128_t(v.template get<ZfField(Decimal, h)>()) << 64) |
      v.template get<ZfField(Decimal, l)>())}};
  }
};
struct TimeMap {
  using T = Time;
  enum { Code = ZfFieldTC::UDT };
  static T save(const ZuTime &v) { return {v.sec(), int32_t(v.nsec())}; }
  template <typename V>
  static ZuTime load(const V &v) {
    return {v.template get<ZfField(Time, sec)>(), v.template get<ZfField(Time, nsec)>()};
  }
};
struct DateTimeMap {
  using T = DateTime;
  enum { Code = ZfFieldTC::UDT };
  static T save(const ZuDateTime &v) { return {v.julian(), v.sec(), v.nsec()}; }
  template <typename V>
  static ZuDateTime load(const V &v) {
    return ZuDateTime{ZuDateTime::Julian{v.template get<ZfField(DateTime, julian)>()},
      v.template get<ZfField(DateTime, sec)>(), v.template get<ZfField(DateTime, nsec)>()};
  }
};
struct BytesMap {
  using T = Bytes;
  enum { Code = ZfFieldTC::UDT };
  static T save(ZuBSpan v) { return {v}; }
  template <typename V>
  static ZuBSpan load(const V &v) { return v.template get<ZfField(Bytes, data)>(); }
};

} // ZiFB::Values

namespace ZuBitmap_ {
  template <unsigned Bits>
  ZiFB::Values::BitmapMap<Bitmap<Bits>> ZiFBTransform(Bitmap<Bits> *);
  template <unsigned Bits>
  inline ZuCSpan ZfVFieldTypeID(Bitmap<Bits> *) { return "Bitmap"; }
}
namespace ZtBitmap_ {
  ZiFB::Values::BitmapMap<Bitmap> ZiFBTransform(Bitmap *);
  inline ZuCSpan ZfVFieldTypeID(Bitmap *) { return "Bitmap"; }
}
namespace ZmBitmap_ {
  ZiFB::Values::HwBitmapMap ZiFBTransform(Bitmap *);
  inline ZuCSpan ZfVFieldTypeID(Bitmap *) { return "Bitmap"; }
}

// Application and higher-layer mappings supply { T, Code, save(), load() }
// through ADL. zf never depends on ZiIP or application value definitions.
void ZiFBTransform(...);

namespace ZiFB {

template <typename T>
using Transform = decltype(ZiFBTransform(ZuDeclVal<ZuDecay<T> *>()));

template <typename T_, unsigned Code_, typename = void>
struct Mapping {
  using T = T_;
  enum { Code = Code_, Mapped = false };
};
template <typename T_, unsigned Code_>
struct Mapping<T_, Code_, decltype(Transform<T_>::Code, void())> :
    public Transform<T_> {
  enum { Mapped = true };
};

// Codes already identify the core Z representations, including wrappers
// convertible to their canonical Z type. No second runtime dispatch is used.
#define ZiFB_ValueMap(Code_) \
  template <typename T> \
  struct Mapping<T, ZfFieldTC::Code_> : public Values::Code_##Map { \
    enum { Mapped = true }; \
  }
ZiFB_ValueMap(Int128);
ZiFB_ValueMap(UInt128);
ZiFB_ValueMap(Fixed);
ZiFB_ValueMap(Decimal);
ZiFB_ValueMap(Time);
ZiFB_ValueMap(DateTime);
#undef ZiFB_ValueMap

// Union alternatives use UDT rather than a field's scalar typecode. Recover
// the same canonical value mappings without inventing native wire structs.
#define ZiFB_UDTMap(Type_, Code_) \
  template <> \
  struct Mapping<Type_, ZfFieldTC::UDT> : \
      public Mapping<Type_, ZfFieldTC::Code_> { }
ZiFB_UDTMap(int128_t, Int128);
ZiFB_UDTMap(uint128_t, UInt128);
ZiFB_UDTMap(ZuFixed, Fixed);
ZiFB_UDTMap(ZuDecimal, Decimal);
ZiFB_UDTMap(ZuTime, Time);
ZiFB_UDTMap(ZuDateTime, DateTime);
#undef ZiFB_UDTMap

// --- field layout

namespace ValueKind { enum { Scalar, String, Vector, Table, Inline, Union, Array }; }

template <typename T, bool = ZuTraits<T>::IsPointer>
struct ObjectType_ { using Type = ZuDecay<T>; };
template <typename T>
struct ObjectType_<T, true> { using Type = ZuDecay<typename ZuTraits<T>::Elem>; };
template <typename T> using ObjectType = typename ObjectType_<T>::Type;

struct WireValue {
  enum { Mapped = false };
  template <typename T>
  static constexpr const T &toWire(const T &v) { return v; }
  template <typename T>
  static T fromWire(T v) { return v; }
};

template <typename T>
struct Scalar : public WireValue {
  using WireType = T;
  enum { Kind = ValueKind::Scalar, Size = sizeof(T), Align = sizeof(T) };
};
struct String : public WireValue {
  enum { Kind = ValueKind::String, Size = sizeof(Offset), Align = sizeof(Offset) };
};
template <typename E>
struct Vector : public WireValue {
  using Elem = E;
  enum { Kind = ValueKind::Vector, Size = sizeof(Offset), Align = sizeof(Offset) };
};

template <ZuString Name>
template <typename O>
struct AsTable<Name>::Value : public WireValue {
  using Native = O;
  enum { Kind = ValueKind::Table, Size = sizeof(Offset), Align = sizeof(Offset) };
};
template <ZuString Name>
template <typename O>
struct AsStruct<Name>::Value : public WireValue {
  using Native = O;
  enum { Kind = ValueKind::Inline, Size = Layout<O>::Size, Align = Layout<O>::Align };
};
template <ZuString Name, typename ...Alts>
template <typename O>
struct AsUnion<Name, Alts...>::Value : public WireValue {
  using Native = O;
  enum { Kind = ValueKind::Union, Size = sizeof(Offset), Align = sizeof(Offset) };
};

template <typename T, unsigned Code> struct Value;
template <typename T, unsigned Code, typename = void> struct Rep;
template <typename T, unsigned Code>
struct Rep<T, Code, ZuIfT<
  (Code >= ZfFieldTC::Int8 && Code <= ZfFieldTC::UInt64)>> :
  public Scalar<ZfFieldTC::Type<Code>> { };
template <typename T>
struct Rep<T, ZfFieldTC::Bool> : public Scalar<uint8_t> { };
template <typename T>
struct Rep<T, ZfFieldTC::Float> :
  public Scalar<ZuIf<ZuIsSame<ZuDecay<T>, float>{}, float, double>> { };
template <typename T>
struct Rep<T, ZfFieldTC::CString> : public String { };
template <typename T>
struct Rep<T, ZfFieldTC::String> : public String { };
template <typename T>
struct Rep<T, ZfFieldTC::Bytes> : public Vector<Scalar<uint8_t>> { };

template <typename T, bool = ZuTraits<T>::IsArray>
struct UDTValue : public As<ObjectType<T>>::template Value<ObjectType<T>> { };
template <typename T>
struct UDTValue<T, true> :
  public Vector<Value<typename ZuTraits<T>::Elem, ZfFieldTC::UDT>> { };
template <typename T>
struct Rep<T, ZfFieldTC::UDT> : public UDTValue<T> { };

template <typename T, unsigned Code>
struct Rep<T, Code, ZuIfT<ZfFieldTC::IsVec<Code>{}>> :
  public Vector<Value<typename ZuTraits<T>::Elem, ZfFieldTC::Elem<Code>{}>> { };

// BytesVec uses a wrapper table; FlatBuffers has no vector-of-vectors.
struct BytesValue : public As<Values::Bytes>::template Value<Values::Bytes> {
  enum { Mapped = true };
  static auto toWire(ZuBSpan v) { return Values::BytesMap::save(v); }
  template <typename V>
  static auto fromWire(const V &v) { return Values::BytesMap::load(v); }
};
template <typename T>
struct Rep<T, ZfFieldTC::BytesVec> : public Vector<BytesValue> { };

template <typename T, unsigned Code>
struct Value : public Rep<typename Mapping<T, Code>::T, Mapping<T, Code>::Code> {
  using Map = Mapping<T, Code>;
  enum { Mapped = Map::Mapped, Code_ = Map::Code };
  template <typename U>
  static constexpr decltype(auto) toWire(const U &v) {
    if constexpr (Mapped) return Map::save(v);
    else return (v);
  }
  template <typename U>
  static auto fromWire(U v) {
    if constexpr (Mapped) return Map::load(v);
    else if constexpr (Code_ == ZfFieldTC::Bool) return v != 0;
    else return Rep<typename Map::T, Map::Code>::fromWire(v);
  }
};

// Synthetic fields use the same field protocol as ZfStruct. Their membership
// is independent of application facets; nested values still receive the facet.
template <typename O_, typename T_, unsigned Code_, ZuString ID,
  typename Props_, auto Get>
struct MapField {
  using O = O_;
  using T = T_;
  using Props = Props_;
  using Orig = MapField;
  enum { Code = Code_, ReadOnly = false };
  template <template <typename> class Override> using Adapt = Override<Orig>;
  static constexpr auto id() { return ID; }
  static decltype(auto) get(const O &o) { return Get(o); }
  static constexpr decltype(auto) deflt() {
    return ZfFieldDeflt<Props, [] {
      using W = ZiFB::Value<T, Code>;
      if constexpr (W::Mapped && __is_constructible(T)) return T{};
      else if constexpr (W::Kind == ValueKind::Scalar) return typename W::WireType(0);
      else if constexpr (__is_constructible(T)) return T{};
    }>();
  }
};
template <typename Fmt, typename K, typename V>
struct MapEntry {
  const K &key;
  const V &value;

  using KF = MapField<MapEntry, K, Fmt::KC, "key",
    typename Fmt::KP::template Push<ZuFieldProp::FB::Key>,
    [](const MapEntry &o) -> const K & { return o.key; }>;
  using VF = MapField<MapEntry, V, Fmt::VC, "value", typename Fmt::VP,
    [](const MapEntry &o) -> const V & { return o.value; }>;
  template <typename Facet>
  friend ZuTypeList<KF, VF> ZuFields_(MapEntry *, Facet *);
  friend AsTable<Fmt::name() + "Entry"_z> ZiFB_Fmt(MapEntry *);
};

template <ZuString Name, unsigned KC, unsigned VC, typename KP, typename VP>
template <typename O>
struct AsMap<Name, KC, VC, KP, VP>::Root {
  const O &entries;

  using F = MapField<Root, O, ZfFieldTC::UDT, "entries", ZuTypeList<>,
    [](const Root &o) -> const O & { return o.entries; }>;
  template <typename Facet>
  friend ZuTypeList<F> ZuFields_(Root *, Facet *);
  friend AsTable<Name> ZiFB_Fmt(Root *);
};

template <ZuString Name, unsigned KC, unsigned VC, typename KP, typename VP>
template <typename O>
struct AsMap<Name, KC, VC, KP, VP>::Value : public Vector<
  ZiFB::Value<Entry<typename O::Key, typename O::Val>, ZfFieldTC::UDT>> {
  using MapType = O;
  template <typename V>
  static auto fromWire(const V &v) { return MapView<O>{v}; }
};

template <typename T> struct ArrayCount : public ZuUnsigned<0> { };
template <typename T, unsigned N>
struct ArrayCount<T[N]> : public ZuUnsigned<N> { };
template <typename T, unsigned N>
struct ArrayCount<ZuArray<T, N>> : public ZuUnsigned<N> { };

template <typename F, bool = ArrayCount<typename F::T>{} &&
  IsStruct<ZuUnder<typename F::O>>{}>
struct FieldValue_ : public Value<typename F::T, F::Code> { };
template <typename F>
struct FieldValue_<F, true> : public Value<typename F::T, F::Code> {
  using V = Value<typename F::T, F::Code>;
  using Elem = typename V::Elem;
  enum { Count = ArrayCount<typename F::T>{} };
  enum { Kind = ValueKind::Array, Size = Count * Elem::Size, Align = Elem::Align };
  ZuAssert(V::Kind == ValueKind::Vector);
  ZuAssert(Elem::Kind == ValueKind::Scalar || Elem::Kind == ValueKind::Inline);
  ZuAssert(Count <= UINT16_MAX);
};
template <typename V>
consteval unsigned valueAlign() {
  if constexpr (V::Kind == ValueKind::Vector) return V::Elem::Align;
  else return V::Align;
}

template <typename F>
struct FieldWire : public FieldValue_<F> {
  using V = FieldValue_<F>;
  enum { Key = ZuTypeIn<ZuFieldProp::FB::Key, typename F::Props>{} };
  enum { Required = ZuTypeIn<ZuFieldProp::Required, typename F::Props>{} ||
    (Key && V::Kind == ValueKind::String) };
  ZuAssert(!Key || (!V::Mapped &&
    (V::Kind == ValueKind::Scalar || V::Kind == ValueKind::String)));
  enum { ForcedAlign = ZuFieldProp::FB::align<F>() };
  enum { InStruct = IsStruct<ZuUnder<typename F::O>>{} };
  ZuAssert(!Required || (ZuFieldActive<F>{} && V::Kind != ValueKind::Scalar));
  ZuAssert(!ForcedAlign || InStruct || V::Kind == ValueKind::Vector);
  static consteval unsigned alignment() {
    // A struct member's annotation aligns its enclosing wire struct. It
    // does not change the member's canonical natural offset within it.
    if constexpr (InStruct) return V::Align;
    else return ForcedAlign > valueAlign<V>() ? ForcedAlign : valueAlign<V>();
  }
};

template <typename V, typename = void> struct IsKeyWire : public ZuFalse { };
template <typename V>
struct IsKeyWire<V, decltype(V::Key, void())> : public ZuBool<V::Key> { };

template <typename F>
consteval auto defaultValue() {
  // Canonical scalar defaults are schema constants. Keep Zf's resolution,
  // including Deflt and its existing fallback, as the only source.
  using T = typename FieldWire<F>::WireType;
  return T(FieldWire<F>::toWire(F::deflt()));
}

template <ZuString Name>
template <typename O>
struct AsStruct<Name>::Layout {
  using Fields = ZuFields<O>;
  static consteval unsigned forcedAlign() {
    unsigned n = 0;
    ZuUnroll::all<Fields>([&n]<typename F>() {
      if (n < FieldWire<F>::ForcedAlign) n = FieldWire<F>::ForcedAlign;
    });
    return n;
  }
  enum { ForcedAlign = forcedAlign() };
  static consteval unsigned naturalAlign() {
    unsigned n = 1;
    ZuUnroll::all<Fields>([&n]<typename F>() {
      using V = FieldWire<F>;
      ZuAssert(ZuFieldActive<F>{}); // inline structs have no deprecated slots
      ZuAssert(!V::Required);
      ZuAssert((!ZuFieldProp::HasValue<typename F::Props, ZuFieldProp::FB::FieldID>{}));
      ZuAssert(V::Kind == ValueKind::Scalar || V::Kind == ValueKind::Inline ||
	V::Kind == ValueKind::Array);
      if (n < V::alignment()) n = V::alignment();
    });
    return n;
  }
  enum { NaturalAlign = naturalAlign() };
  ZuAssert(!ForcedAlign || ForcedAlign >= NaturalAlign);
  enum { Align = ForcedAlign > NaturalAlign ? ForcedAlign : NaturalAlign };

  template <typename F>
  static consteval unsigned offset() {
    unsigned n = 0;
    ZuUnroll::all<Fields>([&n]<typename G>() {
      if constexpr (ZuTypeIndex<G, Fields>{} <= ZuTypeIndex<F, Fields>{}) {
	constexpr unsigned a = FieldWire<G>::alignment();
	n = (n + a - 1) & ~(a - 1);
	if constexpr (!ZuIsSame<F, G>{}) n += FieldWire<G>::Size;
      }
    });
    return n;
  }
  static consteval unsigned size() {
    unsigned n = 0;
    ZuUnroll::all<Fields>([&n]<typename F>() {
      n = offset<F>() + FieldWire<F>::Size;
    });
    return (n + Align - 1) & ~(Align - 1);
  }
  enum { Size = size() };
  ZuAssert(Size); // canonical structs cannot be empty
};

template <typename V, unsigned Kind_ = V::Kind>
struct WireDeps { using T = ZuTypeList<>; };
template <typename V>
struct WireDeps<V, ValueKind::Table> { using T = ZuTypeList<typename V::Native>; };
template <typename V>
struct WireDeps<V, ValueKind::Inline> { using T = ZuTypeList<typename V::Native>; };
template <typename V>
struct WireDeps<V, ValueKind::Union> { using T = ZuTypeList<typename V::Native>; };
template <typename V>
struct WireDeps<V, ValueKind::Vector> : public WireDeps<typename V::Elem> { };
template <typename V>
struct WireDeps<V, ValueKind::Array> : public WireDeps<typename V::Elem> { };
template <typename F> using WireFieldDeps = typename WireDeps<FieldWire<F>>::T;
template <typename A>
using WireAltDeps = typename WireDeps<Value<typename A::T, ZfFieldTC::UDT>>::T;

// One compile-time traversal supplies storage requirements and schema
// reachability. The latter adds enum dependencies through its Children policy.
template <typename Pending, typename Seen,
  template <typename> class Children> struct Reach;
template <typename O, typename Tail, typename Seen,
  template <typename> class Children, bool = ZuTypeIn<O, Seen>{}>
struct ReachOne : public Reach<Tail, Seen, Children> { };
template <typename O, typename Tail, typename Seen, template <typename> class Children>
struct ReachOne<O, Tail, Seen, Children, false> : public Reach<
  ZuTypeConcat<Tail, typename Children<O>::T>, typename Seen::template Push<O>, Children> { };
template <typename O, typename ...Tail, typename Seen, template <typename> class Children>
struct Reach<ZuTypeList<O, Tail...>, Seen, Children> :
  public ReachOne<O, ZuTypeList<Tail...>, Seen, Children> { };
template <typename Seen, template <typename> class Children>
struct Reach<ZuTypeList<>, Seen, Children> { using T = Seen; };

template <typename O, typename Facet>
struct BufferChildren {
  using T = typename As<O>::template Handler<O, Facet>::Children;
};

template <typename Root, typename Facet = ZuFacet::FB>
struct BufferAlign {
  template <typename O> using Children = BufferChildren<O, Facet>;
  // Retain the root projection: original IDs determine wire slots, while
  // selected fields determine the storage requirement of this buffer.
  using Types = typename Reach<ZuTypeList<Root>, ZuTypeList<>, Children>::T;
  static consteval unsigned value() {
    unsigned n = Wire::Align;
    ZuUnroll::all<Types>([&n]<typename O>() {
      if constexpr (!IsUnion<ZuUnder<O>>{}) {
	ZuUnroll::all<ZuActiveFields<O, Facet>>([&n]<typename F>() {
	  if (n < FieldWire<F>::alignment()) n = FieldWire<F>::alignment();
	});
	if constexpr (IsStruct<ZuUnder<O>>{})
	  if (n < As<O>::template Handler<ZuUnder<O>>::Align)
	    n = As<O>::template Handler<ZuUnder<O>>::Align;
      }
    });
    return n;
  }
  enum { N = value() };
};

// --- borrowed field access and verification

template <typename O, typename Facet = ZuFacet::FB> class View;
template <typename V> class VectorValue;
template <typename V> auto valueAt(const uint8_t *);

template <typename U>
class UnionView {
public:
  UnionView(unsigned tag, const uint8_t *data) : m_data{data}, m_tag{uint8_t(tag)} { }
  unsigned tag() const { return m_tag; }
  const uint8_t *data() const { return m_data; }
  template <unsigned Tag>
  auto get() const {
    using Types = typename As<U>::Types;
    ZuAssert(Tag && Tag <= Types::N);
    ZmAssert(m_tag == Tag);
    using Alt = ZuType<Tag - 1, Types>;
    return valueAt<Value<typename Alt::T, ZfFieldTC::UDT>>(m_data);
  }

private:
  const uint8_t *m_data;
  uint8_t m_tag;
};

template <typename V>
inline constexpr bool IsOffset = V::Kind == ValueKind::String ||
  V::Kind == ValueKind::Vector || V::Kind == ValueKind::Table;

template <typename V>
class VectorValue : public VectorView {
public:
  using VectorView::VectorView;
  using Elem = decltype(valueAt<V>(ZuDeclVal<const uint8_t *>()));
  using const_iterator = ZuIter<const VectorValue, Elem>;
  const_iterator begin() const { return {*this, 0}; }
  const_iterator end() const { return {*this, length()}; }
  auto operator [](unsigned i) const {
    auto p = data() + i * V::Size;
    if constexpr (IsOffset<V>) p = follow(p);
    return valueAt<V>(p);
  }
};

template <typename V, unsigned N>
class ArrayValue {
public:
  explicit ArrayValue(const uint8_t *data) : m_data{data} { }
  unsigned length() const { return m_data ? N : 0; }
  const uint8_t *data() const { return m_data; }
  auto operator [](unsigned i) const { return valueAt<V>(m_data + i * V::Size); }
  using Elem = decltype(valueAt<V>(ZuDeclVal<const uint8_t *>()));
  using const_iterator = ZuIter<const ArrayValue, Elem>;
  const_iterator begin() const { return {*this, 0}; }
  const_iterator end() const { return {*this, length()}; }

private:
  const uint8_t *m_data;
};

template <typename V, typename F>
struct ViewField {
  template <typename Base>
  struct Adapter : public Base {
    using O = V;
    using Props = typename F::Props;
    using T = decltype(ZuDeclVal<const V &>().template get<F>());
    enum { ReadOnly = true };
    enum { SourceSave = ZfFieldFilter::Save<F>{} };
    static auto get(const V &v) { return v.template get<F>(); }
  };
  using T = typename F::template Adapt<Adapter>;
};

template <typename V>
auto valueAt(const uint8_t *p) {
  if constexpr (V::Kind == ValueKind::Scalar)
    return V::fromWire(read<typename V::WireType>(p));
  else if constexpr (V::Kind == ValueKind::String) {
    if constexpr (V::Code_ == ZfFieldTC::CString)
      return V::fromWire(VectorView{p}.string().data());
    else
      return V::fromWire(VectorView{p}.string());
  } else if constexpr (V::Kind == ValueKind::Vector) {
    if constexpr (V::Code_ == ZfFieldTC::Bytes)
      return V::fromWire(VectorView{p}.bytes());
    else
      return V::fromWire(VectorValue<typename V::Elem>{p});
  }
  else if constexpr (V::Kind == ValueKind::Array)
    return ArrayValue<typename V::Elem, V::Count>{p};
  else if constexpr (V::Kind == ValueKind::Table || V::Kind == ValueKind::Inline)
    return V::fromWire(View<typename V::Native>{p});
  else
    ZuAssert(V::Kind != ValueKind::Union); // unions use paired tag/value access
}

template <typename O, typename Facet>
class View {
  friend View ZuStructured_(View *);
  friend ZuUnder<O> ZuUnderType(View *);
  friend ZuTrue ZuStructRO_(View *);
  friend ZfStructPrint ZuPrintType(View *);
  template <typename F>
  using Adapt = typename ViewField<View, F>::T;
  template <typename F>
  using Fields = ZuTypeMap<Adapt, ZuActiveFields<O, F>>;
  template <typename F>
  friend Fields<F> ZuFields_(View *, F *);

public:
  using Metadata = typename As<O>::template Handler<O, Facet>;
  View() = default;
  explicit View(const uint8_t *p) : m_data{p} { }
  explicit View(TableView view) : m_data{view.data()} { }
  const uint8_t *data() const { return m_data; }
  bool operator !() const { return !m_data; }
  ZuOpBool

  template <typename F>
  ZuInline const uint8_t *field() const {
    return Metadata::template field<F>(m_data);
  }
  template <typename F>
  bool has() const { return Metadata::template field<F>(m_data); }

  template <typename F>
  auto get() const {
    using V = FieldWire<F>;
    auto p = Metadata::template field<F>(m_data);
    if constexpr (V::Kind == ValueKind::Scalar) {
      using T = typename V::WireType;
      return V::fromWire(p ? read<T>(p) : defaultValue<F>());
    } else if constexpr (V::Kind == ValueKind::Union) {
      unsigned tag = TableView{m_data}.template scalar<uint8_t>(
	Metadata::template id<F>() - 1, 0);
      return V::fromWire(UnionView<typename V::Native>{tag, p ? follow(p) : nullptr});
    } else {
      if constexpr (V::Kind == ValueKind::Inline && V::Mapped) {
	using T = decltype(valueAt<V>(p));
	if (!p) return T(F::deflt());
      }
      if constexpr (IsOffset<V>) { if (p) p = follow(p); }
      return valueAt<V>(p);
    }
  }

private:
  const uint8_t *m_data = nullptr;
};

template <typename O, typename Facet = ZuFacet::FB>
View<O, Facet> view(const ZuBSpan &bytes) {
  return View<O, Facet>{root<Config<Facet>::SizePrefixed>(bytes)};
}

template <typename V>
bool verifyValue(Verifier &, unsigned, unsigned = valueAlign<V>());

template <typename V>
bool verifyValue(Verifier &v, unsigned at, unsigned alignment) {
  if constexpr (V::Kind == ValueKind::Scalar || V::Kind == ValueKind::Array)
    return v.range(at, V::Size, alignment);
  else if constexpr (V::Kind == ValueKind::String)
    return v.string(at);
  else if constexpr (V::Kind == ValueKind::Vector) {
    using E = typename V::Elem;
    unsigned n;
    if (!v.vector(at, E::Size, alignment, n)) return false;
    if constexpr (IsOffset<E>) {
      for (unsigned i = 0; i < n; ++i) {
	unsigned elem = at + sizeof(Offset) + i * E::Size;
	if (!v.offset_(elem, elem)) return false;
	if (!verifyValue<E>(v, elem)) return false;
      }
    }
    return true;
  } else if constexpr (V::Kind == ValueKind::Table || V::Kind == ValueKind::Inline)
    return As<typename V::Native>::template Handler<typename V::Native>::verify(v, at);
  else {
    ZuAssert(V::Kind != ValueKind::Union);
    return false;
  }
}

// Canonical string keys compare as C strings; scalar keys use wire ordering.
// No semantic validation is performed here or during materialization.
template <typename V, typename L, typename R>
int mapKeyCmp(const L &l, const R &r) {
  if constexpr (V::Kind == ValueKind::String) {
    ZuCSpan a{l}, b{r};
    return strcmp(a.data() ? a.data() : "", b.data() ? b.data() : "");
  } else return l < r ? -1 : l > r ? 1 : 0;
}

template <typename O>
class MapView : public VectorValue<typename As<O>::template Value<O>::Elem> {
  using Base = VectorValue<typename As<O>::template Value<O>::Elem>;
public:
  using Entry = typename As<O>::template Entry<typename O::Key, typename O::Val>;
  using Base::Base;
  MapView() = default;
  explicit MapView(Base v) : Base{v} { }

  auto key(unsigned i) const { return (*this)[i].template get<typename Entry::KF>(); }
  auto value(unsigned i) const { return (*this)[i].template get<typename Entry::VF>(); }

  // Like canonical LookupByKey, this assumes sorted input. Loading a map does
  // not use this path and accepts entries in any order, including duplicates.
  template <typename K>
  View<Entry> find(const K &key_) const {
    unsigned lo = 0, hi = this->length();
    while (lo < hi) {
      unsigned mid = lo + ((hi - lo)>>1);
      int cmp = mapKeyCmp<FieldWire<typename Entry::KF>>(key(mid), key_);
      if (cmp < 0) lo = mid + 1;
      else if (cmp > 0) hi = mid;
      else return (*this)[mid];
    }
    return View<Entry>{};
  }
};

// --- typed object handlers

template <typename T, typename Facet, typename U>
auto nativeValue(const U &value) {
  if constexpr (IsMapView<U>{}) {
    using O = ObjectType<T>;
    using H = typename As<O>::template Handler<O, Facet>;
    if constexpr (ZuTraits<T>::IsPointer) return value ? H{value}.alloc() : nullptr;
    else return H{value}.ctor();
  } else if constexpr (IsView<U>{}) {
    using O = ObjectType<T>;
    using H = typename As<O>::template Handler<O, Facet>;
    if constexpr (ZuTraits<T>::IsPointer)
      return value ? H{{View<O, Facet>{value.data()}}}.alloc() : nullptr;
    else
      return H{{View<O, Facet>{value.data()}}}.ctor();
  } else return value;
}

// Materialize table elements only when the destination requests native
// objects. Ordinary borrowed vector access never constructs those objects.
template <typename T, typename Facet, typename V>
class NativeArray {
public:
  explicit NativeArray(V value) : m_value{value} { }
  unsigned length() const { return m_value.length(); }
  auto operator [](unsigned i) const { return nativeValue<T, Facet>(m_value[i]); }
  using Elem = decltype(nativeValue<T, Facet>(ZuDeclVal<typename V::Elem>()));
  using const_iterator = ZuIter<const NativeArray, Elem>;
  const_iterator begin() const { return {*this, 0}; }
  const_iterator end() const { return {*this, length()}; }

private:
  V m_value;
};

template <typename O, typename Facet>
struct ObjectHandler {
  View<O, Facet> view;

  using Root = ZuUnder<O>;
  using Complete = ZuFields<Root>;
  using Selected = ZuFields<O, Facet>;
  using AllFields = ZuActiveFields<O, Facet>;

  template <typename F>
  struct Find {
    template <typename G>
    using Match = ZuBool<ZuString{F::id()} == ZuString{G::id()}>;
    using Matches = ZuTypeGrep<Match, Selected>;
    using Candidates = typename Matches::template Push<F>;
    using T = ZuType<0, Candidates>;
  };
  template <typename F> using Effective = typename Find<F>::T;
  // Facets can rename/select fields, but never renumber the complete schema.
  using SchemaFields = ZuTypeMap<Effective, Complete>;

  static consteval bool namesUnique() {
    using namespace ZuFieldProp::FB;
    bool ok = true;
    ZuUnroll::all<SchemaFields>([&ok]<typename F>() {
      ZuUnroll::all<SchemaFields>([&ok]<typename G>() {
	if constexpr (ZuTypeIndex<F, SchemaFields>{} < ZuTypeIndex<G, SchemaFields>{}) {
	  if (name<F>() == name<G>()) ok = false;
	  if constexpr (IsUnion<typename F::T>{})
	    if (name<F>() + "_type"_z == name<G>()) ok = false;
	  if constexpr (IsUnion<typename G::T>{})
	    if (name<F>() == name<G>() + "_type"_z) ok = false;
	}
      });
    });
    return ok;
  }
  ZuAssert(namesUnique());

private:
  template <typename F> using CtorIndex = ZuFieldProp::GetCtor<typename F::Props>;
  using CtorFields = ZuTypeSort<CtorIndex, ZuTypeGrep<ZfFieldFilter::Ctor, AllFields>>;
  using InitFields = ZuTypeGrep<ZfFieldFilter::Init, AllFields>;

  template <typename F>
  auto loadField() const {
    using W = FieldWire<F>;
    using V = decltype(view.template get<F>());
    if constexpr (IsMapView<V>{})
      return nativeValue<typename F::T, Facet>(view.template get<F>());
    else if constexpr (IsView<V>{}) {
      auto value = view.template get<F>();
      if constexpr (W::Kind == ValueKind::Inline &&
	!ZuIsSame<decltype(F::deflt()), void>{})
	if (!value) return typename F::T(F::deflt());
      return nativeValue<typename F::T, Facet>(value);
    } else if constexpr ((W::Kind == ValueKind::Vector ||
	W::Kind == ValueKind::Array) && W::Code_ != ZfFieldTC::Bytes) {
      using T = typename ZuTraits<typename F::T>::Elem;
      if constexpr (IsView<typename V::Elem>{})
	return NativeArray<T, Facet, V>{view.template get<F>()};
      else return view.template get<F>();
    } else if constexpr (W::Kind == ValueKind::Union && !W::Mapped)
      return As<typename W::Native>::template Handler<typename W::Native, Facet>::
	template ctor<typename F::T>(view.template get<F>());
    else return view.template get<F>();
  }

  template <typename ...F>
  struct Construct {
    template <typename ...Args>
    static O ctor(const ObjectHandler &h, Args &&...args) {
      return O{ZuFwd<Args>(args)..., h.template loadField<F>()...};
    }
    template <typename ...Args>
    static O *alloc(const ObjectHandler &h, Args &&...args) {
      return new O{ZuFwd<Args>(args)..., h.template loadField<F>()...};
    }
    template <typename ...Args>
    static void new_(void *ptr, const ObjectHandler &h, Args &&...args) {
      new (ptr) O{ZuFwd<Args>(args)..., h.template loadField<F>()...};
    }
  };
  using Ctor = ZuTypeApply<Construct, CtorFields>;

  template <typename List>
  void apply(O &o) const {
    if constexpr (List::N)
      ZuUnroll::all<List>([this, &o]<typename F>() {
	if constexpr (!F::ReadOnly) F::set(o, this->template loadField<F>());
      });
  }

public:
  template <bool ReadOnly = ZuStructRO<O>{}, typename = ZuIfT<!ReadOnly>,
    typename ...Args>
  O ctor(Args &&...args) const {
    if constexpr (!InitFields::N)
      return Ctor::ctor(*this, ZuFwd<Args>(args)...);
    else {
      O o = Ctor::ctor(*this, ZuFwd<Args>(args)...);
      apply<InitFields>(o);
      return o;
    }
  }
  template <bool ReadOnly = ZuStructRO<O>{}, typename = ZuIfT<!ReadOnly>,
    typename ...Args>
  O *alloc(Args &&...args) const {
    auto o = Ctor::alloc(*this, ZuFwd<Args>(args)...);
    apply<InitFields>(*o);
    return o;
  }
  template <bool ReadOnly = ZuStructRO<O>{}, typename = ZuIfT<!ReadOnly>,
    typename ...Args>
  void new_(void *ptr, Args &&...args) const {
    Ctor::new_(ptr, *this, ZuFwd<Args>(args)...);
    apply<InitFields>(*static_cast<O *>(ptr));
  }
  template <template <typename> class Filter = ZfFieldFilter::Load,
    bool ReadOnly = ZuStructRO<O>{}, typename = ZuIfT<!ReadOnly>>
  void load(O &o) const { apply<ZuTypeGrep<Filter, AllFields>>(o); }
  template <bool ReadOnly = ZuStructRO<O>{}, typename = ZuIfT<!ReadOnly>>
  void update(O &o) const { apply<ZuTypeGrep<ZfFieldFilter::Upd, AllFields>>(o); }
};

template <typename U>
struct UnionOffset {
  Offset offset;
  uint8_t tag;
};

template <ZuString Name>
template <typename O, typename Facet>
class AsTable<Name>::Handler : public ObjectHandler<O, Facet> {
  using Base = ObjectHandler<O, Facet>;
  using ID = IDs<typename Base::Complete>;
public:
  using typename Base::Root;
  using typename Base::Complete;
  using typename Base::AllFields;
  using typename Base::SchemaFields;
  template <typename F> using Effective = typename Base::template Effective<F>;
  enum { N = ID::N };
  using Children = ZuTypeApply<ZuTypeConcat, ZuTypeMap<WireFieldDeps, AllFields>>;

  template <typename F>
  static consteval unsigned id() {
    using Find = typename Base::template Find<F>;
    using Matches = ZuTypeGrep<Find::template Match, Complete>;
    ZuAssert(Matches::N == 1);
    return ID::template id<ZuType<0, Matches>>();
  }
  template <typename F>
  ZuInline static const uint8_t *field(const uint8_t *p) {
    return TableView{p}.field(id<F>());
  }
  template <template <typename> class Filter = ZfFieldFilter::All>
  static bool verify(Verifier &, unsigned);
  template <template <typename> class Filter = ZfFieldFilter::Save, typename B>
  static unsigned save(B &, const O &);
  template <typename F, typename B, typename T, typename Table>
  static void field(B &, Table &, const T &);
  template <typename F, typename B, typename Table>
  static void fieldOffset(B &, Table &, Offset);
  template <typename F, typename B, typename U, typename Table>
  static void fieldUnion(B &, Table &, UnionOffset<U>);
};

template <ZuString Name>
template <typename O, typename Facet>
class AsStruct<Name>::Handler : public ObjectHandler<O, Facet>,
  public Layout<ZuUnder<O>> {
  using Base = ObjectHandler<O, Facet>;
  using L = Layout<ZuUnder<O>>;
public:
  using typename Base::Root;
  using typename Base::AllFields;
  using typename Base::SchemaFields;
  using Children = ZuTypeApply<ZuTypeConcat, ZuTypeMap<WireFieldDeps, AllFields>>;
  template <typename F>
  static consteval unsigned id() {
    using Find = typename Base::template Find<F>;
    using Complete = typename Base::Complete;
    using Matches = ZuTypeGrep<Find::template Match, Complete>;
    ZuAssert(Matches::N == 1);
    return ZuTypeIndex<ZuType<0, Matches>, Complete>{};
  }
  template <typename F>
  ZuInline static const uint8_t *field(const uint8_t *p) {
    using CoreField = ZuType<id<F>(), typename Base::Complete>;
    return p ? p + L::template offset<CoreField>() : nullptr;
  }
  template <template <typename> class Filter = ZfFieldFilter::All>
  static bool verify(Verifier &v, unsigned at) {
    return v.range(at, L::Size, L::Align);
  }
  static void save(uint8_t *, const O &);
};

template <ZuString Name, typename ...Alts>
template <typename U, typename Facet>
struct AsUnion<Name, Alts...>::Handler {
  UnionView<U> view;

  using Wire = As<U>;
  using Types = typename Wire::Types;
  using Children = ZuTypeApply<ZuTypeConcat, ZuTypeMap<WireAltDeps, Types>>;
  template <typename T = U>
  ZuInline T ctor() const { return ctor<T>(view); }
  template <typename T = U>
  static T ctor(UnionView<U>);
  template <template <typename> class Filter = ZfFieldFilter::Save,
    typename B, typename T>
  static UnionOffset<U> save(B &, const T &);
  template <bool Required>
  static bool verify(Verifier &, const TableBounds &, unsigned, unsigned);
};

template <ZuString Name, typename ...Alts>
template <typename U, typename Facet>
template <typename T>
T AsUnion<Name, Alts...>::Handler<U, Facet>::ctor(UnionView<U> value) {
  T out;
  if (!value.tag()) return out;
  using Types = typename As<U>::Types;
  ZuSwitch::dispatch<Types::N>(value.tag() - 1, [&out, value](auto i) {
    using E = typename T::template Type<i + 1>;
    out.template p<i + 1>(nativeValue<E, Facet>(value.template get<i + 1>()));
  });
  return out;
}

template <ZuString Name>
template <typename O, typename Facet>
template <template <typename> class Filter>
bool AsTable<Name>::Handler<O, Facet>::verify(Verifier &v, unsigned at) {
  using Fields = ZuTypeGrep<ZuFieldActive, SchemaFields>;
  if (!v.enter()) return false;
  TableBounds bounds;
  bool ok = ZuUnroll::all<Fields>(v.table(at, bounds),
    [&v, &bounds]<typename F>(bool ok) {
	if (!ok) return false;
	using V = FieldWire<F>;
	constexpr bool required = V::Required && bool(Filter<F>{});
	unsigned field;
	if (!v.field(bounds, id<F>(), V::Size, V::Align, field))
	  return false;
	if constexpr (V::Kind == ValueKind::Union)
	  return As<typename V::Native>::template Handler<typename V::Native>::
	    template verify<required>(v, bounds, id<F>(), field);
	else {
	  if (!field) return !required;
	  if constexpr (IsOffset<V>)
	    return v.offset_(field, field) &&
	      verifyValue<V>(v, field, V::alignment());
	  return true;
	}
    });
  v.leave();
  return ok;
}

template <ZuString Name, typename ...Alts>
template <typename U, typename Facet>
template <bool Required>
bool AsUnion<Name, Alts...>::Handler<U, Facet>::verify(
  Verifier &v, const TableBounds &bounds, unsigned id, unsigned field)
{
  unsigned tagAt;
  if (!v.field(bounds, id - 1, 1, 1, tagAt)) return false;
  unsigned tag = tagAt ? read<uint8_t>(v.data() + tagAt) : 0;
  if (tag > Types::N || (Required && !tag)) return false;
  if (!field) return !tag && !Required;
  if (!v.offset_(field, field)) return false;
  if (!tag) return true;
  return ZuSwitch::dispatch<Types::N>(tag - 1, [&v, field](auto i) {
    using Alt = ZuType<i, Types>;
    using W = ZiFB::Value<typename Alt::T, ZfFieldTC::UDT>;
    return verifyValue<W>(v, field);
  });
}

// --- object save

ZuDerive(Offsets, (ZtArray<Offset, ZtArrayHeapID<"ZiFB.Offsets">>));

// Bound inline offset storage to one platform cache line. Slot counts are
// fixed by metadata; larger schemas retain ZtScratch with its named heap fallback.
template <ZuString Name>
template <unsigned N, typename F>
decltype(auto) AsTable<Name>::withOffsets(F &&fn) {
  enum { Builtin = Zm::CacheLineSize/sizeof(Offset) };
  if constexpr (N <= Builtin) {
    // ZuArray requires nonzero capacity even for a zero-field table.
    ZuArray<Offset, N ? N : 1> offsets(N, false);
    return ZuFwd<F>(fn)(offsets);
  } else {
    auto offsets = ZtScratch(Offsets, N, N);
    return ZuFwd<F>(fn)(offsets);
  }
}

template <ZuString Name>
template <typename O, typename Facet>
void AsStruct<Name>::Handler<O, Facet>::save(uint8_t *p, const O &o) {
  unsigned end = 0;
  ZuUnroll::all<typename L::Fields>([p, &o, &end]<typename F>() {
    using V = FieldWire<F>;
    constexpr unsigned at = L::template offset<F>();
    if (at > end) memset(p + end, 0, at - end);
    if constexpr (V::Kind == ValueKind::Scalar)
      write<typename V::WireType>(p + at, V::toWire(F::get(o)));
    else if constexpr (V::Kind == ValueKind::Array) {
      using E = typename V::Elem;
      const auto &value = F::get(o);
      for (unsigned i = 0; i < V::Count; ++i) {
	if constexpr (E::Kind == ValueKind::Scalar)
	  write<typename E::WireType>(p + at + i * E::Size, E::toWire(value[i]));
	else
	  As<typename E::Native>::template Handler<typename E::Native>::save(
	    p + at + i * E::Size, E::toWire(value[i]));
      }
    }
    else
      As<typename V::Native>::template Handler<typename V::Native>::save(
	  p + at, V::toWire(F::get(o)));
    end = at + V::Size;
  });
  if (end < L::Size) memset(p + end, 0, L::Size - end);
}

template <typename V, typename B, typename T, typename = void>
struct CanSaveTo : public ZuFalse { };
template <typename V, typename B, typename T>
struct CanSaveTo<V, B, T, decltype(V::Map::saveTo(
  ZuDeclVal<B &>(), ZuDeclVal<const T &>()), void())> : public ZuTrue { };

// Operation filters select fields of the containing row. Each selected
// field is a complete value, including table/vector/union components.
// Explicit All retains synthetic members throughout the object graph.
template <template <typename> class Filter>
struct AllValues : public ZuFalse { };
template <>
struct AllValues<ZfFieldFilter::All> : public ZuTrue { };

template <typename V, typename Facet, template <typename> class Filter,
  typename B, typename T>
unsigned saveValue(B &builder, const T &native, unsigned alignment = valueAlign<V>()) {
  if constexpr (IsMapWire<V>{}) {
    using O = typename V::MapType;
    using H = typename As<O>::template Handler<O, Facet>;
    if constexpr (ZuTraits<T>::IsPointer)
      return native ? H::template saveEntries<Filter>(builder, *native) : 0;
    else return H::template saveEntries<Filter>(builder, native);
  } else if constexpr (CanSaveTo<V, B, T>{}) {
    return V::Map::saveTo(builder, native);
  } else {
    const auto &value = V::toWire(native);
    using W = ZuDecay<decltype(value)>;
    if constexpr (V::Kind == ValueKind::String) {
      ZuCSpan text{value};
      if constexpr (IsKeyWire<V>{}) return builder.string(text);
      else return text.data() ? builder.string(text) : 0;
    } else if constexpr (V::Kind == ValueKind::Table) {
      auto save = [&builder](const auto &o) {
	// Once the native field is selected, its transformed representation
	// is complete. Root key/update filters do not prune helper internals.
	if constexpr (V::Mapped)
	  return As<typename V::Native>::
	    template Handler<typename V::Native, ZuFacet::FB>::
	      template save<ZfFieldFilter::All>(builder, o);
	else if constexpr (AllValues<Filter>{})
	  return As<typename V::Native>::template Handler<typename V::Native, Facet>::
	    template save<ZfFieldFilter::All>(builder, o);
	else
	  return As<typename V::Native>::template Handler<typename V::Native, Facet>::
	    template save<ZfFieldFilter::Save>(builder, o);
      };
      if constexpr (ZuTraits<W>::IsPointer)
	return value ? save(*value) : 0;
      else
	return save(value);
    } else if constexpr (V::Kind == ValueKind::Vector) {
      using E = typename V::Elem;
      if constexpr (ZuTraits<W>::IsSpan)
	if (!ZuTraits<W>::data(value)) return 0;
      auto count = ZuTraits<W>::length(value);
      if (count >= Wire::MaxSize/E::Size) {
	builder.fail(Error::Capacity);
	return 0;
      }
      if constexpr (E::Kind == ValueKind::Scalar || E::Kind == ValueKind::Inline) {
	return builder.vector(count, E::Size, alignment,
	  [&value, count](uint8_t *p) {
	    if constexpr (E::Kind == ValueKind::Scalar) {
	      using Scalar = typename E::WireType;
	      using Native = ZuDecay<typename ZuTraits<W>::Elem>;
	      // Contiguous scalars already in wire representation need one copy.
	      // Width conversion, mapped values and byte swapping retain the loop.
	      if constexpr (!E::Mapped && ZuTraits<W>::IsSpan &&
		  (ZuIsSame<Native, ZuLittleEndian<Scalar>>{} ||
		    (E::Size == 1 && sizeof(Native) == 1 && ZuTraits<Native>::IsIntegral))) {
		if (count) memcpy(p, ZuTraits<W>::data(value), count * E::Size);
	      } else
		for (unsigned i = 0; i < count; ++i)
		  write<typename E::WireType>(p + i * E::Size, E::toWire(value[i]));
	    } else
	      for (unsigned i = 0; i < count; ++i)
		As<typename E::Native>::template Handler<typename E::Native>::save(
		  p + i * E::Size, E::toWire(value[i]));
	  });
      } else {
	// An empty offset vector has no children or temporary offsets.
	if (!count) return builder.vector(0, sizeof(Offset), alignment, [](uint8_t *) { });
	auto offsets = ZtScratch(Offsets, count, count);
	for (unsigned i = 0; i < count; ++i) {
	  if constexpr (E::Kind == ValueKind::String)
	    offsets[i] = builder.string(value[i]); // an empty element is a string
	  else
	    offsets[i] = saveValue<E, Facet, Filter>(builder, value[i]);
	  if (builder.failed()) return 0;
	  // Canonical object vectors have no null elements. Absence belongs
	  // to the field containing the vector.
	  if (!offsets[i]) { builder.fail(Error::Required); return 0; }
	}
	return builder.offsets(offsets, alignment);
      }
    } else {
      ZuAssert(V::Kind == ValueKind::String || V::Kind == ValueKind::Table ||
	V::Kind == ValueKind::Vector);
      return 0;
    }
  }
}

template <ZuString Name, typename ...Alts>
template <typename U, typename Facet>
template <template <typename> class Filter, typename B, typename T>
UnionOffset<U> AsUnion<Name, Alts...>::Handler<U, Facet>::save(B &builder, const T &value) {
  unsigned tag = Wire::tag(value);
  if (!tag) return {0, 0};
  if (tag > Types::N) { builder.fail(Error::Union); return {0, 0}; }
  unsigned offset = ZuSwitch::dispatch<Types::N>(tag - 1,
    [&builder, &value](auto i) {
      using Alt = ZuType<i, Types>;
      using V = ZiFB::Value<typename Alt::T, ZfFieldTC::UDT>;
      const auto &payload = Wire::template value<i + 1>(value);
      if constexpr (V::Kind == ValueKind::Inline) {
	builder.align(V::Align, V::Size);
	if (auto p = builder.push(V::Size)) {
	  As<typename V::Native>::template Handler<typename V::Native>::
	    save(p, V::toWire(payload));
	  return builder.size();
	}
	return 0U;
      } else {
	ZuAssert(V::Kind == ValueKind::Table);
	return saveValue<V, Facet, Filter>(builder, payload);
      }
    });
  if (!offset) builder.fail(Error::Union);
  return {offset, uint8_t(tag)};
}

// Typed field composition shares object-save defaults and inline layout.
// Children are built before startTable; fieldOffset attaches their offsets.
template <ZuString Name>
template <typename O, typename Facet>
template <typename F, typename B, typename T, typename Table>
void AsTable<Name>::Handler<O, Facet>::field(B &builder, Table &table, const T &value) {
  using V = FieldWire<F>;
  constexpr unsigned id = Handler::template id<F>();
  if constexpr (V::Kind == ValueKind::Scalar)
    builder.template field<typename V::WireType>(table, id,
      V::toWire(value), defaultValue<F>());
  else {
    ZuAssert(V::Kind == ValueKind::Inline);
    builder.fieldInline(table, id, V::Size, V::alignment(),
      [&value](uint8_t *p) {
	As<typename V::Native>::template Handler<typename V::Native>::
	  save(p, V::toWire(value));
      });
  }
}

template <ZuString Name>
template <typename O, typename Facet>
template <typename F, typename B, typename Table>
void AsTable<Name>::Handler<O, Facet>::fieldOffset(B &builder, Table &table, Offset value) {
  using V = FieldWire<F>;
  ZuAssert((V::Kind == ValueKind::String || V::Kind == ValueKind::Vector ||
    V::Kind == ValueKind::Table));
  if constexpr (V::Required)
    if (!value) { builder.fail(Error::Required); return; }
  builder.fieldOffset(table, Handler::template id<F>(), value);
}

// Compose already-built union payloads without a temporary native union.
template <ZuString Name>
template <typename O, typename Facet>
template <typename F, typename B, typename U, typename Table>
void AsTable<Name>::Handler<O, Facet>::fieldUnion(
  B &builder, Table &table, UnionOffset<U> value)
{
  using V = FieldWire<F>;
  ZuAssert(V::Kind == ValueKind::Union);
  ZuAssert((ZuIsSame<typename V::Native, U>{}));
  if (value.tag > As<U>::Types::N || bool(value.tag) != bool(value.offset)) {
    builder.fail(Error::Union);
    return;
  }
  if constexpr (V::Required) {
    if (!value.offset) { builder.fail(Error::Required); return; }
  }
  constexpr unsigned id = Handler::template id<F>();
  builder.template field<uint8_t>(table, id - 1, value.tag, 0);
  builder.fieldOffset(table, id, value.offset);
}

template <ZuString Name>
template <typename F>
struct AsTable<Name>::IsNestedField : public ZuBool<FieldWire<F>::Kind == ValueKind::String ||
  FieldWire<F>::Kind == ValueKind::Vector || FieldWire<F>::Kind == ValueKind::Table ||
  FieldWire<F>::Kind == ValueKind::Union> { };

template <ZuString Name>
template <typename O, typename Facet>
template <template <typename> class Filter, typename B>
unsigned AsTable<Name>::Handler<O, Facet>::save(B &builder, const O &o) {
  ZuAssert(!IsView<O>{}, "copy a complete serialized span with copyRoot");
  using Fields = ZuTypeGrep<Filter, AllFields>;
  using Nested = ZuTypeGrep<IsNestedField, Fields>;
  if constexpr (Fields::N <= 1 && !Nested::N) {
    // A scalar-only singleton has no child scratch to reuse.
    auto table = builder.startTable();
    ZuUnroll::all<Fields>([&builder, &table, &o]<typename F>() {
      field<F>(builder, table, F::get(o));
    });
    return builder.endTable(table);
  } else {
    // Bound scratch by the highest selected slot, rather than the full row.
    enum { Count = []() consteval {
      unsigned n = 0;
      ZuUnroll::all<Fields>([&n]<typename F>() {
	constexpr unsigned end = Handler::template id<F>() + 1;
	if (n < end) n = end;
      });
      return n;
    }() };
    return withOffsets<Count>([&builder, &o](auto &offsets) {
      ZuUnroll::all<Complete>([&offsets]<typename F>() {
	using Effective = typename Base::template Effective<F>;
	constexpr unsigned id = Handler::template id<F>();
	if constexpr (id < Count && !ZuTypeIn<Effective, Fields>{}) {
	  offsets[id] = 0;
	  if constexpr (FieldWire<F>::Kind == ValueKind::Union) offsets[id - 1] = 0;
	}
      });
      ZuUnroll::all<Nested>([&builder, &o, &offsets]<typename F>() {
	using V = FieldWire<F>;
	unsigned offset;
	if constexpr (V::Kind == ValueKind::Union) {
	  auto value = As<typename V::Native>::template Handler<typename V::Native, Facet>::
	    template save<Filter>(builder,
	    V::toWire(F::get(o)));
	  offset = value.offset;
	  offsets[Handler::template id<F>() - 1] = value.tag;
	} else
	  offset = saveValue<V, Facet, Filter>(builder, F::get(o), V::alignment());
	offsets[Handler::template id<F>()] = offset;
      });
      auto table = builder.startTable(offsets.data());
      ZuUnroll::all<Fields>([&builder, &table, &o, &offsets]<typename F>() {
	using V = FieldWire<F>;
	if constexpr (V::Kind == ValueKind::Scalar || V::Kind == ValueKind::Inline) {
	  field<F>(builder, table, F::get(o));
	} else if constexpr (V::Kind == ValueKind::Union) {
	  fieldUnion<F>(builder, table, UnionOffset<typename V::Native>{
	    offsets[Handler::template id<F>()], uint8_t(offsets[Handler::template id<F>() - 1])});
	} else if constexpr (IsNestedField<F>{}) {
	  fieldOffset<F>(builder, table, offsets[Handler::template id<F>()]);
	} else {
	  ZuAssert(V::Kind != ValueKind::Union);
	}
      });
      return builder.endTable(table);
    });
  }
}

// Synthetic entry/root fields are always retained. Application values still
// use the ordinary Save filter, unless the caller explicitly requests All.
template <typename F> struct MapFields : public ZuTrue { };

template <ZuString Name, unsigned KC, unsigned VC, typename KP, typename VP>
template <typename O, typename Facet>
class AsMap<Name, KC, VC, KP, VP>::Handler :
  public AsTable<Name>::template Handler<Root<O>, Facet> {
  using R = Root<O>;
  using Base = typename AsTable<Name>::template Handler<R, Facet>;
public:
  using E = Entry<typename O::Key, typename O::Val>;
  using EH = typename AsTable<Name + "Entry"_z>::template Handler<E, Facet>;
  using Val = typename O::Val;
  using Key = typename O::Key;
  using Base::verify;

  explicit Handler(View<O, Facet> v) :
    Base{{View<R, Facet>{v.data()}}},
    m_entries{View<R, Facet>{v.data()}.template get<typename R::F>()} { }
  explicit Handler(MapView<O> entries) :
    Base{{View<R, Facet>{}}}, m_entries{entries} { }
  MapView<O> entries() const { return m_entries; }

  template <template <typename> class Filter = ZfFieldFilter::Save, typename B>
  static unsigned saveEntries(B &b, const O &o) {
    // Count by traversal: the interface does not require count(), and iteration
    // may select fewer entries than a container's physical count. The caller
    // owns the container and keeps it stable for the complete serialization.
    unsigned count = 0;
    {
      auto i = o.citer();
      while (auto node = i()) {
	if (++count >= Wire::MaxSize/sizeof(Offset)) {
	  b.fail(Error::Capacity);
	  return 0;
	}
      }
    }
    auto offsets = ZtScratch(Offsets, count, count);
    unsigned n = 0;
    {
      auto i = o.citer();
      while (auto node = i()) {
	E e{node->key(), node->val()};
	if constexpr (AllValues<Filter>{})
	  offsets[n++] = EH::template save<ZfFieldFilter::All>(b, e);
	else offsets[n++] = EH::template save<MapFields>(b, e);
	if (b.failed()) return 0;
      }
    }
    // Offsets are end-relative, so growth while constructing entries cannot
    // invalidate scratch or borrowed comparisons. Sort the serialized keys.
    auto key = [&b](Offset at) {
      return View<E>{b.data() + b.size() - at}.template get<typename E::KF>();
    };
    ZuSort(offsets.data(), n, [key](Offset a, Offset c) {
      return mapKeyCmp<FieldWire<typename E::KF>>(key(a), key(c));
    });
    return b.offsets(offsets);
  }
  template <template <typename> class Filter = ZfFieldFilter::Save, typename B>
  static unsigned save(B &b, const O &o) {
    auto entries_ = saveEntries<Filter>(b, o);
    auto table = b.startTable();
    b.fieldOffset(table, 0, entries_);
    return b.endTable(table);
  }

  O ctor() const {
    O o;
    load_(o);
    return o;
  }
  O *alloc() const {
    auto o = new O();
    load_(*o);
    return o;
  }
  void new_(void *ptr) const { load_(*new (ptr) O()); }
  void load(O &o) const {
    if constexpr (RawVal) {
      auto i = o.citer();
      while (auto node = i()) release(node);
    }
    o.clean();
    load_(o);
  }
  void update(O &o) const { load_(o); }

private:
  enum { RawVal = ZuTraits<Val>::IsPointer && ZuTraits<Val>::IsPrimitive };
  template <typename Node>
  static void release(Node *node) {
    // Clear the slot before releasing the pointee, so node destruction (which
    // may itself own raw values) sees null. Smart pointers release normally.
    auto &slot = const_cast<Val &>(node->val());
    auto p = slot;
    slot = nullptr;
    delete p;
  }
  Val value(unsigned i) const {
    using W = FieldWire<typename E::VF>;
    auto v = m_entries.value(i);
    if constexpr (IsView<decltype(v)>{} || IsMapView<decltype(v)>{})
      return nativeValue<Val, Facet>(v);
    else if constexpr (W::Kind == ValueKind::Union && !W::Mapped)
      return As<typename W::Native>::template Handler<typename W::Native, Facet>::
	template ctor<Val>(v);
    else if constexpr (W::Kind == ValueKind::Vector && W::Code_ != ZfFieldTC::Bytes) {
      using T = typename ZuTraits<Val>::Elem;
      if constexpr (IsView<typename decltype(v)::Elem>{})
	return Val{NativeArray<T, Facet, decltype(v)>{v}};
      else return Val{v};
    } else return Val{v};
  }
  void load_(O &o) const {
    for (unsigned i = 0, n = m_entries.length(); i < n; ++i) {
      Key key{m_entries.key(i)};
      Val val = value(i);
      {
	auto old = o.del(key);
	if constexpr (RawVal) { if (old) release(&*old); }
      }
      o.add(ZuMv(key), ZuMv(val));
    }
  }

  MapView<O> m_entries;
};

template <typename B>
unsigned Values::HwBitmapMap::saveTo(B &b, const ZmBitmap &v) {
    int last = v.last();
    if (last < 0 && !!v) { b.fail(Error::Value); return 0; }
    unsigned n = last < 0 ? 0 : (unsigned(last)>>6) + 1;
    auto data = b.vector(n, sizeof(uint64_t), alignof(uint64_t),
      [&v, n](uint8_t *p) {
	for (unsigned i = 0; i < n; ++i) {
	  uint64_t word = 0;
	  ZuUnroll::all<Parts>([&v, i, &word](auto j) {
	    word |= uint64_t(hwloc_bitmap_to_ith_ulong(v, i * Parts + j)) <<
	      (j * (sizeof(unsigned long)<<3));
	  });
	  write<uint64_t>(p + i * sizeof(uint64_t), word);
	}
      });
    auto table = b.startTable();
    b.fieldOffset(table,
      As<Bitmap>::template Handler<Bitmap>::template id<ZfField(Bitmap, data)>(), data);
    return b.endTable(table);
  }

// --- pooled I/O construction and address mapping

// The array bounds are the protocol address widths, independent of native
// socket structure layout. Addresses retain network byte order on the wire.
struct IPv4 { uint8_t addr[4]; };
ZfStruct(ZiAPI, (IPv4, FB), (addr,, Bytes));
ZiFB::AsStruct<"Zi.IPv4"> ZiFB_Fmt(IPv4 *);
struct IPv6 { uint8_t addr[16]; };
ZfStruct(ZiAPI, (IPv6, FB), (addr,, Bytes));
ZiFB::AsStruct<"Zi.IPv6"> ZiFB_Fmt(IPv6 *);

struct IP : public ZiFB::AsUnion<"Zi.IP",
    ZiFB::Alt<"IPv4", IPv4>, ZiFB::Alt<"IPv6", IPv6>> {
  static unsigned tag(const ZiIP &v) { return v.type(); }
  template <unsigned I>
  static auto value(const ZiIP &v) {
    ZuAssert(I == ZiIP::V4 || I == ZiIP::V6);
    if constexpr (I == ZiIP::V4) {
      IPv4 out;
      memcpy(out.addr, &v.inAddr(), sizeof(out.addr));
      return out;
    } else {
      IPv6 out;
      memcpy(out.addr, &v.in6Addr(), sizeof(out.addr));
      return out;
    }
  }
};

struct IPMap {
  using T = ZiIP;
  enum { Code = ZfFieldTC::UDT };
  static const ZiIP &save(const ZiIP &v) { return v; }
  template <typename V>
  static ZiIP load(const V &v) {
    switch (v.tag()) {
      case ZiIP::V4: {
	in_addr addr;
	memcpy(&addr, v.template get<1>().template get<
	  ZfField(IPv4, addr)>().data(), sizeof(addr));
	return ZiIP{addr};
      }
      case ZiIP::V6: {
	in6_addr addr;
	memcpy(&addr, v.template get<2>().template get<
	  ZfField(IPv6, addr)>().data(), sizeof(addr));
	return ZiIP{addr};
      }
      default: return {};
    }
  }
};

// Exact growth preserves the arena's end-relative offsets.
inline unsigned ioGrow_(unsigned, unsigned n) { return n; }

template <unsigned Align>
class IOStorage {
  ZuAssert(Align >= Wire::Align && Align <= Wire::MaxAlign);
  ZuAssert(!(Align & (Align - 1)) && !(ZiIOBuf_Align % Align));
public:
  explicit IOStorage(
    ZmRef<ZiIOBuf> buf, unsigned align = Align,
    unsigned maxSize = UINT32_MAX, unsigned tailRoom = 0)
  :
    m_align{align}, m_maxSize{maxSize}, m_tailRoom{tailRoom}
  {
    ZmAssert(align >= Wire::Align && align <= Align && !(align & (align - 1)));
    attach(ZuMv(buf));
  }

  uint8_t *data() const { return m_buf ? m_buf->data() : nullptr; }
  unsigned size() const {
    if (!m_buf) return 0;
    unsigned end = m_buf->size < m_maxSize ? m_buf->size : m_maxSize;
    unsigned head = m_buf->skip;
    return end > uint64_t(head) + m_tailRoom ?
      (end - head - m_tailRoom) & ~(m_align - 1) : 0;
  }
  bool canGrow(unsigned n) const {
    return m_buf && uint64_t(n) + m_buf->skip + m_tailRoom <= UINT32_MAX;
  }
  uint8_t *grow(unsigned oldSize, unsigned newSize,
    unsigned head, unsigned tail) {
    ZmAssert(canGrow(newSize));
    return m_buf->template realloc<ioGrow_>(oldSize + m_tailRoom,
      newSize + m_tailRoom, head, tail ? tail + m_tailRoom : 0);
  }
  const ZiIOBuf *buffer() const { return m_buf.ptr(); }

  void attach(ZmRef<ZiIOBuf> buf) {
    if (buf) {
      ZmAssert(!(buf->skip & (m_align - 1)));
      ZmAssert(!(uintptr_t(buf->data()) & (m_align - 1)));
      ZmAssert(buf->skip <= buf->size);
      buf->length = 0;
    }
    m_buf = ZuMv(buf);
  }

  ZmRef<ZiIOBuf> detach(unsigned capacity, unsigned length) {
    if (!m_buf) return nullptr;
    unsigned limit = size();
    // Construction scratch can force growth even when the final frame fits.
    if (length <= limit && capacity > limit) {
      memmove(data() + limit - length, data() + capacity - length, length);
      capacity = limit;
    }
    auto buf = ZuMv(m_buf);
    buf->skip += capacity - length;
    buf->length = length;
    return buf;
  }

private:
  ZmRef<ZiIOBuf>	m_buf;
  unsigned		m_align;
  unsigned		m_maxSize;
  unsigned		m_tailRoom;
};

template <unsigned Align_ = ZiFB::Wire::Align>
class IOBuilder : private IOStorage<Align_>,
  public ZiFB::Builder<IOStorage<Align_>> {
  using Storage = IOStorage<Align_>;
  using Base = ZiFB::Builder<Storage>;
public:
  enum { Align = Align_ };

  explicit IOBuilder(
    ZmRef<ZiIOBuf> buf, unsigned align = Align,
    unsigned maxSize = UINT32_MAX, unsigned tailRoom = 0)
  :
    Storage{ZuMv(buf), align, maxSize, tailRoom}, Base{*this, align} { }

  IOBuilder(const IOBuilder &) = delete;
  IOBuilder &operator =(const IOBuilder &) = delete;
  IOBuilder(IOBuilder &&) = delete;
  IOBuilder &operator =(IOBuilder &&) = delete;

  using Base::size;
  using Base::data;
  using Base::failed;
  using Base::capacity;

  // Detaching transfers the existing buffer owner. The arena owns no
  // separate allocation, and is reset before any subsequent use.
  ZmRef<ZiIOBuf> buf() {
    if (failed()) return nullptr;
    auto buf = Storage::detach(capacity(), size());
    Base::reset();
    return buf;
  }
  void buf(ZmRef<ZiIOBuf> buf) {
    Storage::attach(ZuMv(buf));
    Base::reset();
  }
  const ZiIOBuf *cspan() const { return Storage::buffer(); }
};

// A native container used as a root retains its canonical mapped table's
// schema and codec. Loading materializes through the existing conversion.
template <typename Map>
struct AsMapped : public As<typename Map::T> {
  ZuAssert(As<typename Map::T>::Kind == Kind::Table);
  template <typename O, typename Facet = ZuFacet::FB>
  class Handler : public As<typename Map::T>::template Handler<typename Map::T, Facet> {
    using Wire = typename Map::T;
    using Base = typename As<Wire>::template Handler<Wire, Facet>;
  public:
    explicit Handler(View<O, Facet> value) : Base{{View<Wire, Facet>{value.data()}}} { }
    O ctor() const { return O{Map::load(this->view)}; }
    O *alloc() const { return new O{Map::load(this->view)}; }
    void new_(void *ptr) const { new (ptr) O{Map::load(this->view)}; }
    void load(O &o) const { o = Map::load(this->view); }
    void update(O &o) const {
      if constexpr (ZuTypeGrep<ZfFieldFilter::Upd, typename Base::AllFields>::N)
	load(o);
    }
    template <template <typename> class Filter = ZfFieldFilter::Save, typename B>
    static unsigned save(B &builder, const O &o) {
      return Base::template save<Filter>(builder, Map::save(o));
    }
  };
};

// --- load/save entry points

// Detect Builder and derived builders without erasing the caller's type.
template <typename Storage> ZuTrue IsBuilder_(Builder<Storage> *);
ZuFalse IsBuilder_(...);
template <typename T>
using IsBuilder = decltype(IsBuilder_(ZuDeclVal<T *>()));

// Compose into an existing arena; finishing and ownership stay with the caller.
template <typename Facet = ZuFacet::FB,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename B, typename O, typename = ZuIfT<IsBuilder<B>{}>,
  typename = decltype(As<O>::Kind)>
ZuInline auto save(B &builder, const O &o) {
  return As<O>::template Handler<O, Facet>::template save<Filter>(builder, o);
}

// Write an inline wire struct directly into supplied destination storage.
template <typename O>
ZuInline void save(uint8_t *p, const O &o) {
  As<O>::template Handler<O>::save(p, o);
}

template <typename O, typename Facet = ZuFacet::FB,
  template <typename> class Filter = ZfFieldFilter::All>
bool verify(const ZuBSpan &bytes, const char *id = nullptr, VerifyLimits limits = {}) {
  Verifier verifier{bytes, limits};
  unsigned at;
  return verifier.template root<Config<Facet>::SizePrefixed>(at, id) &&
    As<O>::template Handler<O, Facet>::template verify<Filter>(verifier, at);
}

template <typename O, typename Facet>
ZuInline auto handler(const View<O, Facet> &value) {
  return typename As<O>::template Handler<O, Facet>{{value}};
}

template <typename U, typename Facet = ZuFacet::FB>
ZuInline auto handler(const UnionView<U> &value) {
  return typename As<U>::template Handler<U, Facet>{value};
}

template <typename O, typename Facet = ZuFacet::FB>
auto handler(const ZuBSpan &bytes) {
  return typename As<O>::template Handler<O, Facet>{{view<O, Facet>(bytes)}};
}

// Generic streams need only byte-span insertion. Transmit streams build in
// detached output storage and flush it. Return codes report codec errors;
// destination failures remain in S. Failed construction emits no object bytes.
template <typename Facet = ZuFacet::FB,
  template <typename> class Filter = ZfFieldFilter::Save, typename S, typename O,
  typename = ZuIfT<!IsBuilder<S>{} && !ZuIsSame<ZuDecay<S>, uint8_t *>{}>>
int save(S &stream, const O &o, const char *id = nullptr) {
  ZuAssert(As<ZuUnder<O>>::Kind == Kind::Table);
  constexpr unsigned Align = BufferAlign<O, Facet>::N;
  auto save_ = [&o, id]<typename B, typename Emit>(B &builder, Emit emit) {
    auto bytes = builder.template finish<Config<Facet>::SizePrefixed>(
      As<O>::template Handler<O, Facet>::template save<Filter>(builder, o), id);
    if (builder.failed()) return builder.error();
    emit(bytes);
    return int(Error::OK);
  };
  if constexpr (ZiIsTxStream<S>{}) {
    unsigned head = (stream.headRoom() + Align - 1) & ~(Align - 1);
    auto buf = stream.allocBuf(head);
    if (!buf) return Error::OK;
    unsigned tail = stream.tailRoom();
    unsigned maxSize = stream.maxSize();
    unsigned capacity = maxSize > uint64_t(head) + tail ?
      (maxSize - head - tail) & ~(Align - 1) : 0;
    IOBuilder<Align> builder{ZuMv(buf), Align, maxSize, tail};
    return save_(builder, [&stream, &builder, capacity](ZuBSpan bytes) {
      if (bytes.length() <= capacity)
	stream << builder.buf() << Zi::Flush{};
      else {
	// Oversized objects retain the stream's ordinary frame splitting.
	stream << bytes << Zi::Flush{};
      }
    });
  } else {
    using Storage = ArrayStorage<Align>;
    using B = Builder<Storage>;
    auto buf = ZtScratch(Buffer, B::Initial + Align - 1);
    Storage storage{buf};
    B builder{storage, Align};
    return save_(builder, [&stream](ZuBSpan bytes) { stream << bytes; });
  }
}

// Loading is for trusted bytes: verify external data once at its ingress
// boundary. Borrowed strings/vectors in O retain the input span's lifetime.
template <typename O, typename Facet = ZuFacet::FB,
  typename ...Args>
O load(const ZuBSpan &bytes, Args &&...args) {
  return handler<O, Facet>(bytes).ctor(ZuFwd<Args>(args)...);
}

} // ZiFB

ZiFB::IP ZiFB_Fmt(ZiIP *);
ZiFB::IPMap ZiFBTransform(ZiIP *);
inline ZuCSpan ZfVFieldTypeID(ZiIP *) { return "IP"; }

#endif /* ZiFB_HH */
