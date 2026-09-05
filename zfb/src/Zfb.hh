//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// flatbuffers integration

#ifndef Zfb_HH
#define Zfb_HH

#ifndef ZfbLib_HH
#include <zlib/ZfbLib.hh>
#endif

#include <flatbuffers/flatbuffers.h>

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>

#include <zlib/ZmBitmap.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZtBitmap.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiIP.hh>

#include <zlib/zfb_types_fbs.h>

namespace Zfb {

using namespace flatbuffers;

using Builder = FlatBufferBuilder;

// IOBuilder customizes FlatBufferBuilder with an allocator that
// builds directly into a detachable IOBuf for transmission/persistence
// - Note: IOBuilder is immovable - use a ZuPtr<> if needed
class IOBuilder : public Allocator, public Builder {
public:
  enum { Align = 8 };

  IOBuilder(ZmRef<ZiIOBuf> buf) :
    Builder{(buf->size & ~(Align - 1)) - buf->skip,
      this, false, Align},
    m_buf{ZuMv(buf)} {
    ZmAssert(!(m_buf->skip & (Align - 1)));
    m_buf->length = 0;
    m_buf->size &= ~(Align - 1);
  }

  // Release the builder arena while its owning IOBuf is still alive,
  // including when serialization throws before Finish()/buf().
  ~IOBuilder() { Reset(); }

  IOBuilder(IOBuilder &&) = delete;
  IOBuilder &operator =(IOBuilder &&) = delete;

  // attach buffer to builder
  void buf(ZmRef<ZiIOBuf> buf) {
    ZmAssert(!(buf->skip & (Align - 1)));
    buf->length = 0;
    buf->size &= ~(Align - 1);
    Builder::operator =(
      Builder{buf->size - buf->skip, this, false, Align});
    m_buf = ZuMv(buf);
  }

  // detach buffer from builder
  ZmRef<ZiIOBuf> buf() {
    if (ZuUnlikely(!m_buf)) return nullptr;
    auto buf = ZuMv(m_buf);
    size_t size, skip;
    ReleaseRaw(size, skip);
    buf->skip += skip;
    buf->length = size - skip;
    Clear();
    return buf;
  }

  // read buffer without detaching
  const ZiIOBuf *cspan() const { return m_buf.ptr(); }

protected:
  uint8_t *allocate(size_t size) {
    ZmAssert(m_buf);
    if (ZuUnlikely(!m_buf->alloc(size + m_buf->skip))) return nullptr;
    return m_buf->data();
  }

  void deallocate(uint8_t *ptr, size_t size) {
    if (m_buf) m_buf->free(ptr - m_buf->skip);
  }

  // override ZiIOBuf's default grow() with a pass-through because flatbuffers
  // has it's own buffer growth algorithm in vector_downward::reallocate()
private:
  static unsigned grow(unsigned, unsigned n) { return n; }
protected:
  uint8_t *reallocate_downward(
      uint8_t *old_p, size_t old_size, size_t new_size,
      size_t in_use_back, size_t in_use_front) {
    ZmAssert(m_buf);
    return m_buf->template realloc<grow>(
	old_size, new_size, in_use_front, in_use_back);
  }

private:
  ZmRef<ZiIOBuf>	m_buf;
};

namespace Save {
  using namespace Zfb; // needed for using namespace Zfb::Save

  // compile-time-recursive vector push
  template <typename T, typename I>
  inline void push_(T *, I) { }
  template <typename T, typename I, typename Arg0, typename ...Args>
  inline void push_(T *buf, I i, Arg0 &&arg0, Args &&...args) {
    buf[i] = ZuFwd<Arg0>(arg0);
    push_(buf, ZuUnsigned<i + 1>{}, ZuFwd<Args>(args)...);
  }

  // compile-time-recursive vector push, with a lambda map function
  template <typename T, typename L, typename I>
  inline void lpush_(T *, L &&, I) { }
  template <typename T, typename L, typename I, typename Arg0, typename ...Args>
  inline void lpush_(T *buf, L &&l, I i, Arg0 &&arg0, Args &&...args) {
    buf[i] = l(ZuFwd<Arg0>(arg0));
    lpush_(buf, ZuFwd<L>(l), ZuUnsigned<i + 1>{}, ZuFwd<Args>(args)...);
  }

