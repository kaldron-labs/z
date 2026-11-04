//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixTxModel_HH
#define ZfixTxModel_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <string.h>
#include <zlib/ZuTuple.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZfixFields.hh>

namespace Zfix::TxModel_ {

// Only unformatted variable values live here. Positions survive storage growth;
// neither a caller's views nor a partially encoded FIX message is retained.
struct Bytes { unsigned offset = 0, length = 0; };
struct Arena {
  explicit Arena(Fmt::Limits cfg) : limits{cfg} { }
  Bytes put(ZuBSpan bytes, int32_t tag) {
    if (error) return {};
    unsigned used = data ? data->length : 0;
    if (bytes.length() > limits.frameBytes - used) {
      fail(Fmt::ErrorCode::Size, tag); return {};
    }
    if (!bytes.length()) return {used, 0};
    if (!data) data = new Buffer{nullptr, 0};
    if (!data->ensure(used + bytes.length())) {
      fail(Fmt::ErrorCode::Size, tag); return {};
    }
    memcpy(data->data() + used, bytes.data(), bytes.length());
    data->length = used + bytes.length();
    return {used, unsigned(bytes.length())};
  }
  ZuBSpan get(Bytes bytes) const {
    return {data ? data->data() + bytes.offset : nullptr, bytes.length};
  }
  bool group(uint64_t n, int32_t tag) {
    if (depth == limits.groupDepth) { fail(Fmt::ErrorCode::Depth, tag); return false; }
    if (n > limits.groupEntries - entries) { fail(Fmt::ErrorCode::Size, tag); return false; }
    entries += n; ++depth; return true;
  }
  void fail(unsigned code, int32_t tag) { if (!error) error = {code, tag, 0}; }
  uint64_t memory() const { return data ? data->memory() : 0; }
  Fmt::Limits limits;
  Fmt::Error error;
  unsigned entries = 0, depth = 0;
  using Buffer = Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
    ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.TxModel.Raw">>;
  ZmRef<ZiIOBuf> data;
};

template <typename Array> uint64_t arrayMemory(const Array &array) {
  if (!array.size()) return 0;
  using Heap = typename Array::VHeap;
  uint64_t bytes = array.size() * sizeof(typename Array::T) + alignof(typename Array::T);
  auto i = Heap::cacheI(bytes);
  return i < Heap::NCaches ? Heap::blockSize(i) : bytes;
}

// Opaque scalar formatters must declare an owning snapshot rather than silently
// copying a possibly borrowed C++ object. The specialization supplies capture,
// emit<Properties>(out, arena), and memory; its state remains inline in the tuple.
template <typename T, typename Facet> struct ScalarOwn;

template <unsigned Code, typename T, typename Facet, typename Enable = void>
struct Value {
  using Stored = ZfFieldTC::Type<Code>;
  Stored value;
  void capture(const T &source, Arena &, int32_t) { value = Stored(source); }
  template <typename Props, typename Out> void emit(Out &out, const Arena &) const {
    Fmt::saveScalar<Facet, Code, Props>(out, value);
  }
  uint64_t memory() const { return 0; }
};

template <unsigned Code, typename T, typename Facet>
struct Value<Code, T, Facet, ZuIfT<Code == ZfFieldTC::String ||
  Code == ZfFieldTC::CString || Code == ZfFieldTC::Bytes>> {
  Bytes value;
  void bytes(ZuBSpan source, Arena &arena, int32_t tag) { value = arena.put(source, tag); }
  void capture(const T &source, Arena &arena, int32_t tag) {
    if constexpr (Code == ZfFieldTC::Bytes) bytes(ZuBSpan{source}, arena, tag);
    else bytes(ZuBSpan{ZuCSpan{source}}, arena, tag);
  }
  template <typename Props, typename Out> void emit(Out &out, const Arena &arena) const {
    if constexpr (Code == ZfFieldTC::Bytes)
      Fmt::saveScalar<Facet, Code, Props>(out, arena.get(value));
    else Fmt::saveScalar<Facet, Code, Props>(out, ZuCSpan{arena.get(value)});
  }
  uint64_t memory() const { return 0; }
};

template <unsigned Code, typename T, typename Facet> struct List {
  using Elem = Fmt::GroupElem<T>;
  using Item = Value<Code, Elem, Facet>;
  ZtArray<Item, ZtArrayHeapID<"Zfix.TxModel.List">> values;
  void capture(const T &source, Arena &arena, int32_t tag) {
    auto n = ZuTraits<T>::length(source);
    if (n > uint64_t(arena.limits.fieldBytes) + 1) {
      arena.fail(Fmt::ErrorCode::Size, tag); return;
    }
    values.length(n);
    for (unsigned i = 0; i < n && !arena.error; ++i)
      values[i].capture(source[i], arena, tag);
  }
  template <typename Props, typename Out> void emit(Out &out, const Arena &arena) const {
    for (unsigned i = 0; i < values.length() && !out.failed(); ++i) {
      if (i) out << ' ';
      values[i].template emit<Props>(out, arena);
    }
  }
  uint64_t memory() const {
    uint64_t total = arrayMemory(values);
    for (const auto &value : values) total += value.memory();
    return total;
  }
};

template <unsigned Code, typename T, typename Facet>
struct Value<Code, T, Facet, ZuIfT<ZfFieldTC::IsVec<Code>{}>> :
    List<ZfFieldTC::Elem<Code>{}, T, Facet> { };

template <typename T, typename Facet, bool Multiple = Fmt::Multiple<Fmt::As<T>>{}>
struct Scalar : ScalarOwn<T, Facet> { };

template <typename T, typename Facet> struct Scalar<T, Facet, true> :
    List<Fmt::As<T>::ElemCode, T, Facet> {
  template <typename Props, typename Out> void emit(Out &out, const Arena &arena) const {
    List<Fmt::As<T>::ElemCode, T, Facet>::template emit<typename Fmt::As<T>::ElemProps>(out, arena);
  }
};

template <typename T, typename Facet>
struct Value<ZfFieldTC::UDT, T, Facet, void> : Scalar<T, Facet> { };

struct NoHeader {
  template <typename F, typename Out> bool field(Out &) const { return false; }
};

template <typename O, typename Facet, template <typename> class Filter> class Model;

template <typename F, typename Facet, template <typename> class Filter,
  bool Component = Fmt::Component<F, Facet>{}, bool Group = Fmt::GroupField<F, Facet>{},
  bool Active = Fmt::activeField<F, Facet, Filter>()>
struct Field {
  template <typename O> void capture(const O &, Arena &) { }
  template <typename Out, typename Header>
  void emit(Out &out, const Arena &, const Header &header) const {
    if constexpr (!Component && !Group) header.template field<F>(out);
  }
  bool empty() const { return true; }
  uint64_t memory() const { return 0; }
};

template <typename F, typename Facet, template <typename> class Filter>
struct Field<F, Facet, Filter, false, false, true> {
  Value<F::Type::Code, typename F::T, Facet> value;
  bool present = false;
  template <typename O> void capture(const O &source, Arena &arena) {
    const auto &v = F::get(source);
    if constexpr (ZuFieldProp::FIX::GetOptional<typename F::Props>{}) {
      if constexpr (F::Type::Code != ZfFieldTC::Bool) {
	if constexpr (ZuFieldProp::HasEnum<typename F::Props>{}) { if (v < 0) return; }
	else if constexpr (F::Type::Code == ZfFieldTC::DateTime) { if (!*v) return; }
	else if (ZuNull(v)) return;
      }
    }
    present = true;
    if constexpr (Fmt::Binary<F>{}) {
      auto bytes = Fmt::rawBytes(v);
      if (bytes.length() > arena.limits.fieldBytes) {
	arena.fail(Fmt::ErrorCode::Size, Fmt::tag<F>()); return;
      }
      value.bytes(bytes, arena, Fmt::tag<F>());
    } else {
      if constexpr (F::Type::Code == ZfFieldTC::String || F::Type::Code == ZfFieldTC::CString) {
	if (ZuCSpan{v}.length() > arena.limits.fieldBytes) {
	  arena.fail(Fmt::ErrorCode::Size, Fmt::tag<F>()); return;
	}
      } else if constexpr (F::Type::Code == ZfFieldTC::Bytes) {
	if (Fmt::bytesLength<typename F::Props>(ZuBSpan{v}) > arena.limits.fieldBytes) {
	  arena.fail(Fmt::ErrorCode::Size, Fmt::tag<F>()); return;
	}
      }
      value.capture(v, arena, Fmt::tag<F>());
    }
  }
  template <typename Out, typename Header>
  void emit(Out &out, const Arena &arena, const Header &header) const {
    if (header.template field<F>(out) || !present) return;
    if constexpr (Fmt::Binary<F>{}) Fmt::saveBinary<F>(out, arena.get(value.value));
    else {
      out.field(Fmt::tag<F>());
      value.template emit<typename F::Props>(out, arena);
      out.end();
    }
  }
  bool empty() const { return !present; }
  uint64_t memory() const { return value.memory(); }
};

template <typename F, typename Facet, template <typename> class Filter>
struct Field<F, Facet, Filter, true, false, true> {
  using T = typename F::T;
  using Object = Fmt::ComponentType<F, Facet>;
  Model<Object, Facet, Filter> value;
  bool present = false;
  template <typename O> void capture(const O &source, Arena &arena) {
    const auto &v = F::get(source);
    if constexpr (Fmt::UnionField<F, Facet>{}) {
      if (!v.template is<Object>()) { arena.fail(Fmt::ErrorCode::Unresolved, 0); return; }
      value.capture(v.template p<Object>(), arena);
    } else if constexpr (Fmt::ObjPtr<T>{}) {
      if (!v) {
	if constexpr (ZuTypeIn<ZuFieldProp::Required, typename F::Props>{})
	  arena.fail(Fmt::ErrorCode::Required, 0);
	return;
      }
      value.capture(*v, arena);
    } else value.capture(v, arena);
    present = true;
  }
  template <typename Out, typename Header>
  void emit(Out &out, const Arena &arena, const Header &header) const {
    if (present) value.emit(out, arena, header);
  }
  bool empty() const { return !present || value.empty(); }
  uint64_t memory() const { return value.memory(); }
};

template <typename F, typename Facet, template <typename> class Filter>
struct Field<F, Facet, Filter, false, true, true> {
  using Object = Fmt::GroupElem<typename F::T>;
  using Child = Model<Object, Facet, Fmt::NestedFilter<Filter>::template T>;
  ZtArray<Child, ZtArrayHeapID<"Zfix.TxModel.Group">> values;
  bool present = false;
  template <typename O> void capture(const O &source, Arena &arena) {
    const auto &v = F::get(source);
    if constexpr (ZuFieldProp::FIX::GetOptional<typename F::Props>{})
      if (ZuNull(v)) return;
    auto n = ZuTraits<typename F::T>::length(v);
    if (!arena.group(n, Fmt::tag<F>())) return;
    present = true; values.length(n);
    for (unsigned i = 0; i < n && !arena.error; ++i) values[i].capture(v[i], arena);
    --arena.depth;
  }
  template <typename Out, typename Header>
  void emit(Out &out, const Arena &arena, const Header &) const {
    if (!present || !out.group(values.length())) return;
    out.field(Fmt::tag<F>()); out << ZuBox<uint64_t>{values.length()}; out.end();
    int32_t delimiter = 0;
    for (const auto &value : values) {
      auto offset = out.length(); value.emit(out, arena, NoHeader{}); out.entry(offset, delimiter);
      if (out.failed()) break;
    }
    out.endGroup();
  }
  bool empty() const { return !present; }
  uint64_t memory() const {
    uint64_t total = arrayMemory(values);
    for (const auto &value : values) total += value.memory();
    return total;
  }
};

template <typename O, typename Facet, template <typename> class Filter>
class Model {
  using Fields = typename Fmt::Schema<O, Facet>::Fields;
  template <typename ...F> using Tuple = ZuTuple<Field<F, Facet, Filter>...>;
  using Values = ZuTypeApply<Tuple, Fields>;
public:
  void capture(const O &source, Arena &arena) {
    ZuUnroll::all<Fields>([this, &source, &arena]<typename F>() {
      if (!arena.error) m_values.template p<ZuTypeIndex<F, Fields>{}>().capture(source, arena);
    });
  }
  template <typename Out, typename Header = NoHeader>
  void emit(Out &out, const Arena &arena, const Header &header = {}) const {
    ZuUnroll::all<Fields>([this, &out, &arena, &header]<typename F>() {
      if (!out.failed()) m_values.template p<ZuTypeIndex<F, Fields>{}>().emit(out, arena, header);
    });
  }
  bool empty() const {
    bool empty = true;
    ZuUnroll::all<Fields>([this, &empty]<typename F>() {
      if (!m_values.template p<ZuTypeIndex<F, Fields>{}>().empty()) empty = false;
    });
    return empty;
  }
  uint64_t memory() const {
    uint64_t total = 0;
    ZuUnroll::all<Fields>([this, &total]<typename F>() {
      total += m_values.template p<ZuTypeIndex<F, Fields>{}>().memory();
    });
    return total;
  }
private:
  Values m_values;
};
} // Zfix::TxModel_

#endif /* ZfixTxModel_HH */
