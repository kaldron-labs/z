//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Migration-only comparison with the actual custom pooled canonical builder.

#include <stdlib.h>

#include <iostream>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmScratch.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtArray.hh>
#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>
#include <zlib/ZfFB.hh>
#include <zlib/ZfFBReflect.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZiFB.hh>
#include "iobaseline.hh"

#include "bench_generated.h"

namespace Bench {

struct Row { uint64_t seq; ZuBSpan data; };
ZfStruct(, (Row, JSON),
  (seq, (Ctor<0>, Deflt<0>), UInt64), (data, (Ctor<1>), Bytes));
ZfStructImpl(Row);
ZfFB::Table<"bench.Row"> ZfFBType(Row *);

using Text = ZtString<ZtStringHeapID<"zffbiobench.Text">>;
using Numbers = ZtArray<int64_t, ZtArrayHeapID<"zffbiobench.Numbers">>;
struct Wide {
  uint64_t seq;
  ZuBSpan data;
  Text label;
  Numbers values;
  double price;
  int32_t qty;
  bool active;
};
ZfStruct(, (Wide, JSON),
  (seq, (Ctor<0>, Deflt<0>), UInt64), (data, (Ctor<1>), Bytes),
  (label, (Ctor<2>), String), (values, (Ctor<3>), Int64Vec),
  (price, (Ctor<4>, Deflt<0>), Float),
  (qty, (Ctor<5>, Deflt<0>), Int32), (active, (Ctor<6>), Bool));
ZfStructImpl(Wide);
ZfFB::Table<"bench.Wide"> ZfFBType(Wide *);

struct Point { int64_t x, y; };
ZfStruct(, Point, (x,, Int64), (y,, Int64));
ZfFB::Inline<"bench.Point"> ZfFBType(Point *);
struct Child { uint64_t value; };
ZfStruct(, Child, (value, (Deflt<0>), UInt64));
ZfFB::Table<"bench.Child"> ZfFBType(Child *);
struct Vectors {
  uint64_t seq;
  ZuBSpan data;
  ZuSpan<const Point> points;
  ZuSpan<const Child *> children;
};
ZfStruct(, Vectors,
  (seq, (Deflt<0>), UInt64), (data,, Bytes), (points,, UDT), (children,, UDT));
ZfFB::Table<"bench.Vectors"> ZfFBType(Vectors *);

// Row/Wide borrowed views need canonical adapters. Preserve the old
// declaration-order emission and mapped-vector access, without retaining the
// deleted production adapter framework.
template <typename T>
struct Ref {
  const T *ptr;
  uint64_t seq() const { return ptr->seq(); }
  ZuBSpan data() const {
    auto v = ptr->data();
    return v ? ZuBSpan{v->data(), v->size()} : ZuBSpan{};
  }
  ZuCSpan label() const {
    auto v = ptr->label();
    return v ? ZuCSpan{v->c_str(), v->size()} : ZuCSpan{};
  }
  auto values() const {
    using Vec = flatbuffers::Vector<int64_t>;
    auto v = ptr->values();
    if (!v) return ZfStruct_::Int64Vec{};
    return ZfStruct_::Int64Vec{*const_cast<Vec *>(v), v->size(),
      [](const void *v_, uint64_t i) -> int64_t {
	return static_cast<const Vec *>(v_)->Get(i);
      }};
  }
  double price() const { return ptr->price(); }
  int32_t qty() const { return ptr->qty(); }
  bool active() const { return ptr->active(); }
};
using RowRef = Ref<fbs::Row>;
using WideRef = Ref<fbs::Wide>;
ZfStruct(, (RowRef, JSON),
  ((seq, RdFn), (Deflt<0>), UInt64), ((data, RdFn),, Bytes));
ZfStruct(, (WideRef, JSON),
  ((seq, RdFn), (Deflt<0>), UInt64), ((data, RdFn),, Bytes),
  ((label, RdFn),, String), ((values, RdFn),, Int64Vec),
  ((price, RdFn), (Deflt<0>), Float),
  ((qty, RdFn), (Deflt<0>), Int32), ((active, RdFn),, Bool));
ZfStructImpl(RowRef);
ZfStructImpl(WideRef);

struct Canonical {
  using Builder = IOBuilder;
  template <typename O>
  using Table = ZuIf<ZuIsSame<O, Wide>{}, fbs::Wide, fbs::Row>;
  template <typename O>
  static auto view(ZuBSpan bytes) {
    return Ref<Table<O>>{flatbuffers::GetRoot<Table<O>>(bytes.data())};
  }
  template <typename O>
  static bool verify(ZuBSpan bytes) {
    flatbuffers::Verifier verifier{bytes.data(), bytes.length()};
    return verifier.VerifyBuffer<Table<O>>(nullptr);
  }
  template <typename O>
  static O load(ZuBSpan bytes) {
    auto v = view<O>(bytes);
    O row;
    row.seq = v.seq();
    row.data = v.data();
    if constexpr (ZuIsSame<O, Wide>{}) {
      row.label = v.label();
      row.values = v.values();
      row.price = v.price();
      row.qty = v.qty();
      row.active = v.active();
    }
    return row;
  }
  template <typename O>
  static void save(Builder &b, const O &row) {
    auto data = b.CreateVector(row.data.data(), row.data.length());
    if constexpr (ZuIsSame<O, Wide>{}) {
      auto label = b.CreateString(row.label.data(), row.label.length());
      int64_t *dst;
      auto values = b.CreateUninitializedVector(row.values.length(), &dst);
      for (unsigned i = 0, n = row.values.length(); i < n; ++i)
	dst[i] = flatbuffers::EndianScalar(row.values[i]);
      fbs::WideBuilder table{b};
      table.add_seq(row.seq);
      table.add_data(data);
      table.add_label(label);
      table.add_values(values);
      table.add_price(row.price);
      table.add_qty(row.qty);
      table.add_active(row.active);
      b.Finish(table.Finish());
    } else {
      fbs::RowBuilder table{b};
      table.add_seq(row.seq);
      table.add_data(data);
      b.Finish(table.Finish());
    }
  }
  static uint64_t read(ZuBSpan bytes) {
    return flatbuffers::GetRoot<flatbuffers::Table>(bytes.data())->
      GetField<uint64_t>(4, 0);
  }
};

struct CanonicalVectors : public Canonical {
  static void save(Builder &b, const Vectors &row) {
    auto data = b.CreateVector(row.data.data(), row.data.length());
    fbs::Point *dst;
    auto points = b.CreateUninitializedVectorOfStructs<fbs::Point>(row.points.length(), &dst);
    for (unsigned i = 0, n = row.points.length(); i < n; ++i)
      new (dst + i) fbs::Point{row.points[i].x, row.points[i].y};
    using Offset = flatbuffers::Offset<fbs::Child>;
    unsigned n = row.children.length();
    auto offsets = ZmScratch(Offset, n);
    for (unsigned i = 0; i < n; ++i)
      offsets.data()[i] = fbs::CreateChild(b, row.children[i]->value);
    auto children = b.CreateVector(offsets.data(), n);
    fbs::VectorsBuilder table{b};
    table.add_seq(row.seq);
    table.add_data(data);
    table.add_points(points);
    table.add_children(children);
    b.Finish(table.Finish());
  }
};

struct Native {
  using Builder = ZiFB::IOBuilder<>;
  template <typename O>
  static void save(Builder &b, const O &row) {
    b.finish(ZfFB::saveObject(b, row));
  }
  static uint64_t read(ZuBSpan bytes) {
    return ZfFB::root(bytes).scalar<uint64_t>(0, 0);
  }
};

namespace Op { enum {
  Read, Verify, Load, Reflect, VerifyReflected, Print,
  VerifyCanonical, VerifyNative, N
}; }

struct Field {
  const reflection::Field *canonical;
  ZfFB::Reflect::FieldView native;
  unsigned align;
};

// Schema parsing/emission and reflected-field association happen once, before
// timing, just as stores retain their setup-time field handles.
template <typename O>
struct Schemas {
  flatbuffers::Parser canonical;
  Text native;
  const reflection::Schema *schema;
  const reflection::Schema *nativeSchema;
  const reflection::Field *seq;
  ZfFB::Reflect::SchemaView view, canonicalView;
  ZfFB::Reflect::FieldView field;
  // Fixed by the benchmark schema, in native declaration order.
  ZuArray<Field, ZfFB::Meta<O>::N> fields;