  // push uninitialized vector
  template <typename Builder, typename T>
  inline Offset<Vector<T>> pvector_(Builder &fbb, unsigned length, T *&data) {
    return fbb.CreateUninitializedVector(
	length, sizeof(T), AlignOf<T>(), reinterpret_cast<uint8_t **>(&data));
  }
  // inline creation of a vector of primitive scalars
  template <typename T, typename Builder, typename ...Args>
  inline Offset<Vector<T>> pvector(Builder &fbb, Args &&...args) {
    auto n = ZuUnsigned<sizeof...(Args)>{};
    T *buf = nullptr;
    auto r = pvector_(fbb, n, buf);
    if (r.IsNull() || !buf) return {};
    lpush_(buf, [](T v) { return EndianScalar(v); },
	ZuUnsigned<0>{}, ZuFwd<Args>(args)...);
    return r;
  }
  // iterated creation of a vector of primitive values
  template <typename T, typename Builder, typename L>
  inline Offset<Vector<T>> pvectorIter(Builder &fbb, unsigned n, L &&l) {
    T *buf = nullptr;
    auto r = pvector_(fbb, n, buf);
    if (r.IsNull() || !buf) return {};
    for (unsigned i = 0; i < n; i++) buf[i] = EndianScalar<T>(ZuFwd<L>(l)(i));
    return r;
  }

  // Note: CreateUninitializedVector() cannot be used for vectors
  // of offsets. flatbuffers offsets are always unsigned and positive,
  // and the vector must therefore be lower in memory than the referenced
  // entities. Since flatbuffers are written downwards in memory, the
  // vector must be written following the entities, so the offsets must be
  // collected in a temporary buffer while they are being written.

  // inline creation of a vector of offsets
  template <typename T, typename Builder, typename ...Args>
  inline Offset<Vector<Offset<T>>> vector(Builder &fbb, Args &&...args) {
    auto n = ZuUnsigned<sizeof...(Args)>{};
    auto buf = ZmScratch(Offset<T>, n);
    if (!buf.data()) return {};
    push_(buf.data(), ZuUnsigned<0>{}, ZuFwd<Args>(args)...);
    auto r = fbb.CreateVector(buf.data(), n);
    return r;
  }
  // inline creation of a vector of lambda-transformed offsets
  template <typename T, typename Builder, typename L, typename ...Args>
  inline Offset<Vector<Offset<T>>> lvector(Builder &fbb, L &&l, Args &&...args) {
    auto n = ZuUnsigned<sizeof...(Args)>{};
    auto buf = ZmScratch(Offset<T>, n);
    if (!buf.data()) return {};
    lpush_(buf.data(), ZuFwd<L>(l), ZuUnsigned<0>{}, ZuFwd<Args>(args)...);
    auto r = fbb.CreateVector(buf.data(), n);
    return r;
  }
  // iterated creation of a vector of offsets
  template <typename T, typename Builder, typename L>
  inline Offset<Vector<Offset<T>>> vectorIter(Builder &fbb, unsigned n, L &&l) {
    auto buf = ZmScratch(Offset<T>, n);
    if (!buf.data()) return {};
    for (unsigned i = 0; i < n; i++) buf[i] = ZuFwd<L>(l)(fbb, i);
    auto r = fbb.CreateVector(buf.data(), n);
    return r;
  }

  // iterated creation of a vector of structs
  template <typename T, typename Builder, typename L>
  inline Offset<Vector<const T *>>
  structVecIter(Builder &fbb, unsigned n, L &&l) {
    return fbb.template CreateVectorOfStructs<T>(n,
      [&l](size_t i, T *ptr, void *) {
	ZuFwd<L>(l)(ptr, i);
      }, static_cast<void *>(nullptr));
  }

