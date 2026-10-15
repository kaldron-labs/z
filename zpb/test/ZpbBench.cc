//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZpbReflect.hh>

using namespace ZuTestUtil;

ZuDerive(BenchBytes,
  (ZtArray<uint8_t, ZtArrayHeapID<"ZpbBench.Bytes">>));
ZuDerive(BenchNumbers,
  (ZtArray<int32_t, ZtArrayHeapID<"ZpbBench.Numbers">>));
ZuDerive(BenchMap,
  (ZmRBTreeKV<int32_t, int32_t, ZmRBTreeHeapID<"ZpbBench.Map">>));
Zpb::AsMap<"bench.Map", ZfFieldTC::Int32, ZfFieldTC::Int32>
  Zpb_Fmt(BenchMap *);

struct BenchSmall { uint32_t id = 0; bool enabled = false; };
ZfStruct(, (BenchSmall, PB), (id,, UInt32), (enabled,, Bool));
Zpb::AsMessage<"bench.Small"> Zpb_Fmt(BenchSmall *);

struct BenchNested { BenchSmall child; uint32_t sequence = 0; };
ZfStruct(, (BenchNested, PB), (child,, UDT), (sequence,, UInt32));
Zpb::AsMessage<"bench.Nested"> Zpb_Fmt(BenchNested *);

struct BenchRepeated { BenchNumbers numbers; BenchMap entries; };
ZfStruct(, (BenchRepeated, PB),
  (numbers,, Int32Vec), (entries,, UDT));
Zpb::AsMessage<"bench.Repeated"> Zpb_Fmt(BenchRepeated *);

struct BenchLarge { ZuBSpan bytes; ZuCSpan text; };
ZfStruct(, (BenchLarge, PB), (bytes,, Bytes), (text,, String));
Zpb::AsMessage<"bench.Large"> Zpb_Fmt(BenchLarge *);

using BenchPool = ZiIOBufAlloc<32, 1024, "ZpbBench.TxPool">;
struct BenchTx : ZiTxStream<BenchTx> {
  using Base = ZiTxStream<BenchTx>;
  using Base::operator <<;
  BenchBytes output;
  unsigned bytes = 0, sends = 0;
  bool capture = false;

  explicit BenchTx(bool capture_ = false) : Base{128, 8, 4},
    capture{capture_} { }
  ~BenchTx() { flush(); }
  ZmRef<ZiIOBuf> allocBuf_(unsigned head) {
    ZmRef<ZiIOBuf> buf = new BenchPool{};
    buf->skip = head;
    return buf;
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    ++sends;
    bytes += buf->length;
    if (capture)
      for (auto byte : buf->cspan()) output.push(byte);
    return true;
  }
};
ZuAssert(ZiIsTxStream<BenchTx>{});

template <typename T, typename Check>
static bool roundTrip(const T &object, ZuCSpan name, Check check)
{
  Zpb::Reflect::DescSet descriptor;
  if (!Zpb::Reflect::exportSchema<T>(descriptor, "bench.proto", "bench",
    Zpb::Reflect::Syntax::Proto3)) return false;
  BenchBytes descriptorBytes;
  Zpb::ArrayStorage descriptorStorage{descriptorBytes};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto schemaBytes = Zpb::save(descriptorBuilder, descriptor);
  if (descriptorBuilder.failed() ||
    !Zpb::Reflect::verifySchema(schemaBytes)) return false;
  Zpb::Reflect::Schema schema{schemaBytes};
  auto message = schema.message(name);
  if (!message) return false;

  BenchBytes output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, object);
  if (builder.failed() || !Zpb::verify<T>(wire) ||
    !Zpb::Reflect::verify(schema, *message, wire)) return false;
  auto decoded = Zpb::load<T>(wire);
  if (!check(decoded)) return false;

  Zpb::IOBuilder pooled{new ZiIOBufAlloc<>};
  auto pooledWire = Zpb::save(pooled, object);
  if (pooled.failed() || pooledWire.length() != wire.length() ||
    memcmp(pooledWire.data(), wire.data(), wire.length())) return false;
  auto buf = pooled.buf();
  return buf && Zpb::verify<T>(buf->cspan()) &&
    Zpb::Reflect::verify(schema, *message, buf->cspan());
}

