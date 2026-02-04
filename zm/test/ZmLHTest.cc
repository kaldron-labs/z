//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

/* test program */

#include <zlib/ZuLib.hh>

#include <iostream>

#include <stdlib.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmDemangle.hh>

void out(const char *s) { std::cout << s << '\n' << std::flush; }
void fail() { }

#define CHECK(x) ((x) ? out("OK  " #x) : (fail(), out("NOK " #x)))

template <int N> struct String {
  String() { }
  String(const String &s) { memcpy(m_data, s.m_data, N); }
  String &operator =(const String &s) {
    if (this == &s) return *this;
    memcpy(m_data, s.m_data, N);
    return *this;
  }
  ~String() { }
  String(const char *s) {
    if (!s) { m_data[0] = 0; return; }
    strncpy(m_data, s, N);
    m_data[N - 1] = 0;
  }
  operator const char *() const { return m_data; }
  operator char *() { return m_data; }
  const char *data() const { return m_data; }
  char *data() { return m_data; }
  unsigned length() const { return strlen(m_data); }
  bool operator !() const { return !m_data[0]; }
  template <typename S>
  int cmp(const S &s) const {
    return ZuCmp<String>::cmp(*this, s);
  }
  template <typename S>
  bool equals(const S &s) const {
    return ZuCmp<String>::equals(*this, s);
  }
  template <typename L, typename R>
  friend inline
  ZuIfT<ZuIs_<L, String>{} && ZuTraits<R>::IsString, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend inline
  ZuIfT<ZuIs_<L, String>{} && ZuTraits<R>::IsString, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

  uint32_t hash() const { return ZuHash<String>::hash(*this); }

  struct Traits : public ZuBaseTraits<String> {
    using Elem = char;
    enum { IsPOD = 1, IsCString = 1, IsString = 1 };
    static char *data(String<N> &s) { return s.data(); }
    static const char *data(const String<N> &s) { return s.data(); }
    static unsigned length(const String<N> &s) { return s.length(); }
  };
  friend Traits ZuTraitsType(String *);

  char	m_data[N];
};

ZuDerive(S, String<16>);

ZuDerive(Hash, (ZmHashKV<S, int, ZmHashLock<ZmNoLock>>));
ZuDerive(LHash, (ZmLHashKV<S, int, ZmLHashLock<ZmNoLock>>));

template <typename H>
struct HashAdapter {
  using T = typename H::NodeRef;
  static const typename H::Key &key(const typename H::Node *n) {
    return n->key();
  }
  static const typename H::Val &val(const typename H::Node *n) {
    return n->val();
  }
};
template <typename H>
struct LHashAdapter {
  using T = const typename H::T *;
  static decltype(auto) key(T ptr) { return H::KeyAxor(*ptr); }
  static decltype(auto) val(T ptr) { return H::ValAxor(*ptr); }
};

template <typename H> void add(H &h)
{
  h.add("Hello", 42);
  h.add("Hello", 43);
  h.add("Hello", 44);
}

template <typename H> void add5(H &h)
{
  h.add("Hello", 42);
  h.add("Hello", 43);
  h.add("Hello", 44);
  h.add("Hello", 45);
  h.add("Hello", 46);
}

template <typename H> void del(H &h, int i)
{
  h.del("Hello", i);
}

template <typename H> void iter(H &h, int check, int del = -1)
{
  auto i = h.iter("Hello");
  int j;
  int total = 0;
  while (!ZuCmp<int>::null(j = i.val())) {
    total += j;
    if (j == del) i.del();
  }
  std::cout << "total=" << total << " check=" << check << '\n' << std::flush;
  CHECK(total == check);
}

template <typename H> void iter2(H &h, int check, int del = -1)
{
  typename H::Iter i(h);
  int j;
  int total = 0;
  while (!ZuCmp<int>::null(j = i.val())) {
    if (j >= 0) total += j;
    if (j == del) i.del();
  }
  std::cout << "total=" << total << " check=" << check << '\n' << std::flush;
  CHECK(total == check);
}

template <typename H> void iterDel(H &h)
{
}

