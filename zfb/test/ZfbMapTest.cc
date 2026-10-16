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
#include <zlib/ZfbReflect.hh>

using namespace ZuTestUtil;

ZuStructFacet(MapWire);

namespace MapTest {

template <typename K, typename V, ZuString ID>
struct Hash_ : public ZmNHeap<Hash_<K, V, ID>>, public ZmHashKV<K, V, ZmHashHeapID<ID>> {
friend inline ZuStringT<ID + ".Obj"_z> ZmNHeapID(Hash_ *);

};

ZuDerive(Hash, (Hash_<int, int, "ZfbMapTest.Hash">));
ZuDerive(Tree, (ZmRBTreeKV<int, int, ZmRBTreeHeapID<"ZfbMapTest.Tree">>));
Zfb::AsMap<"test.IntMap", ZfFieldTC::Int32, ZfFieldTC::Int32> Zfb_Fmt(Hash *);
Zfb::AsMap<"test.IntMap", ZfFieldTC::Int32, ZfFieldTC::Int32> Zfb_Fmt(Tree *);
ZuDerive(Strings, (Hash_<ZtString<>, ZtString<>, "ZfbMapTest.Text">));
Zfb::AsMap<"test.TextMap", ZfFieldTC::String, ZfFieldTC::String> Zfb_Fmt(Strings *);
using AltText = ZtString<ZtStringHeapID<"ZfbMapTest.AltText">>;
ZuDerive(AltStrings, (ZmRBTreeKV<AltText, AltText,
  ZmRBTreeHeapID<"ZfbMapTest.AltStrings">>));
Zfb::AsMap<"test.TextMap", ZfFieldTC::String, ZfFieldTC::String> Zfb_Fmt(AltStrings *);
struct Shared { ZmRef<Strings> hash; AltStrings tree; };
ZfStruct(, (Shared, FB), (hash, (Mutable), UDT), (tree, (Mutable), UDT));
Zfb::AsTable<"test.SharedMaps"> Zfb_Fmt(Shared *);

struct Row { ZmRef<Hash> map; Tree tree; };
ZfStruct(, (Row, FB), (map, (Mutable), UDT), (tree, (Mutable), UDT));
Zfb::AsTable<"test.MapRow"> Zfb_Fmt(Row *);

static int alive = 0;
struct Item_ : public ZmNHeap<Item_> {
friend inline ZuStringT<"ZfbMapTest.Item"> ZmNHeapID(Item_ *);

  int value;
  int omitted = -7;

