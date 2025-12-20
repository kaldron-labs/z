//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// generic printing, with support for output to non-Z strings / streams

// struct UDT {
//   template <typename S> void print(S &s) const { ... }
//   friend ZuPrintFn ZuPrintType(Nested *);
// }

#ifndef ZuPrint_HH
#define ZuPrint_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTraits.hh>

struct ZuPrintable { };

struct ZuPrintCannot {
  enum { OK = 0, String = 0, Delegate = 0, Buffer = 0 };
};

struct ZuPrintFn {
  enum { OK = 1, String = 0, Delegate = 1, Buffer = 0 };
  template <typename S, typename T>
  static void print(S &s, const T &v) { v.print(s); }
};

template <auto Fn>
struct ZuPrintLambda {
  enum { OK = 1, String = 0, Delegate = 1, Buffer = 0 };
  template <typename S, typename T>
  static void print(S &s, const T &v) { Fn()(s, v); }
};

ZuPrintCannot ZuPrintType(...);

template <typename U>
struct ZuPrintT_ { using T = U; };
template <typename U>
struct ZuPrintT_<U *> { using T = typename ZuPrintT_<ZuDecay<U>>::T *; };
template <typename U>
using ZuPrintT = typename ZuPrintT_<ZuDecay<U>>::T *;

template <typename U>
using ZuPrint = decltype(ZuPrintType(ZuDeclVal<ZuPrintT<U>>()));

ZuPrintFn ZuPrintType(ZuPrintable *);

struct ZuPrintString {
  enum { OK = 1, String = 1, Delegate = 0, Buffer = 0 };
};
struct ZuPrintDelegate {
  enum { OK = 1, String = 0, Delegate = 1, Buffer = 0 };
};
struct ZuPrintBuffer {
  enum { OK = 1, String = 0, Delegate = 0, Buffer = 1 };
};

struct ZuPrintNull : public ZuPrintable {
  template <typename S> void print(S &) const { }
};

template <typename T>
struct ZuPrintPtr {
  T	*ptr;
  template <typename S> void print(S &s) const {
    if (!ptr)
      s << "(null)";
    else
      s << *ptr;
  }
  friend ZuPrintFn ZuPrintType(ZuPrintPtr *);
};
template <typename T> ZuPrintPtr(T *) -> ZuPrintPtr<T>;

#include <iostream>

template <typename Impl, typename S> struct ZuStdStream_ {
  enum { OK = 1 };
  template <typename P>
  static ZuIfT<ZuPrint<P>::String> print(S &s, const P &p) {
    const typename ZuTraits<P>::Elem *ptr = ZuTraits<P>::data(p);
    if (ZuLikely(ptr)) Impl::append(s, ptr, ZuTraits<P>::length(p));
  }
  template <typename P>
  static ZuIfT<ZuPrint<P>::Delegate> print(S &s, const P &p) {
    ZuPrint<P>::print(s, p);
  }
  template <typename P>
  static ZuIfT<ZuPrint<P>::Buffer> print(S &s, const P &p) {
    unsigned len = ZuPrint<P>::length(p);
    auto buf = static_cast<char *>(ZuAlloca(len, 1));
    if (ZuLikely(buf)) Impl::append(s, buf, ZuPrint<P>::print(buf, len, p));
  }
};

#include <iostream>

#include <zlib/ZuStdString.hh>

template <typename S, bool = ZuIsBase<S, std::ios_base>{}>
struct ZuStdStream { enum { OK = 0 }; };
template <typename S>
struct ZuStdStream<S, true> : public ZuStdStream_<ZuStdStream<S, true>, S> {
  static S &append(S &s, const char *data, size_t length) {
    if (ZuLikely(data)) s.write(data, length);
    return s;
  }
};
template <typename T, typename A>
struct ZuStdStream<std::basic_string<char, T, A>, false> :
    public ZuStdStream_<
      ZuStdStream<std::basic_string<char, T, A>, false>,
      std::basic_string<char, T, A>> {
  using S = std::basic_string<char, T, A>;
  static S &append(S &s, const char *data, size_t length) {
    if (ZuLikely(data)) s.append(data, length);
    return s;
  }
};

template <typename S, typename P, bool = ZuStdStream<S>::OK>
struct ZuStdStreamable { enum { OK = 0 }; };
template <typename S, typename P>
struct ZuStdStreamable<S, P, true> { enum { OK = ZuPrint<P>::OK }; };

template <typename S, typename P>
inline ZuIfT<ZuStdStreamable<S, P>::OK, S &>
operator <<(S &s, const P &p) { ZuStdStream<S>::print(s, p); return s; }

template <typename S, typename P>
inline ZuIfT<ZuStdStreamable<S, P>::OK, S &>
operator +=(S &s, const P &p) { ZuStdStream<S>::print(s, p); return s; }

#endif /* ZuPrint_HH */
