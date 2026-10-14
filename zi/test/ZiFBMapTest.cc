//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiFBReflect.hh>

using namespace ZuTestUtil;

ZuStructFacet(MapWire);

namespace MapTest {

template <typename K, typename V, ZuString ID, typename Heap = ZuVoid>
struct Hash_ : public Heap, public ZmHashKV<K, V, ZmHashHeapID<ID>> { };
template <typename K, typename V, ZuString ID>
using HashHeap = ZmHeap<ID + ".Obj"_z, Hash_<K, V, ID>>;
ZuDerive(Hash, (Hash_<int, int, "ZiFBMapTest.Hash",
  HashHeap<int, int, "ZiFBMapTest.Hash">>));
ZuDerive(Tree, (ZmRBTreeKV<int, int, ZmRBTreeHeapID<"ZiFBMapTest.Tree">>));
ZiFB::AsMap<"test.IntMap", ZfFieldTC::Int32, ZfFieldTC::Int32> ZiFB_Fmt(Hash *);
ZiFB::AsMap<"test.IntMap", ZfFieldTC::Int32, ZfFieldTC::Int32> ZiFB_Fmt(Tree *);
ZuDerive(Strings, (Hash_<ZtString<>, ZtString<>, "ZiFBMapTest.Text",
  HashHeap<ZtString<>, ZtString<>, "ZiFBMapTest.Text">>));
ZiFB::AsMap<"test.TextMap", ZfFieldTC::String, ZfFieldTC::String> ZiFB_Fmt(Strings *);
using AltText = ZtString<ZtStringHeapID<"ZiFBMapTest.AltText">>;
ZuDerive(AltStrings, (ZmRBTreeKV<AltText, AltText,
  ZmRBTreeHeapID<"ZiFBMapTest.AltStrings">>));
ZiFB::AsMap<"test.TextMap", ZfFieldTC::String, ZfFieldTC::String> ZiFB_Fmt(AltStrings *);
struct Shared { ZmRef<Strings> hash; AltStrings tree; };
ZfStruct(, (Shared, FB), (hash, (Mutable), UDT), (tree, (Mutable), UDT));
ZiFB::AsTable<"test.SharedMaps"> ZiFB_Fmt(Shared *);

struct Row { ZmRef<Hash> map; Tree tree; };
ZfStruct(, (Row, FB), (map, (Mutable), UDT), (tree, (Mutable), UDT));
ZiFB::AsTable<"test.MapRow"> ZiFB_Fmt(Row *);

static int alive = 0;
template <typename Heap = ZuVoid>
struct Item_ : public Heap {
  int value;
  int omitted = -7;