  explicit Item_(int v = 0) : value{v} { ++alive; }
  ~Item_() { --alive; }
};

ZuDerive(Item, Item_);
ZfStruct(, (Item, FB),
  (value, (Ctor<0>, Deflt<0>), Int32),
  (omitted,, Int32));
ZfStructRender(, Item, MapWire, value);
Zfb::AsTable<"test.MapItem"> Zfb_Fmt(Item *);
ZuDerive(Raw, (Hash_<int, Item *, "ZfbMapTest.Raw">));
Zfb::AsMap<"test.RawMap", ZfFieldTC::Int32, ZfFieldTC::UDT> Zfb_Fmt(Raw *);
ZuDerive(Owned, (ZmRBTreeKV<int, ZuPtr<Item>, ZmRBTreeHeapID<"ZfbMapTest.Owned">>));
Zfb::AsMap<"test.OwnedMap", ZfFieldTC::Int32, ZfFieldTC::UDT> Zfb_Fmt(Owned *);

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
ZuDerive(Custom, (ZmRBTreeKV<int, Own, ZmRBTreeHeapID<"ZfbMapTest.Custom">>));
Zfb::AsMap<"test.CustomMap", ZfFieldTC::Int32, ZfFieldTC::UDT> Zfb_Fmt(Custom *);

ZuDerive(Nested, (ZmRBTreeKV<int, Tree, ZmRBTreeHeapID<"ZfbMapTest.Nested">>));
Zfb::AsMap<"test.NestedMap", ZfFieldTC::Int32, ZfFieldTC::UDT> Zfb_Fmt(Nested *);
ZuDerive(RefNested, (ZmRBTreeKV<int, ZmRef<Hash>, ZmRBTreeHeapID<"ZfbMapTest.RefNested">>));
Zfb::AsMap<"test.RefNestedMap", ZfFieldTC::Int32, ZfFieldTC::UDT> Zfb_Fmt(RefNested *);

struct Small { int value = 0; };
struct SmallMap {
  using T = int;
  enum { Code = ZfFieldTC::Int32 };
  static constexpr int save(Small v) { return v.value; }
  static Small load(int v) { return {v}; }
};
SmallMap ZfbTransform(Small *);
ZuDerive(Mapped, (ZmRBTreeKV<int, Small, ZmRBTreeHeapID<"ZfbMapTest.Mapped">>));
Zfb::AsMap<"test.MappedMap", ZfFieldTC::Int32, ZfFieldTC::UDT> Zfb_Fmt(Mapped *);

struct Variant { int value; };
ZfStruct(, (Variant, FB), (value, (Ctor<0>, Deflt<0>), Int32));
Zfb::AsTable<"test.MapVariant"> Zfb_Fmt(Variant *);
ZuDerive(Choice, (ZuUnion<void, Variant>));
Zfb::AsUnion<"test.MapChoice", Zfb::Alt<"Variant", Variant>> Zfb_Fmt(Choice *);
ZuDerive(Variants, (ZmRBTreeKV<int, Choice, ZmRBTreeHeapID<"ZfbMapTest.Variants">>));
Zfb::AsMap<"test.VariantMap", ZfFieldTC::Int32, ZfFieldTC::UDT> Zfb_Fmt(Variants *);

static void roundTrip()
{
  ZuTestScope(roundTrip);
  ZmRef<Hash> hash = new Hash();
  hash->add(7, 70);
  hash->add(-3, 30);
  hash->add(0, 0);
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, *hash) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Hash>(bytes));
  auto entries = Zfb::handler<Hash>(bytes).entries();
  ZuCheck(entries.length() == 3);
  ZuCheck(entries.key(0) == -3 && entries.key(1) == 0 && entries.key(2) == 7);
  using E = decltype(entries)::Entry;
  ZuCheck(entries.find(7).template get<E::VF>() == 70);
  ZuCheck(!entries.find(8));
  auto tree = Zfb::load<Tree>(bytes);
  ZuCheck(tree.count_() == 3 && tree.findVal(-3) == 30);
  tree.add(99, 99);
  Zfb::handler<Tree>(bytes).update(tree);
  ZuCheck(tree.count_() == 4 && tree.findVal(99) == 99);
  Zfb::handler<Tree>(bytes).load(tree);
  ZuCheck(tree.count_() == 3 && !tree.find(99));
  ZmRef<Hash> loaded = Zfb::handler<Hash>(bytes).alloc();
  ZuCheck(loaded->count_() == 3 && loaded->findVal(7) == 70);

  Row row{new Hash(), {}};
  row.map->add(4, 40);
  row.tree.add(5, 50);
  bytes.null();
  ZuCheck(Zfb::save(bytes, row) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Row>(bytes));
  auto view = Zfb::view<Row>(bytes).get<ZfField(Row, map)>();
  ZuCheck(view.length() == 1 && view.key(0) == 4 && view.value(0) == 40);
  auto out = Zfb::load<Row>(bytes);
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
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, *map) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Strings>(bytes));
  auto entries = Zfb::handler<Strings>(bytes).entries();
  ZuCheck(entries.length() == 3 && entries.key(0) == "");
  ZmRef<Strings> out = new Strings();
  Zfb::handler<Strings>(bytes).load(*out);
  ZuCheck(out->findVal("") == "empty" && out->findVal("z") == "last");
}