  Schemas() {
    const char *decl;
    if constexpr (ZuIsSame<O, Wide>{})
      decl = "namespace Bench.fbs; table Wide { seq:ulong; data:[ubyte];"
	"label:string; values:[long]; price:double; qty:int; active:bool; } root_type Wide;";
    else decl = "namespace Bench.fbs; table Row { seq:ulong; data:[ubyte]; } root_type Row;";
    if (!canonical.Parse(decl)) Zm::exit(1);
    canonical.Serialize();
    schema = reflection::GetSchema(canonical.builder_.GetBufferPointer());
    seq = schema->root_table()->fields()->LookupByKey("seq");
    ZfFB::saveSchema<O>(native);
    view = ZfFB::view<ZfFB::Reflect::Schema>(native);
    nativeSchema = reflection::GetSchema(native.data());
    canonicalView = ZfFB::view<ZfFB::Reflect::Schema>({
      canonical.builder_.GetBufferPointer(), canonical.builder_.GetSize()});
    field = ZfFB::Reflect::field(ZfFB::Reflect::rootObject(view), ZuCSpan{"seq"});
    ZuUnroll::all<ZuFields<O>>([this]<typename F>() {
      constexpr auto name = ZfFB::Meta<O>::template fieldName<F>();
      auto nativeField = ZfFB::Reflect::field(ZfFB::Reflect::rootObject(view), name.cspan());
      fields.push(Field{schema->root_table()->fields()->LookupByKey(name.data()),
	nativeField, ZfFB::Reflect::alignment(nativeField)});
    });
  }
};

// Compare the schema-driven writer primitives used by stores. Field handles
// are cached at setup; conversion to each native value is outside this kernel.
template <typename Codec, typename O>
void write(typename Codec::Builder &b, const O &row, const Schemas<O> &schema)
{
  namespace R = ZfFB::Reflect;
  constexpr bool Native = ZuIsSame<Codec, Bench::Native>{};
  using Fields = ZuFields<O>;
  auto offsets = ZtScratch(ZfFB::Offsets, Fields::N, Fields::N);
  ZuUnroll::all<Fields>([&b, &row, &schema, &offsets]<typename F>() {
    (void)schema;
    using W = ZfFB::FieldWire<F>;
    constexpr unsigned I = ZuTypeIndex<F, Fields>{};
    if constexpr (ZfFB::IsOffset<W>) {
      auto &&value = F::get(row);
      if constexpr (Native) {
	auto align = schema.fields[I].align;
	offsets[I] = ZfFB::saveValue<W, ZuFacet::Core, ZfFieldFilter::All>(
	  b, value, (align > ZfFB::valueAlign<W>() ? align : ZfFB::valueAlign<W>()));
      } else if constexpr (W::Kind == ZfFB::ValueKind::String)
	offsets[I] = b.CreateString(value.data(), value.length()).o;
      else if constexpr (W::Code_ == ZfFieldTC::Bytes)
	offsets[I] = b.CreateVector(value.data(), value.length()).o;
      else {
	using T = ZuStrip<typename ZuTraits<ZuDecay<decltype(value)>>::Elem>;
	T *dst;
	offsets[I] = b.CreateUninitializedVector(value.length(), &dst).o;
	for (unsigned i = 0, n = value.length(); i < n; ++i)
	  dst[i] = flatbuffers::EndianScalar(value[i]);
      }
    }
  });
  auto table = [&b]() {
    if constexpr (Native) return b.startTable();
    else return b.StartTable();
  }();
  ZuUnroll::all<Fields>([&b, &table, &row, &schema, &offsets]<typename F>() {
    (void)table;
    using W = ZfFB::FieldWire<F>;
    constexpr unsigned I = ZuTypeIndex<F, Fields>{};
    if constexpr (W::Kind == ZfFB::ValueKind::Scalar) {
      using T = typename W::WireType;
      auto value = W::toWire(F::get(row));
      if constexpr (Native) R::saveScalar<T>(b, table, schema.fields[I].native, value);
      else {
	auto f = schema.fields[I].canonical;
	auto deflt = [f]() -> T {
	  if constexpr (ZuTraits<T>::IsFloatingPoint) return f->default_real();
	  else return f->default_integer();
	}();
	b.template AddElement<T>(f->offset(), value, deflt);
      }
    } else if constexpr (Native)
      R::saveOffset(b, table, schema.fields[I].native, offsets[I]);
    else b.AddOffset(schema.fields[I].canonical->offset(),
      flatbuffers::Offset<void>{offsets[I]});
  });
  if constexpr (Native) b.finish(b.endTable(table));
  else b.Finish(flatbuffers::Offset<flatbuffers::Table>{b.EndTable(table)});
}

template <typename Codec, unsigned Op, typename O>
uint64_t operation(ZuBSpan bytes, const Schemas<O> &schema, Text &text)
{
  constexpr bool Native = ZuIsSame<Codec, Bench::Native>{};
  if constexpr (Op == Op::Read) return Codec::read(bytes);
  else if constexpr (Op == Op::Verify) {
    if constexpr (Native) return ZfFB::verify<O>(bytes);
    else return Canonical::verify<O>(bytes);
  } else if constexpr (Op == Op::Load) {
    auto row = [bytes]() {
      if constexpr (Native) return ZfFB::load<O>(bytes);
      else return Canonical::load<O>(bytes);
    }();
    // Observe the complete object, including fields outside the checksum.
    __asm__ __volatile__("" : : "g"(&row) : "memory");
    return row.seq + row.data.length();
  } else if constexpr (Op == Op::Reflect) {
    if constexpr (Native)
      return ZfFB::Reflect::scalar<uint64_t>(ZfFB::root(bytes), schema.field);
    else return flatbuffers::GetAnyFieldI(
      *flatbuffers::GetRoot<flatbuffers::Table>(bytes.data()), *schema.seq);
  } else if constexpr (Op == Op::VerifyReflected) {
    if constexpr (Native) return ZfFB::Reflect::verify(schema.view, bytes);
    else return flatbuffers::Verify(*schema.schema, *schema.schema->root_table(),
      bytes.data(), bytes.length());
  } else if constexpr (Op == Op::VerifyCanonical) {
    if constexpr (Native) return ZfFB::Reflect::verify(schema.canonicalView, bytes);
    else return flatbuffers::Verify(*schema.schema, *schema.schema->root_table(),
      bytes.data(), bytes.length());
  } else if constexpr (Op == Op::VerifyNative) {
    if constexpr (Native) return ZfFB::Reflect::verify(schema.view, bytes);
    else return flatbuffers::Verify(*schema.nativeSchema,
      *schema.nativeSchema->root_table(), bytes.data(), bytes.length());
  } else {
    text.clear();
    // Both fixtures expose every source field. Use the same filter on both
    // sides so they also share the underlying JSON value-formatting code.
    if constexpr (Native)
      ZfJSON::save<ZuFacet::JSON, ZfFieldFilter::All>(text, ZfFB::view<O>(bytes));
    else ZfJSON::save<ZuFacet::JSON, ZfFieldFilter::All>(text, Canonical::view<O>(bytes));
    return text.length();
  }
}

template <typename Codec, unsigned Op, typename O>
void measure(ZuCSpan name, ZuCSpan op, ZuBSpan bytes, const Schemas<O> &schema, unsigned count)
{
  auto text = ZtScratch(Text, 0);
  // Warm each operation and its output capacity outside the measured loop.
  auto expected = operation<Codec, Op>(bytes, schema, text);
  uint64_t sum = 0;
  auto begin = Zm::now();
  for (unsigned i = 0; i < count; ++i) {
    // Re-read the input each iteration instead of hoisting pure operations.
    __asm__ __volatile__("" : : : "memory");
    sum += operation<Codec, Op>(bytes, schema, text);
  }
  auto elapsed = Zm::now() - begin;
  if (sum != expected * count) Zm::exit(1);
  std::cout << name << ',' << op << ',' << bytes.length() << ',' << count
    << ',' << elapsed.as_decimal() << ',' << sum << '\n';
}

template <typename O>
static void operations(const O &input, unsigned count, unsigned rounds)
{
  Schemas<O> schema;
  Canonical::Builder builder{new ZiIOBufAlloc<>};
  Canonical::save(builder, input);
  auto owner = builder.buf();
  auto bytes = owner->cspan();
  auto canonicalText = ZtScratch(Text, 0);
  auto nativeText = ZtScratch(Text, 0);
  // Check both operations and exact print output before timing either codec.
  ZuUnroll::all<Op::N>([&schema, bytes, &canonicalText, &nativeText](auto op) {
    auto a = operation<Canonical, op>(bytes, schema, canonicalText);
    auto b = operation<Native, op>(bytes, schema, nativeText);
    if (a != b || ((op == Op::Verify || op == Op::VerifyReflected ||
	op == Op::VerifyCanonical || op == Op::VerifyNative) && !a)) {
      std::cerr << "operation mismatch: " << op << " canonical=" << a
	<< " native=" << b << '\n';
      Zm::exit(1);
    }
  });
  auto expectedText = ZtScratch(Text, 0);
  ZfJSON::save(expectedText, input);
  if (canonicalText != expectedText || nativeText != expectedText) {
    std::cerr << "print mismatch\n"; Zm::exit(1);
  }
  // Validate every constructed field outside timing, including owning arrays.
  canonicalText.clear();
  nativeText.clear();
  ZfJSON::save(canonicalText, Canonical::load<O>(bytes));
  ZfJSON::save(nativeText, ZfFB::load<O>(bytes));
  if (canonicalText != expectedText || nativeText != expectedText) {
    std::cerr << "load mismatch\n"; Zm::exit(1);
  }
  std::cout << "codec,operation,encoded_bytes,count,seconds,checksum\n";
  for (unsigned r = 0; r < rounds; ++r) {
    ZuUnroll::all<Op::N>([&schema, bytes, count, r](auto op) {
      using Names = ZuStringTL<"read", "verify", "load", "reflect", "verify_reflected", "print",
	"verify_canonical_schema", "verify_native_schema">;
      ZuCSpan name = ZuType<op, Names>{}().cspan();
      if (r & 1) {
	measure<Native, op>("native", name, bytes, schema, count);
	measure<Canonical, op>("canonical", name, bytes, schema, count);
      } else {
	measure<Canonical, op>("canonical", name, bytes, schema, count);
	measure<Native, op>("native", name, bytes, schema, count);
      }
    });
  }
}

// Fresh mode measures allocation/growth on every iteration. Reuse mode keeps
// the grown owner, but exercises detach and attach for every transmitted row.
template <typename Codec, bool Reflected = false, typename O>
void run(ZuCSpan name, const O &input, unsigned count, bool fresh,
  const Schemas<O> *schema = nullptr)
{
  ZmRef<ZiIOBuf> owner = new ZiIOBufAlloc<>;
  typename Codec::Builder builder{owner};
  uint64_t sum = 0, bytes = 0;
  unsigned capacity = 0;
  O row = input;
  auto begin = Zm::now();
  for (unsigned i = 0; i < count; ++i) {
    row.seq = i + 1;
    if constexpr (Reflected) write<Codec>(builder, row, *schema);
    else Codec::save(builder, row);
    owner = builder.buf();
    if (!owner) { std::cerr << "builder failed\n"; Zm::exit(1); }
    // Expose the complete serialized output, not only the checksum field.
    __asm__ __volatile__("" : : "g"(owner->data()), "g"(owner->length) : "memory");
    sum += Codec::read(owner->cspan());
    bytes += owner->length;
    capacity = owner->size;
    if (fresh) owner = new ZiIOBufAlloc<>;
    else owner->clear();
    builder.buf(owner);
  }
  auto elapsed = Zm::now() - begin;
  std::cout << name << ',' << (fresh ? "fresh" : "reuse") << ','
    << row.data.length() << ',' << count << ',' << bytes << ',' << capacity
    << ',' << elapsed.as_decimal() << ',' << sum << '\n';
}

template <typename C, bool Reflected = false, typename O>
void samples(const O &input, unsigned count, unsigned rounds,
  const Schemas<O> *schema = nullptr)
{
  std::cout << "codec,mode,payload,count,total_bytes,capacity,seconds,checksum\n";
  for (unsigned r = 0; r < rounds; ++r) {
    // Alternate order to reduce systematic first-run/cache bias. Each row
    // retains its sample so distributions can be inspected without hiding it.
    for (unsigned mode = 0; mode < 2; ++mode) {
      if (r & 1) {
        Bench::run<Bench::Native, Reflected>("native", input, count, mode, schema);
        Bench::run<C, Reflected>("canonical", input, count, mode, schema);
      } else {
        Bench::run<C, Reflected>("canonical", input, count, mode, schema);
        Bench::run<Bench::Native, Reflected>("native", input, count, mode, schema);
      }
    }
  }
}

template <bool Reflected = false, typename O>
void saves(const O &input, unsigned count, unsigned rounds)
{
  // Check the native writer against the independent canonical declaration.
  Schemas<O> schema;
  Native::Builder builder{new ZiIOBufAlloc<>};
  if constexpr (Reflected) write<Native>(builder, input, schema);
  else Native::save(builder, input);
  auto owner = builder.buf();
  if (!owner || !Canonical::verify<O>(owner->cspan())) {
    std::cerr << "save verification failed\n"; Zm::exit(1);
  }
  auto expected = ZtScratch(Text, 0);
  auto actual = ZtScratch(Text, 0);
  ZfJSON::save(expected, input);
  ZfJSON::save<ZuFacet::JSON, ZfFieldFilter::All>(actual, Canonical::view<O>(owner->cspan()));
  if (actual != expected) {
    std::cerr << "save mismatch\nexpected: " << expected << "\nactual: " << actual << '\n';
    Zm::exit(1);
  }
  if constexpr (Reflected) {
    Canonical::Builder peer{new ZiIOBufAlloc<>};
    write<Canonical>(peer, input, schema);
    auto buf = peer.buf();
    if (!buf || !ZfFB::verify<O>(buf->cspan())) Zm::exit(1);
    actual.clear();
    ZfJSON::save<ZuFacet::JSON, ZfFieldFilter::All>(actual, ZfFB::view<O>(buf->cspan()));
    if (actual != expected) Zm::exit(1);
  }
  samples<Canonical, Reflected>(input, count, rounds, &schema);
}

static void vectors(ZuBSpan payload, unsigned count, unsigned rounds)
{
  // Workload size determines both vector lengths; setup is outside timing.
  unsigned n = payload.length()/sizeof(Point);
  using Points = ZtArray<Point, ZtArrayHeapID<"zffbiobench.Points">>;
  using Children = ZtArray<Child, ZtArrayHeapID<"zffbiobench.Children">>;
  using Ptrs = ZtArray<const Child *, ZtArrayHeapID<"zffbiobench.Ptrs">>;
  // Keep zero-length spans present, matching canonical empty vectors.
  unsigned capacity = n ? n : 1;
  auto points = ZtScratch(Points, n, capacity);
  auto children = ZtScratch(Children, n, capacity);
  auto ptrs = ZtScratch(Ptrs, n, capacity);
  for (unsigned i = 0; i < n; ++i) {
    points[i] = {int64_t(i), -int64_t(i)};
    children[i] = {uint64_t(i) + 1};
    ptrs[i] = &children[i];
  }
  Vectors row{42, payload, points, ptrs};
  auto check = [&row, n]<typename Codec>() {
    typename Codec::Builder builder{new ZiIOBufAlloc<>};
    Codec::save(builder, row);
    auto owner = builder.buf();
    if (!owner) Zm::exit(1);
    auto bytes = owner->cspan();
    flatbuffers::Verifier verifier{bytes.data(), bytes.length()};
    if (!verifier.VerifyBuffer<fbs::Vectors>(nullptr) ||
	!ZfFB::verify<Vectors>(bytes)) Zm::exit(1);
    auto canonical = flatbuffers::GetRoot<fbs::Vectors>(bytes.data());
    auto native = ZfFB::view<Vectors>(bytes);
    auto points = native.template get<ZfField(Vectors, points)>();
    auto children = native.template get<ZfField(Vectors, children)>();
    auto cp = canonical->points();
    auto cc = canonical->children();
    auto cd = canonical->data();
    auto data = cd ? ZuBSpan{cd->data(), cd->size()} : ZuBSpan{};
    if (canonical->seq() != row.seq || native.template get<ZfField(Vectors, seq)>() != row.seq ||
	(cp ? cp->size() : 0) != n || (cc ? cc->size() : 0) != n ||
	points.length() != n || children.length() != n ||
	native.template get<ZfField(Vectors, data)>() != row.data || data != row.data)
      Zm::exit(1);
    for (unsigned i = 0; i < n; ++i)
      if (cp->Get(i)->x() != row.points[i].x ||
	  cp->Get(i)->y() != row.points[i].y ||
	  cc->Get(i)->value() != row.children[i]->value ||
	  points[i].template get<ZfField(Point, x)>() != row.points[i].x ||
	  points[i].template get<ZfField(Point, y)>() != row.points[i].y ||
	  children[i].template get<ZfField(Child, value)>() != row.children[i]->value)
	Zm::exit(1);
  };
  check.template operator ()<Native>();
  check.template operator ()<CanonicalVectors>();
  samples<CanonicalVectors>(row, count, rounds);
}

} // Bench