template <typename H, template <typename> class A>
void funcTest_(int bits, double loadFactor)
{
  ZmRef<H> h_ = new H{ZmHashParams{}.bits(bits).loadFactor(loadFactor)};
  H &h = *h_;
  std::cout << "funcTest_<" << ZmDemangle<H>{} << ", " << ZmDemangle<A<H>>{} << ">(" << bits << ", " << loadFactor << ")\n";

  h.add("Goodbye", -42);
  CHECK(A<H>::val(typename A<H>::T{h.find("Goodbye")}) == -42);
  add(h), iter(h, 42+43+44);
  out("DEL 42 43 44");
  del(h, 42), iter(h, 43+44), del(h, 43), iter(h, 44), del(h, 44), iter(h, 0);
  CHECK(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 42 44 43");
  del(h, 42), iter(h, 43+44), del(h, 44), iter(h, 43), del(h, 43), iter(h, 0);
  CHECK(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 43 42 44");
  del(h, 43), iter(h, 42+44), del(h, 42), iter(h, 44), del(h, 44), iter(h, 0);
  CHECK(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 43 44 42");
  del(h, 43), iter(h, 42+44), del(h, 44), iter(h, 42), del(h, 42), iter(h, 0);
  CHECK(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 44 42 43");
  del(h, 44), iter(h, 42+43), del(h, 42), iter(h, 43), del(h, 43), iter(h, 0);
  CHECK(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 44 43 42");
  del(h, 44), iter(h, 42+43), del(h, 43), iter(h, 42), del(h, 42), iter(h, 0);
  CHECK(h.count_() == 1);
  add5(h);
  out("DEL 44 43 45 [42->46]");
  del(h, 44), iter(h, 42+43+45+46), del(h, 43), iter(h, 42+45+46),
  del(h, 45), iter(h, 42+46), del(h, 42), del(h, 46);
  CHECK(h.count_() == 1);
  add5(h);
  out("DEL 44 45 43 [42->46]");
  del(h, 44), iter(h, 42+43+45+46), del(h, 45), iter(h, 42+43+46),
  del(h, 43), iter(h, 42+46), del(h, 46), del(h, 42);
  CHECK(h.count_() == 1);
  h.findAdd("Goodbye", -46);
  {
    auto v = A<H>::val(typename A<H>::T{h.find("Goodbye")});
    CHECK(v == -42 || v == -46);
  }
  h.del("Goodbye", -42);
  h.findAdd("Goodbye", -46);
  CHECK(A<H>::val(typename A<H>::T{h.find("Goodbye")}) == -46);
  {
    auto v = A<H>::val(typename A<H>::T{h.find("Goodbye")});
    CHECK(v == -42 || v == -46);
  }
  CHECK(h.count_() == 1);

  out("ITERDEL 44 43 42");
  add(h);
  iter(h, 42+43+44, 44);
  iter(h, 42+43, 43);
  iter(h, 42, 42);
  CHECK(h.count_() == 1);
  out("ITERDEL 43 44 42");
  add(h); iter(h, 42+43+44, 43); iter(h, 42+44, 44); iter(h, 42, 42);
  CHECK(h.count_() == 1);
  out("ITERDEL 42 44 43");
  add(h); iter(h, 42+43+44, 42); iter(h, 43+44, 44); iter(h, 43, 43);
  CHECK(h.count_() == 1);
  out("ITERDEL 44 42 43");
  add(h); iter(h, 42+43+44, 44); iter(h, 42+43, 42); iter(h, 43, 43);
  CHECK(h.count_() == 1);
  out("ITERDEL 43 42 44");
  add(h); iter(h, 42+43+44, 43); iter(h, 42+44, 42); iter(h, 44, 44);
  CHECK(h.count_() == 1);
  out("ITERDEL 42 43 44");
  add(h); iter(h, 42+43+44, 42); iter(h, 43+44, 43); iter(h, 44, 44);
  CHECK(h.count_() == 1);

  out("ITERDEL2 44 43 42");
  add(h); iter2(h, 42+43+44, 44); iter2(h, 42+43, 43); iter2(h, 42, 42);
  CHECK(h.count_() == 1);
  out("ITERDEL2 43 44 42");
  add(h); iter2(h, 42+43+44, 43); iter2(h, 42+44, 44); iter2(h, 42, 42);
  CHECK(h.count_() == 1);
  out("ITERDEL2 42 44 43");
  add(h); iter2(h, 42+43+44, 42); iter2(h, 43+44, 44); iter2(h, 43, 43);
  CHECK(h.count_() == 1);
  out("ITERDEL2 44 42 43");
  add(h); iter2(h, 42+43+44, 44); iter2(h, 42+43, 42); iter2(h, 43, 43);
  CHECK(h.count_() == 1);
  out("ITERDEL2 43 42 44");
  add(h); iter2(h, 42+43+44, 43); iter2(h, 42+44, 42); iter2(h, 44, 44);
  CHECK(h.count_() == 1);
  out("ITERDEL2 42 43 44");
  add(h); iter2(h, 42+43+44, 42); iter2(h, 43+44, 43); iter2(h, 44, 44);
  CHECK(h.count_() == 1);
}

template <typename H, template <typename> class A> void funcTest()
{
  for (unsigned bits = 1; bits < 8; bits++) {
    funcTest_<H, A>(bits, 0.5);
    funcTest_<H, A>(bits, 1.0);
  }
}

ZuDerive(PerfHash, (ZmHashKV<unsigned, String<16>, ZmHashLock<ZmLock> >));
ZuDerive(PerfLHash, (ZmLHashKV<unsigned, String<16>, ZmLHashLock<ZmLock> >));

unsigned perfTestSize = 1000;

template <typename H> void hashIt(H *h)
{
  String<16> s = "Hello World", t = "Goodbye World";
  unsigned i;

  std::cout << "perfTestSize=" << perfTestSize << '\n' << std::flush;

  for (i = 0; i < perfTestSize; i++) h->add(i, s);
  for (i = 0; i < perfTestSize; i++) h->findAdd(i, t);
  for (i = 0; i < perfTestSize; i++) h->del(i);
  for (i = 0; i < perfTestSize; i++) h->add(i, s), h->del(i);
  for (i = 0; i < perfTestSize; i++) h->findAdd(i, t);
  for (i = 0; i < perfTestSize; i++) h->del(i, t);
  for (i = 0; i < perfTestSize; i++) h->findAdd(i, t), h->del(i, t);
}

unsigned concurrency = 1;

template <typename H, template <typename> class A> void perfTest_(int bits)
{
  ZmThread threads[16];
  unsigned n = concurrency;

  std::cout << "concurrency=" << concurrency << '\n' << std::flush;

  if (n > 16) n = 16;

  ZmRef<H> h = new H(ZmHashParams().bits(bits).loadFactor(1.0));

  for (unsigned i = 0; i < n; i++)
    threads[i] = ZmThread{[h]() { hashIt<H>(h.ptr()); }};
  for (unsigned i = 0; i < n; i++) threads[i].join(0);
}

template <typename H, template <typename> class A> void perfTest()
{
  for (unsigned bits = 8; bits < 12; bits++) perfTest_<H, A>(bits);
}

int main(int argc, char **argv)
{
  ZuTime start, end;

  funcTest<Hash, HashAdapter>();
  funcTest<LHash, LHashAdapter>();

  if (argc > 1) perfTestSize = atoi(argv[1]);
  if (argc > 2) concurrency = atoi(argv[2]);

  std::cout
    << "perfTestSize=" << perfTestSize
    << " concurrency=" << concurrency << '\n' << std::flush;

  start = Zm::now();
  for (unsigned i = 0; i < 10; i++) perfTest<PerfHash, HashAdapter>();
  end = Zm::now();
  end -= start;

  std::cout << "ZmHash time=" << end.interval() << '\n';

  start = Zm::now();
  for (unsigned i = 0; i < 10; i++) perfTest<PerfLHash, LHashAdapter>();
  end = Zm::now();
  end -= start;

  std::cout << "ZmLHash time=" << end.interval() << '\n';
}