  // inline creation of a vector of keyed offsets
  template <typename T, typename Builder, typename ...Args>
  inline Offset<Vector<Offset<T>>> keyVec(Builder &fbb, Args &&...args) {
    auto n = ZuUnsigned<sizeof...(Args)>{};
    auto buf = ZmScratch(Offset<T>, n);
    if (!buf.data()) return {};
    push_(buf.data(), ZuUnsigned<0>{}, ZuFwd<Args>(args)...);
    auto r = fbb.CreateVectorOfSortedTables(buf.data(), n);
    return r;
  }
  // inline creation of a vector of lambda-transformed keyed offsets
  template <typename T, typename Builder, typename L, typename ...Args>
  inline Offset<Vector<Offset<T>>> lkeyVec(Builder &fbb, L &&l, Args &&...args) {
    auto n = ZuUnsigned<sizeof...(Args)>{};
    auto buf = ZmScratch(Offset<T>, n);
    if (!buf.data()) return {};
    lpush_(buf.data(), ZuFwd<L>(l), ZuUnsigned<0>{}, ZuFwd<Args>(args)...);
    auto r = fbb.CreateVectorOfSortedTables(buf.data(), n);
    return r;
  }
  // iterated creation of a vector of lambda-transformed keyed offsets
  template <typename T, typename Builder, typename L>
  inline Offset<Vector<Offset<T>>> keyVecIter(Builder &fbb, unsigned n, L &&l) {
    auto buf = ZmScratch(Offset<T>, n);
    if (!buf.data()) return {};
    for (unsigned i = 0; i < n; i++) buf[i] = ZuFwd<L>(l)(fbb, i);
    auto r = fbb.CreateVectorOfSortedTables(buf.data(), n);
    return r;
  }

  // inline creation of a string (shorthand alias for CreateString)
  template <typename Builder>
  inline auto str(Builder &fbb, ZuCSpan s) {
    return fbb.CreateString(s.data(), s.length());
  }
  // fixed-width string -> span<const uint8_t>
  template <unsigned N>
  inline auto strN(ZuCSpan s) -> span<const uint8_t, N> {
    return {span<const uint8_t, N>{
      reinterpret_cast<const uint8_t *>(s.data()), N}};
  }

  // inline creation of a vector of strings
  template <typename Builder, typename ...Args>
  inline auto strVec(Builder &fbb, Args &&...args) {
    return lvector<String>(fbb, [&fbb](const auto &s) {
      return str(fbb, s);
    }, ZuFwd<Args>(args)...);
  }
  // iterated creation of a vector of strings
  template <typename Builder, typename L>
  inline auto strVecIter(Builder &fbb, unsigned n, L &&l) {
    return vectorIter<String>(fbb, n,
      [&l](Builder &fbb, unsigned i) mutable {
	return str(fbb, ZuFwd<L>(l)(i));
      });
  }

  // inline creation of a vector of bytes from raw data
  template <typename Builder>
  inline auto bytes(Builder &fbb, const void *data, unsigned len) {
    return fbb.CreateVector(static_cast<const uint8_t *>(data), len);
  }
  // inline creation of a vector of bytes from raw data
  template <typename Builder>
  inline auto bytes(Builder &fbb, ZuBSpan b) {
    return fbb.CreateVector(b.data(), b.length());
  }

  // save file
  ZfbExtern int save(
      const Zi::Path &path, Builder &fbb, unsigned mode, ZeError *e);

  // nest flatbuffer
  // - l(Builder &fbb) must return Offset<RootType>
  // - this is a zero-copy flatbuffer nesting that simulates
  //   Finish(), but without any provision for a file or size prefix
  namespace Nest {
    // circumvent minalign_ being a protected data member
    struct Builder : public Zfb::Builder {
      auto alignment() const { return Builder::minalign_; }
    };
    inline auto alignment(const Zfb::Builder &fbb) {
      return static_cast<const Builder &>(fbb).alignment();
    }
  }
  template <typename L>
  inline Offset<Vector<uint8_t>> nest(Builder &fbb, L &&l) {
    auto o = fbb.GetSize();
    uoffset_t root = ZuFwd<L>(l)(fbb).o;
    if (ZuUnlikely(!root)) return {};
    fbb.PreAlign(sizeof(uoffset_t), Nest::alignment(fbb));
    fbb.PushElement(fbb.ReferTo(root));
    o = fbb.GetSize() - o;
    return {fbb.PushElement(o)};
  }

} // Save

namespace Load {
  using namespace Zfb; // needed for using namespace Zfb::Load