int main(int argc, char **argv)
{
#ifndef NDEBUG
  std::cerr << "Performance measurements require a release build\n";
  return 1;
#endif
  if (argc != 4 && argc != 5) {
    std::cerr << "Usage: zifbiobench COUNT BYTES ROUNDS "
      "[ops|wide|wideops|vectors|write|widewrite]\n";
    return 1;
  }
  unsigned count = ZuBox<unsigned>{argv[1]};
  unsigned size = ZuBox<unsigned>{argv[2]};
  unsigned rounds = ZuBox<unsigned>{argv[3]};
  if (!count || !rounds || size >= (ZiIOBuf_DefltMaxSize>>1)) return 1;
  if (auto value = getenv("ZIFB_BENCH_POOL")) {
    unsigned pool = ZuBox<unsigned>{value};
    if (!pool) return 1;
    ZmHeapMgr::init(ZiIOBuf_HeapID{}(), ZmSelf()->partition(), 0, {pool});
    std::cout << "# fixed_pool=" << pool << '\n';
  }
  // A zero-byte workload still denotes a present empty byte vector.
  auto payload = ZtScratch(ZfFB::Buffer, size, (size ? size : 1));
  for (unsigned i = 0; i < size; ++i) payload[i] = i;
  struct IDs {
    using Keys = ZuStringTL<"ops", "wide", "wideops", "vectors", "write", "widewrite">;
  };
  static constexpr auto matcher = ZuMatcher<IDs>();
  int mode = argc == 5 ? matcher.exact(argv[4]) : -1;
  if (argc == 5 && mode < 0) return 1;
  if (mode == 3) {
    Bench::vectors(payload, count, rounds);
    return 0;
  }
  if (mode == 4) {
    Bench::saves<true>(Bench::Row{42, payload}, count, rounds);
    return 0;
  }
  if (mode == 0) {
    Bench::operations(Bench::Row{42, payload}, count, rounds);
    return 0;
  }
  if (mode > 0) {
    Bench::Wide input{42, payload, "owning label and signed vector benchmark", {}, 12.5, 7, true};
    unsigned n = size/sizeof(int64_t);
    input.values.size(n);
    for (unsigned i = 0; i < n; ++i) input.values.push(int64_t(i) - (n>>1));
    if (mode == 2) Bench::operations(input, count, rounds);
    else if (mode == 5) Bench::saves<true>(input, count, rounds);
    else Bench::saves(input, count, rounds);
  } else Bench::saves(Bench::Row{42, payload}, count, rounds);
}