  explicit Item_(int v = 0) : value{v} { ++alive; }
  ~Item_() { --alive; }
};
using ItemHeap = ZmHeap<"ZiFBMapTest.Item", Item_<>>;
ZuDerive(Item, Item_<ItemHeap>);
ZfStruct(, (Item, FB),
  (value, (Ctor<0>, Deflt<0>), Int32),
  (omitted,, Int32));
ZfStructRender(, Item, MapWire, value);
ZiFB::AsTable<"test.MapItem"> ZiFB_Fmt(Item *);
ZuDerive(Raw, (Hash_<int, Item *, "ZiFBMapTest.Raw",
  HashHeap<int, Item *, "ZiFBMapTest.Raw">>));
ZiFB::AsMap<"test.RawMap", ZfFieldTC::Int32, ZfFieldTC::UDT> ZiFB_Fmt(Raw *);
ZuDerive(Owned, (ZmRBTreeKV<int, ZuPtr<Item>, ZmRBTreeHeapID<"ZiFBMapTest.Owned">>));
ZiFB::AsMap<"test.OwnedMap", ZfFieldTC::Int32, ZfFieldTC::UDT> ZiFB_Fmt(Owned *);

// This wrapper deliberately has no ZuPtr/ZmRef ancestry. Its pointer traits
// and implicit adoption are the entire codec-facing contract.
class Own {
public:
  Own(Item *p = nullptr) : m_ptr{p} { }
  Own(Own &&o) : m_ptr{o.m_ptr} { o.m_ptr = nullptr; }
  ~Own() { delete m_ptr; }
  Own &operator =(Own &&o) {
    if (this != &o) { delete m_ptr; m_ptr = o.m_ptr; o.m_ptr = nullptr; }
    return *this;
  }
  Item &operator *() const { return *m_ptr; }
  Item *operator ->() const { return m_ptr; }
  bool operator !() const { return !m_ptr; }
  ZuOpBool
  struct Traits : public ZuTraits<Item *> { enum { IsPrimitive = 0, IsPOD = 0 }; };
  friend Traits ZuTraitsType(Own *);
private:
  Item *m_ptr;
};
ZuAssert(ZuTraits<Own>::IsPointer);
ZuAssert((ZuIsConvertible<Item *, Own>{}));
ZuDerive(Custom, (ZmRBTreeKV<int, Own, ZmRBTreeHeapID<"ZiFBMapTest.Custom">>));
ZiFB::AsMap<"test.CustomMap", ZfFieldTC::Int32, ZfFieldTC::UDT> ZiFB_Fmt(Custom *);

ZuDerive(Nested, (ZmRBTreeKV<int, Tree, ZmRBTreeHeapID<"ZiFBMapTest.Nested">>));
ZiFB::AsMap<"test.NestedMap", ZfFieldTC::Int32, ZfFieldTC::UDT> ZiFB_Fmt(Nested *);
ZuDerive(RefNested, (ZmRBTreeKV<int, ZmRef<Hash>, ZmRBTreeHeapID<"ZiFBMapTest.RefNested">>));
ZiFB::AsMap<"test.RefNestedMap", ZfFieldTC::Int32, ZfFieldTC::UDT> ZiFB_Fmt(RefNested *);

struct Small { int value = 0; };
struct SmallMap {
  using T = int;
  enum { Code = ZfFieldTC::Int32 };
  static constexpr int save(Small v) { return v.value; }
  static Small load(int v) { return {v}; }
};
SmallMap ZiFBTransform(Small *);
ZuDerive(Mapped, (ZmRBTreeKV<int, Small, ZmRBTreeHeapID<"ZiFBMapTest.Mapped">>));
ZiFB::AsMap<"test.MappedMap", ZfFieldTC::Int32, ZfFieldTC::UDT> ZiFB_Fmt(Mapped *);

struct Variant { int value; };
ZfStruct(, (Variant, FB), (value, (Ctor<0>, Deflt<0>), Int32));
ZiFB::AsTable<"test.MapVariant"> ZiFB_Fmt(Variant *);
ZuDerive(Choice, (ZuUnion<void, Variant>));
ZiFB::AsUnion<"test.MapChoice", ZiFB::Alt<"Variant", Variant>> ZiFB_Fmt(Choice *);
ZuDerive(Variants, (ZmRBTreeKV<int, Choice, ZmRBTreeHeapID<"ZiFBMapTest.Variants">>));
ZiFB::AsMap<"test.VariantMap", ZfFieldTC::Int32, ZfFieldTC::UDT> ZiFB_Fmt(Variants *);

static void roundTrip()
{
  ZuTestScope(roundTrip);
  ZmRef<Hash> hash = new Hash();
  hash->add(7, 70);
  hash->add(-3, 30);
  hash->add(0, 0);
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, *hash) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Hash>(bytes));
  auto entries = ZiFB::handler<Hash>(bytes).entries();
  ZuCheck(entries.length() == 3);
  ZuCheck(entries.key(0) == -3 && entries.key(1) == 0 && entries.key(2) == 7);
  using E = typename decltype(entries)::Entry;
  ZuCheck(entries.find(7).template get<typename E::VF>() == 70);
  ZuCheck(!entries.find(8));
  auto tree = ZiFB::load<Tree>(bytes);
  ZuCheck(tree.count_() == 3 && tree.findVal(-3) == 30);
  tree.add(99, 99);
  ZiFB::handler<Tree>(bytes).update(tree);
  ZuCheck(tree.count_() == 4 && tree.findVal(99) == 99);
  ZiFB::handler<Tree>(bytes).load(tree);
  ZuCheck(tree.count_() == 3 && !tree.find(99));
  ZmRef<Hash> loaded = ZiFB::handler<Hash>(bytes).alloc();
  ZuCheck(loaded->count_() == 3 && loaded->findVal(7) == 70);

  Row row{new Hash(), {}};
  row.map->add(4, 40);
  row.tree.add(5, 50);
  bytes.null();
  ZuCheck(ZiFB::save(bytes, row) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Row>(bytes));
  auto view = ZiFB::view<Row>(bytes).get<ZfField(Row, map)>();
  ZuCheck(view.length() == 1 && view.key(0) == 4 && view.value(0) == 40);
  auto out = ZiFB::load<Row>(bytes);
  ZuCheck(out.map->count_() == 1 && out.map->findVal(4) == 40);
  ZuCheck(out.tree.count_() == 1 && out.tree.findVal(5) == 50);
}