static void duplicates()
{
  ZuTestScope(duplicates);
  Zfb::Buffer buf;
  Zfb::ArrayStorage<> storage{buf};
  Zfb::Builder<Zfb::ArrayStorage<>> builder{storage};
  using E = Zfb::As<Hash>::template Entry<int, int>;
  using EH = Zfb::As<E>::template Handler<E>;
  // An explicit unsorted wire fixture with a repeated key. Verification is
  // structural only, and both native containers must keep the last value.
  ZuArray<Zfb::Offset, 3> offsets(3, false);
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
  ZuCheck(Zfb::verify<Hash>(bytes));
  ZmRef<Hash> hash = new Hash();
  hash->add(99, 99);
  Zfb::handler<Hash>(bytes).update(*hash);
  ZuCheck(hash->count_() == 3 && hash->findVal(7) == 700 && hash->findVal(99) == 99);
  Zfb::handler<Hash>(bytes).load(*hash);
  ZuCheck(hash->count_() == 2 && hash->findVal(7) == 700 && hash->findVal(2) == 20);
  auto tree = Zfb::load<Tree>(bytes);
  ZuCheck(tree.count_() == 2 && tree.findVal(7) == 700);
  ZuCheck(!Zfb::verify<Hash>(ZuBSpan{bytes.data(), bytes.length() - 1}));
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
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, *source) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Raw>(bytes));
  Zfb::handler<Raw>(bytes).update(*out);
  ZuCheck(alive == 3 && out->count_() == 3 && out->findVal(1)->value == 10);
  ZuCheck(!out->findVal(2) && out->findVal(9)->value == 99);
  Zfb::handler<Raw>(bytes).load(*out);
  ZuCheck(alive == 2 && out->count_() == 2 && out->findVal(1)->value == 10);
  {
    Owned owned;
    owned.add(1, ZuPtr<Item>{new Item(44)});
    // The named wire layout is the same, with pointer ownership chosen by
    // the destination. Null values stay null; smart pointer release is ordinary.
    Zfb::handler<Owned>(bytes).update(owned);
    ZuCheck(alive == 3 && owned.find(1)->val()->value == 10);
  }
  ZuCheck(alive == 2);
  ZmRef<Raw> empty = new Raw();
  bytes.null();
  ZuCheck(Zfb::save(bytes, *empty) == Zfb::Error::OK);
  Zfb::handler<Raw>(bytes).load(*source);
  Zfb::handler<Raw>(bytes).load(*out);
  ZuCheck(alive == 0);
}

static void schemas()
{
  ZuTestScope(schemas);
  Zfb::Buffer bfbs;
  Zfb::saveSchema<Hash>(bfbs);
  ZuCheck(Zfb::Reflect::verifySchema(bfbs));
  auto schema = Zfb::view<Zfb::Reflect::Schema>(bfbs);
  auto objects = schema.get<Zfb::Reflect::ZfField(Schema, objects)>();
  ZuCheck(objects.length() == 2);
  bool keyed = false;
  for (unsigned i = 0, n = objects.length(); i < n; ++i) {
    auto fields = objects[i].get<Zfb::Reflect::ZfField(Object, fields)>();
    for (unsigned j = 0, m = fields.length(); j < m; ++j)
      if (fields[j].get<Zfb::Reflect::ZfField(Field, name)>() == "key")
        keyed = fields[j].get<Zfb::Reflect::ZfField(Field, key)>();
  }
  ZuCheck(keyed);
  ZtString<> text;
  Zfb::saveFBS<Hash>(text);
  ZuCheck(strstr(text.data(), "key:int = 0 (id: 0, key)") != nullptr);
  ZuCheck(strstr(text.data(), "entries:[test.IntMapEntry]") != nullptr);

  // Both native maps have one canonical entry table despite distinct key,
  // value, node, and container heaps.
  Shared shared{new Strings(), {}};
  shared.hash->add("left", "hash");
  shared.tree.add("right", "tree");
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, shared) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Shared>(bytes));
  bfbs.null();
  Zfb::saveSchema<Shared>(bfbs);
  ZuCheck(Zfb::Reflect::verifySchema(bfbs));
  objects = Zfb::view<Zfb::Reflect::Schema>(bfbs)
    .get<Zfb::Reflect::ZfField(Schema, objects)>();
  ZuCheck(objects.length() == 2);
  text.null();
  Zfb::saveFBS<Shared>(text);
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
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, source) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Nested>(bytes));
  auto out = Zfb::load<Nested>(bytes);
  ZuCheck(out.find(1)->val().findVal(2) == 20);
  auto entries = Zfb::handler<Nested>(bytes).entries();
  auto inner = entries.value(0);
  ZuCheck(inner.length() == 1 && inner.key(0) == 2 && inner.value(0) == 20);
  ZmRef<Hash> hash = new Hash();
  hash->add(3, 30);
  RefNested refs;
  refs.add(1, hash);
  refs.add(2, nullptr);
  bytes.null();
  ZuCheck(Zfb::save(bytes, refs) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<RefNested>(bytes));
  auto loaded = Zfb::load<RefNested>(bytes);
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
    Zfb::Buffer bytes;
    ZuCheck(Zfb::save<ZuFacet::MapWire>(bytes, source) == Zfb::Error::OK);
    ZuCheck((Zfb::verify<Custom, ZuFacet::MapWire>(bytes)));
    auto out = Zfb::load<Custom, ZuFacet::MapWire>(bytes);
    ZuCheck(out.find(1)->val()->value == 11 && out.find(1)->val()->omitted == -7);
    ZuCheck(!out.find(2)->val());
    Zfb::handler<Custom, ZuFacet::MapWire>(bytes).update(out);
    ZuCheck(alive == 2 && out.find(1)->val()->value == 11);
  }
  ZuCheck(alive == 0);
  Mapped mapped;
  mapped.add(9, Small{90});
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, mapped) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Mapped>(bytes));
  auto out = Zfb::load<Mapped>(bytes);
  ZuCheck(out.findVal(9).value == 90);
}