  // shorthand iteration over flatbuffer [T] vectors
  template <typename T, typename L>
  inline void all(T *v, L &&l) {
    if (ZuLikely(v))
      for (unsigned i = 0, n = v->size(); i < n; i++)
	ZuFwd<L>(l)(i, v->Get(i));
  }

  // inline zero-copy conversion of a FB string to a ZuCSpan
  inline ZuCSpan str(const String *s) {
    if (!s) return {};
    return ZuBSpan{s->Data(), s->size()};
  }
  // inline zero-copy conversion of a fixed-width FB string to a ZuCSpan
  template <unsigned N>
  inline ZuCSpan strN(const Array<uint8_t, N> *s) {
    if (!s) return {};
    ZuCSpan data = ZuBSpan{s->Data(), N};
    if (data[N-1]) return data;
    return {data.data()}; // deferred strlen
  }

  // inline zero-copy conversion of a [uint8] to a ZuBSpan
  inline ZuBSpan bytes(const Vector<uint8_t> *v) {
    if (!v) return {};
    return {v->data(), v->size()};
  }

  // load file
  using LoadFn = ZmFn<bool(ZuBSpan), ZmFnHeapID<"Zfb.LoadFn">>;
  ZfbExtern int load(
    const Zi::Path &path, LoadFn, Zi::Offset maxSize, ZeError *e);

} // Load

} // Zfb

#define ZfbEnum_(ID, Value) ZuAssert(int(Value) == int(fbs::ID::Value));
#define ZfbEnum(API, ID, ...) \
  ZtEnum(API, ID, ZuUnder<fbs::ID>, __VA_ARGS__); \
  enum { MIN = int(fbs::ID::MIN), MAX = int(fbs::ID::MAX) }; \
  ZuPP_Eval(ZuPP_MapArg(ZfbEnum_, ID, __VA_ARGS__))

#define ZfbEnumNS(API, ID, ...) \
  namespace ID { ZfbEnum(API, ID, __VA_ARGS__); }

#define ZfbEnumStruct(API, ID, ...) \
  struct ID { ZfbEnum(API, ID, __VA_ARGS__); }

#define ZfbEnumMatch_Assert(Namespace, Value) \
  ZuAssert(int(Value) == int(Namespace::Value));

#define ZfbEnumMatch_(Namespace, ...) \
  ZuPP_Eval(ZuPP_MapArg(ZfbEnumMatch_Assert, Namespace, __VA_ARGS__))

#define ZfbEnumMatch(API, ID, Namespace, ...) \
  ZfbEnum(API, ID, __VA_ARGS__) \
  ZfbEnumMatch_(Namespace, __VA_ARGS__)

#define ZfbEnumMatchNS(API, ID, Namespace, ...) \
  namespace ID { ZfbEnumMatch(API, ID, Namespace, __VA_ARGS__); }

#define ZfbEnumMatchStruct(API, ID, Namespace, ...) \
  struct ID { ZfbEnumMatch(API, ID, Namespace, __VA_ARGS__); }

#define ZfbEnum_Type(T) fbs::T
#define ZfbEnum_Assert(T) ZuAssert(int(T) == int(TypeIndex<fbs::T>{}));
#define ZfbEnumUnion(API, ID, ...) \
  ZfbEnum(API, ID, NONE, __VA_ARGS__) \
  using Types = \
    ZuTypeList<void, ZuPP_Eval(ZuPP_MapComma(ZfbEnum_Type, __VA_ARGS__))>; \
  template <unsigned I> using Type = ZuType<I, Types>; \
  template <typename T> using TypeIndex = ZuTypeIndex<T, Types>; \
  ZuPP_Eval(ZuPP_Map(ZfbEnum_Assert, __VA_ARGS__))

#define ZfbEnumUnionNS(API, ID, ...) \
  namespace ID { ZfbEnumUnion(API, ID, __VA_ARGS__); }

#define ZfbEnumUnionStruct(API, ID, ...) \
  struct ID { ZfbEnumUnion(API, ID, __VA_ARGS__); }

#endif /* Zfb_HH */