static void strings()
{
  ZuTestScope(strings);
  ZmRef<Strings> map = new Strings();
  map->add("z", "last");
  map->add("a", "first");
  map->add("", "empty");
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, *map) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Strings>(bytes));
  auto entries = ZiFB::handler<Strings>(bytes).entries();
  ZuCheck(entries.length() == 3 && entries.key(0) == "");
  ZmRef<Strings> out = new Strings();
  ZiFB::handler<Strings>(bytes).load(*out);
  ZuCheck(out->findVal("") == "empty" && out->findVal("z") == "last");
}

static void duplicates()
{
  ZuTestScope(duplicates);
  ZiFB::Buffer buf;
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<ZiFB::ArrayStorage<>> builder{storage};
  using E = typename ZiFB::As<Hash>::template Entry<int, int>;
  using EH = typename ZiFB::As<E>::template Handler<E>;
  // An explicit unsorted wire fixture with a repeated key. Verification is
  // structural only, and both native containers must keep the last value.
  ZuArray<ZiFB::Offset, 3> offsets(3, false);
  int key = 7, val = 70;
  offsets[0] = EH::save(builder, E{key, val});
  key = 2; val = 20;
  offsets[1] = EH::save(builder, E{key, val});
  key = 7; val = 700;
  offsets[2] = EH::save(builder, E{key, val});
  auto entries = builder.offsets(offsets);
  auto table = builder.startTable();
  builder.fieldOffset(table, 0, entries);
  auto bytes = builder.finish(builder.endTable(table));
  ZuCheck(ZiFB::verify<Hash>(bytes));
  ZmRef<Hash> hash = new Hash();
  hash->add(99, 99);
  ZiFB::handler<Hash>(bytes).update(*hash);
  ZuCheck(hash->count_() == 3 && hash->findVal(7) == 700 && hash->findVal(99) == 99);
  ZiFB::handler<Hash>(bytes).load(*hash);
  ZuCheck(hash->count_() == 2 && hash->findVal(7) == 700 && hash->findVal(2) == 20);
  auto tree = ZiFB::load<Tree>(bytes);
  ZuCheck(tree.count_() == 2 && tree.findVal(7) == 700);
  ZuCheck(!ZiFB::verify<Hash>(ZuBSpan{bytes.data(), bytes.length() - 1}));
}

static void pointers()
{
  ZuTestScope(pointers);
  ZmRef<Raw> source = new Raw();
  ZmRef<Raw> out = new Raw();
  source->add(1, new Item(10));
  source->add(2, nullptr);
  out->add(1, new Item(90));
  out->add(9, new Item(99));
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, *source) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Raw>(bytes));
  ZiFB::handler<Raw>(bytes).update(*out);
  ZuCheck(alive == 3 && out->count_() == 3 && out->findVal(1)->value == 10);
  ZuCheck(!out->findVal(2) && out->findVal(9)->value == 99);
  ZiFB::handler<Raw>(bytes).load(*out);
  ZuCheck(alive == 2 && out->count_() == 2 && out->findVal(1)->value == 10);
  {
    Owned owned;
    owned.add(1, ZuPtr<Item>{new Item(44)});
    // The named wire layout is the same, with pointer ownership chosen by
    // the destination. Null values stay null; smart pointer release is ordinary.
    ZiFB::handler<Owned>(bytes).update(owned);
    ZuCheck(alive == 3 && owned.find(1)->val()->value == 10);
  }
  ZuCheck(alive == 2);
  ZmRef<Raw> empty = new Raw();
  bytes.null();
  ZuCheck(ZiFB::save(bytes, *empty) == ZiFB::Error::OK);
  ZiFB::handler<Raw>(bytes).load(*source);
  ZiFB::handler<Raw>(bytes).load(*out);
  ZuCheck(alive == 0);
}