static void construction()
{
  ZuTestScope(construction);
  Tree source;
  source.add(8, 80);
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, source) == Zfb::Error::OK);
  alignas(Tree) uint8_t memory[sizeof(Tree)];
  Zfb::handler<Tree>(bytes).new_(memory);
  auto placed = ZuLaunder(reinterpret_cast<Tree *>(memory));
  ZuCheck(placed->findVal(8) == 80);
  placed->~Tree();
  Tree empty;
  bytes.null();
  ZuCheck(Zfb::save(bytes, empty) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Tree>(bytes));
  auto out = Zfb::load<Tree>(bytes);
  ZuCheck(out.count_() == 0 && Zfb::handler<Tree>(bytes).entries().length() == 0);
}

static void unions()
{
  ZuTestScope(unions);
  Variants source;
  Choice choice;
  choice.p<1>(Variant{42});
  source.add(7, ZuMv(choice));
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, source) == Zfb::Error::OK);
  ZuCheck(Zfb::verify<Variants>(bytes));
  auto out = Zfb::load<Variants>(bytes);
  ZuCheck(out.find(7)->val().type() == 1 && out.find(7)->val().p<1>().value == 42);
  Zfb::Buffer bfbs;
  Zfb::saveSchema<Variants>(bfbs);
  ZuCheck(Zfb::Reflect::verifySchema(bfbs));
  ZtString<> text;
  Zfb::saveFBS<Variants>(text);
  ZuCheck(strstr(text.data(), "value:test.MapChoice") != nullptr);
}

static void growth()
{
  ZuTestScope(growth);
  Zfb::Buffer buf;
  Zfb::ArrayStorage<> storage{buf};
  Zfb::Builder<decltype(storage)> builder{storage};
  Tree first;
  first.add(1, 10);
  auto saved = Zfb::save(builder, first);
  unsigned before = builder.capacity();
  unsigned scratch = builder.head();
  ZuCheck(scratch != 0);
  ZmRef<Strings> source = new Strings();
  // Scale the fixture from the actual arena capacity to force several growths.
  unsigned count = before/sizeof(Zfb::Offset) + 1;
  for (unsigned i = 0; i < count; ++i) {
    ZtString<> key, value;
    key << "key" << ZuBox<unsigned>{i};
    value << key << ": a value large enough to grow the entry arena";
    source->add(ZuMv(key), ZuMv(value));
  }
  auto bytes = builder.finish(Zfb::save(builder, *source));
  ZuCheck(!builder.failed() && builder.capacity() > before);
  ZuCheck(builder.head() >= scratch && Zfb::verify<Strings>(bytes));
  auto entries = Zfb::handler<Strings>(bytes).entries();
  ZuCheck(entries.length() == count && bool(entries.find("key0")));
  auto old = Zfb::handler(Zfb::View<Tree>{builder.at(saved)}).entries();
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