static void small()
{
  ZuTestScope(small);
  BenchSmall object{7, true};
  ZuCheck(roundTrip(object, ".bench.Small", [](const auto &value) {
    return value.id == 7 && value.enabled;
  }));
}

static void nested()
{
  ZuTestScope(nested);
  BenchNested object{{7, true}, 43};
  ZuCheck(roundTrip(object, ".bench.Nested", [](const auto &value) {
    return value.child.id == 7 && value.child.enabled &&
      value.sequence == 43;
  }));
}

static bool sameRepeated(const BenchRepeated &value)
{
  if (value.numbers.length() != 32 || value.entries.citer().count() != 32)
    return false;
  for (int i = 1; i <= 32; ++i) {
    auto entry = value.entries.find(i);
    if (value.numbers[i - 1] != i || !entry || entry->val() != i << 2)
      return false;
  }
  return true;
}

static void repeated()
{
  ZuTestScope(repeated);
  BenchRepeated object;
  for (int i = 1; i <= 32; ++i) {
    object.numbers.push(i);
    object.entries.add(i, i << 2);
  }
  ZuCheck(roundTrip(object, ".bench.Repeated", sameRepeated));
}

static void large()
{
  ZuTestScope(large);
  // 64 KiB exercises pooled growth and a payload larger than typical I/O
  // blocks; it is a fixture size, not a codec limit.
  constexpr unsigned PayloadSize = 1U << 16;
  BenchBytes bytes;
  bytes.length(PayloadSize);
  ZtString<ZtStringHeapID<"ZpbBench.Text">> text;
  text.length(PayloadSize);
  for (unsigned i = 0, n = bytes.length(); i < n; ++i)
    bytes[i] = uint8_t(i ^ (i >> 8));
  for (unsigned i = 0, n = text.length(); i < n; ++i)
    text[i] = char('a' + i % 26);
  BenchLarge object{bytes, text};
  ZuCheck(roundTrip(object, ".bench.Large", [&](const auto &value) {
    return value.bytes == bytes && value.text == object.text;
  }));
}

template <typename T, typename Check>
static bool measure(const T &object, ZuCSpan name, Check check,
  unsigned count, int operation)
{
  if (!roundTrip(object, name, check)) return false;
  Zpb::Reflect::DescSet descriptor;
  if (!Zpb::Reflect::exportSchema<T>(descriptor, "bench.proto", "bench",
    Zpb::Reflect::Syntax::Proto3)) return false;
  BenchBytes descriptorBytes;
  Zpb::ArrayStorage descriptorStorage{descriptorBytes};
  Zpb::Builder descriptorBuilder{descriptorStorage};
  auto schemaBytes = Zpb::save(descriptorBuilder, descriptor);
  if (descriptorBuilder.failed()) return false;
  Zpb::Reflect::Schema schema{schemaBytes};
  auto message = schema.message(name);
  if (!message) return false;
  BenchBytes output;
  Zpb::ArrayStorage storage{output};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, object);
  if (builder.failed()) return false;
  if (operation == 6) {
    BenchTx preview{true};
    if (Zpb::save(preview, object) != Zpb::Error::OK ||
      preview.failed() || preview.output != wire ||
      preview.bytes != wire.length() || !preview.sends) return false;
  }
  uint64_t sum = 0;
  auto begin = Zm::now();
  for (unsigned i = 0; i < count; ++i) {
    __asm__ __volatile__("" : : : "memory");
    switch (operation) {
      case 0: {
	Zpb::Builder next{storage};
	auto bytes = Zpb::save(next, object);
	if (next.failed()) return false;
	sum += bytes.length();
	break;
      }
      case 1: {
	Zpb::IOBuilder next{new ZiIOBufAlloc<>};
	auto bytes = Zpb::save(next, object);
	if (next.failed()) return false;
	auto owner = next.buf();
	if (!owner) return false;
	sum += bytes.length();
	break;
      }
      case 2: {
	auto decoded = Zpb::load<T>(wire);
	__asm__ __volatile__("" : : "g"(&decoded) : "memory");
	sum += wire.length();
	break;
      }
      case 3: sum += Zpb::verify<T>(wire); break;
      case 4: sum += Zpb::Reflect::verify(schema, *message, wire); break;
      case 5: {
	BenchBytes fresh;
	Zpb::ArrayStorage freshStorage{fresh};
	Zpb::Builder next{freshStorage};
	auto bytes = Zpb::save(next, object);
	if (next.failed()) return false;
	sum += bytes.length();
	break;
      }
      case 6: {
	BenchTx next;
	if (Zpb::save(next, object) != Zpb::Error::OK || next.failed() ||
	  next.bytes != wire.length() || !next.sends) return false;
	sum += next.bytes;
	break;
      }
    }
  }
  auto elapsed = Zm::now() - begin;
  std::cout << name << ',' << operation << ',' << wire.length() << ','
    << count << ',' << elapsed.as_decimal() << ',' << sum << '\n';
  return true;
}