static void schemas()
{
  ZuTestScope(schemas);
  ZiFB::Buffer bfbs;
  ZiFB::saveSchema<Hash>(bfbs);
  ZuCheck(ZiFB::Reflect::verifySchema(bfbs));
  auto schema = ZiFB::view<ZiFB::Reflect::Schema>(bfbs);
  auto objects = schema.get<ZiFB::Reflect::ZfField(Schema, objects)>();
  ZuCheck(objects.length() == 2);
  bool keyed = false;
  for (unsigned i = 0, n = objects.length(); i < n; ++i) {
    auto fields = objects[i].get<ZiFB::Reflect::ZfField(Object, fields)>();
    for (unsigned j = 0, m = fields.length(); j < m; ++j)
      if (fields[j].get<ZiFB::Reflect::ZfField(Field, name)>() == "key")
        keyed = fields[j].get<ZiFB::Reflect::ZfField(Field, key)>();
  }
  ZuCheck(keyed);
  ZtString<> text;
  ZiFB::saveFBS<Hash>(text);
  ZuCheck(strstr(text.data(), "key:int = 0 (id: 0, key)") != nullptr);
  ZuCheck(strstr(text.data(), "entries:[test.IntMapEntry]") != nullptr);

  // Both native maps have one canonical entry table despite distinct key,
  // value, node, and container heaps.
  Shared shared{new Strings(), {}};
  shared.hash->add("left", "hash");
  shared.tree.add("right", "tree");
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, shared) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Shared>(bytes));
  bfbs.null();
  ZiFB::saveSchema<Shared>(bfbs);
  ZuCheck(ZiFB::Reflect::verifySchema(bfbs));
  objects = ZiFB::view<ZiFB::Reflect::Schema>(bfbs)
    .get<ZiFB::Reflect::ZfField(Schema, objects)>();
  ZuCheck(objects.length() == 2);
  text.null();
  ZiFB::saveFBS<Shared>(text);
  ZuCheck(strstr(text.data(), "table TextMapEntry") != nullptr);
  auto first = strstr(text.data(), "table TextMapEntry");
  ZuCheck(!strstr(first + 1, "table TextMapEntry"));
}

static void nested()
{
  ZuTestScope(nested);
  Nested source;
  Tree tree;
  tree.add(2, 20);
  source.add(1, ZuMv(tree));
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, source) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Nested>(bytes));
  auto out = ZiFB::load<Nested>(bytes);
  ZuCheck(out.find(1)->val().findVal(2) == 20);
  auto entries = ZiFB::handler<Nested>(bytes).entries();
  auto inner = entries.value(0);
  ZuCheck(inner.length() == 1 && inner.key(0) == 2 && inner.value(0) == 20);
  ZmRef<Hash> hash = new Hash();
  hash->add(3, 30);
  RefNested refs;
  refs.add(1, hash);
  refs.add(2, nullptr);
  bytes.null();
  ZuCheck(ZiFB::save(bytes, refs) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<RefNested>(bytes));
  auto loaded = ZiFB::load<RefNested>(bytes);
  ZuCheck(loaded.findVal(1)->findVal(3) == 30 && !loaded.findVal(2));
}

