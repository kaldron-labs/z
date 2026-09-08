//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

/* benchmark program */

#include <stdlib.h>

#include <iostream>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTime.hh>

template <typename... Ts>
void out(const Ts &...values) {
  (std::cout << ... << values) << '\n';
}

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

ZmHashKVDerive(PerfHash, unsigned, String<16>, (ZmHashLock<ZmLock>));
ZmLHashKVDerive(PerfLHash, unsigned, String<16>, ZmLHashLock<ZmLock>);

unsigned perfTestSize = 1000;
unsigned concurrency = 1;

template <typename H> void hashIt(H *h)
{
  String<16> s = "Hello World", t = "Goodbye World";

  for (unsigned i = 0; i < perfTestSize; i++) h->add(i, s);
  for (unsigned i = 0; i < perfTestSize; i++) h->findAdd(i, t);
  for (unsigned i = 0; i < perfTestSize; i++) h->del(i);
  for (unsigned i = 0; i < perfTestSize; i++) h->add(i, s), h->del(i);
  for (unsigned i = 0; i < perfTestSize; i++) h->findAdd(i, t);
  for (unsigned i = 0; i < perfTestSize; i++) h->del(i, t);
  for (unsigned i = 0; i < perfTestSize; i++) h->findAdd(i, t), h->del(i, t);
}

template <typename H> void perfTest_(int bits)
{
  ZmThread threads[16];
  unsigned n = concurrency;

  if (n > 16) n = 16;

  ZmRef<H> h = new H(ZmHashParams().bits(bits).loadFactor(1.0));

  for (unsigned i = 0; i < n; i++)
    threads[i] = ZmThread{[h]() { hashIt<H>(h.ptr()); }};
  for (unsigned i = 0; i < n; i++) threads[i].join(0);
}

template <typename H> void perfTest()
{
  for (unsigned bits = 8; bits < 12; bits++) perfTest_<H>(bits);
}

int main(int argc, char **argv)
{
  if (argc > 1) perfTestSize = atoi(argv[1]);
  if (argc > 2) concurrency = atoi(argv[2]);

  out("perfTestSize=", perfTestSize, " concurrency=", concurrency);

  ZuTime start = Zm::now();
  for (unsigned i = 0; i < 10; i++) perfTest<PerfHash>();
  ZuTime end = Zm::now();
  end -= start;

  out("ZmHash time=", end.interval());
  start = Zm::now();
  for (unsigned i = 0; i < 10; i++) perfTest<PerfLHash>();
  end = Zm::now();
  end -= start;

  out("ZmLHash time=", end.interval());
}