static bool bench(unsigned count, int workload, int operation)
{
  switch (workload) {
    case 0: {
      BenchSmall object{7, true};
      return measure(object, ".bench.Small", [](const auto &value) {
	return value.id == 7 && value.enabled;
      }, count, operation);
    }
    case 1: {
      BenchNested object{{7, true}, 43};
      return measure(object, ".bench.Nested", [](const auto &value) {
	return value.child.id == 7 && value.child.enabled &&
	  value.sequence == 43;
      }, count, operation);
    }
    case 2: {
      BenchRepeated object;
      for (int i = 1; i <= 32; ++i) {
	object.numbers.push(i);
	object.entries.add(i, i << 2);
      }
      return measure(object, ".bench.Repeated", sameRepeated, count,
	operation);
    }
    case 3: {
      constexpr unsigned PayloadSize = 1U << 16;
      BenchBytes bytes;
      bytes.length(PayloadSize);
      ZtString<ZtStringHeapID<"ZpbBench.Text">> text;
      text.length(PayloadSize);
      for (unsigned i = 0; i < PayloadSize; ++i) {
	bytes[i] = uint8_t(i ^ (i >> 8));
	text[i] = char('a' + i % 26);
      }
      BenchLarge object{bytes, text};
      return measure(object, ".bench.Large", [&](const auto &value) {
	return value.bytes == bytes && value.text == text;
      }, count, operation);
    }
  }
  return false;
}

int main(int argc, char **argv)
{
  if (argc != 1) {
    if (argc != 4) {
      std::cerr << "Usage: ZpbBench [COUNT "
	"small|nested|repeated|large "
	"save|pooled|decode|verify|reflect|grow|tx]\n";
      return 1;
    }
    struct Workloads {
      using Keys = ZuStringTL<"small", "nested", "repeated", "large">;
    };
    struct Operations {
      using Keys = ZuStringTL<"save", "pooled", "decode", "verify",
	"reflect", "grow", "tx">;
    };
    static constexpr auto workloads = ZuMatcher<Workloads>();
    static constexpr auto operations = ZuMatcher<Operations>();
    unsigned count = ZuBox<unsigned>{argv[1]};
    int workload = workloads.exact(argv[2]);
    int operation = operations.exact(argv[3]);
    if (!count || workload < 0 || operation < 0) return 1;
    std::cout << "message,operation,encoded_bytes,count,seconds,checksum\n";
    return bench(count, workload, operation) ? 0 : 1;
  }
  ZuTestMain();
  ZuTestCall(small);
  ZuTestCall(nested);
  ZuTestCall(repeated);
  ZuTestCall(large);
}