static void converted()
{
  ZuTestScope(converted);
  {
    Custom source;
    auto item = new Item(11);
    item->omitted = 42;
    source.add(1, Own{item});
    source.add(2, Own{});
    ZiFB::Buffer bytes;
    ZuCheck(ZiFB::save<ZuFacet::MapWire>(bytes, source) == ZiFB::Error::OK);
    ZuCheck((ZiFB::verify<Custom, ZuFacet::MapWire>(bytes)));
    auto out = ZiFB::load<Custom, ZuFacet::MapWire>(bytes);
    ZuCheck(out.find(1)->val()->value == 11 && out.find(1)->val()->omitted == -7);
    ZuCheck(!out.find(2)->val());
    ZiFB::handler<Custom, ZuFacet::MapWire>(bytes).update(out);
    ZuCheck(alive == 2 && out.find(1)->val()->value == 11);
  }
  ZuCheck(alive == 0);
  Mapped mapped;
  mapped.add(9, Small{90});
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, mapped) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Mapped>(bytes));
  auto out = ZiFB::load<Mapped>(bytes);
  ZuCheck(out.findVal(9).value == 90);
}

static void construction()
{
  ZuTestScope(construction);
  Tree source;
  source.add(8, 80);
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, source) == ZiFB::Error::OK);
  alignas(Tree) uint8_t memory[sizeof(Tree)];
  ZiFB::handler<Tree>(bytes).new_(memory);
  auto placed = ZuLaunder(reinterpret_cast<Tree *>(memory));
  ZuCheck(placed->findVal(8) == 80);
  placed->~Tree();
  Tree empty;
  bytes.null();
  ZuCheck(ZiFB::save(bytes, empty) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Tree>(bytes));
  auto out = ZiFB::load<Tree>(bytes);
  ZuCheck(out.count_() == 0 && ZiFB::handler<Tree>(bytes).entries().length() == 0);
}

static void unions()
{
  ZuTestScope(unions);
  Variants source;
  Choice choice;
  choice.p<1>(Variant{42});
  source.add(7, ZuMv(choice));
  ZiFB::Buffer bytes;
  ZuCheck(ZiFB::save(bytes, source) == ZiFB::Error::OK);
  ZuCheck(ZiFB::verify<Variants>(bytes));
  auto out = ZiFB::load<Variants>(bytes);
  ZuCheck(out.find(7)->val().type() == 1 && out.find(7)->val().p<1>().value == 42);
  ZiFB::Buffer bfbs;
  ZiFB::saveSchema<Variants>(bfbs);
  ZuCheck(ZiFB::Reflect::verifySchema(bfbs));
  ZtString<> text;
  ZiFB::saveFBS<Variants>(text);
  ZuCheck(strstr(text.data(), "value:test.MapChoice") != nullptr);
}

static void growth()
{
  ZuTestScope(growth);
  ZiFB::Buffer buf;
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  Tree first;
  first.add(1, 10);
  auto saved = ZiFB::save(builder, first);
  unsigned before = builder.capacity();
  unsigned scratch = builder.head();
  ZuCheck(scratch != 0);
  ZmRef<Strings> source = new Strings();
  // Scale the fixture from the actual arena capacity to force several growths.
  unsigned count = before/sizeof(ZiFB::Offset) + 1;
  for (unsigned i = 0; i < count; ++i) {
    ZtString<> key, value;
    key << "key" << ZuBox<unsigned>{i};
    value << key << ": a value large enough to grow the entry arena";
    source->add(ZuMv(key), ZuMv(value));
  }
  auto bytes = builder.finish(ZiFB::save(builder, *source));
  ZuCheck(!builder.failed() && builder.capacity() > before);
  ZuCheck(builder.head() >= scratch && ZiFB::verify<Strings>(bytes));
  auto entries = ZiFB::handler<Strings>(bytes).entries();
  ZuCheck(entries.length() == count && bool(entries.find("key0")));
  auto old = ZiFB::handler(ZiFB::View<Tree>{builder.at(saved)}).entries();
  ZuCheck(old.length() == 1 && old.key(0) == 1 && old.value(0) == 10);
}

} // MapTest

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(MapTest::roundTrip);
  ZuTestCall(MapTest::strings);
  ZuTestCall(MapTest::duplicates);
  ZuTestCall(MapTest::pointers);
  ZuTestCall(MapTest::schemas);
  ZuTestCall(MapTest::nested);
  ZuTestCall(MapTest::converted);
  ZuTestCall(MapTest::construction);
  ZuTestCall(MapTest::unions);
  ZuTestCall(MapTest::growth);
}
