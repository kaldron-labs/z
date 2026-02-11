//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmDemangle.hh>

using namespace ZuTestUtil;

void out(const char *s) { log(s); }

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
  log("total=", total, " check=", check);
  ZuCheckRT(total == check);
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
  log("total=", total, " check=", check);
  ZuCheckRT(total == check);
}

template <typename H, template <typename> class A>
void funcTest_(int bits, double loadFactor)
{
  ZmRef<H> h_ = new H{ZmHashParams{}.bits(bits).loadFactor(loadFactor)};
  H &h = *h_;
  log("funcTest_<", ZmDemangle<H>{}, ", ", ZmDemangle<A<H>>{}, ">(", bits, ", ", loadFactor, ")");
  h.add("Goodbye", -42);
  ZuCheckRT(A<H>::val(typename A<H>::T{h.find("Goodbye")}) == -42);
  add(h), iter(h, 42+43+44);
  out("DEL 42 43 44");
  del(h, 42), iter(h, 43+44), del(h, 43), iter(h, 44), del(h, 44), iter(h, 0);
  ZuCheckRT(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 42 44 43");
  del(h, 42), iter(h, 43+44), del(h, 44), iter(h, 43), del(h, 43), iter(h, 0);
  ZuCheckRT(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 43 42 44");
  del(h, 43), iter(h, 42+44), del(h, 42), iter(h, 44), del(h, 44), iter(h, 0);
  ZuCheckRT(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 43 44 42");
  del(h, 43), iter(h, 42+44), del(h, 44), iter(h, 42), del(h, 42), iter(h, 0);
  ZuCheckRT(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 44 42 43");
  del(h, 44), iter(h, 42+43), del(h, 42), iter(h, 43), del(h, 43), iter(h, 0);
  ZuCheckRT(h.count_() == 1);
  add(h), iter(h, 42+43+44);
  out("DEL 44 43 42");
  del(h, 44), iter(h, 42+43), del(h, 43), iter(h, 42), del(h, 42), iter(h, 0);
  ZuCheckRT(h.count_() == 1);
  add5(h);
  out("DEL 44 43 45 [42->46]");
  del(h, 44), iter(h, 42+43+45+46), del(h, 43), iter(h, 42+45+46),
  del(h, 45), iter(h, 42+46), del(h, 42), del(h, 46);
  ZuCheckRT(h.count_() == 1);
  add5(h);
  out("DEL 44 45 43 [42->46]");
  del(h, 44), iter(h, 42+43+45+46), del(h, 45), iter(h, 42+43+46),
  del(h, 43), iter(h, 42+46), del(h, 46), del(h, 42);
  ZuCheckRT(h.count_() == 1);
  h.findAdd("Goodbye", -46);
  {
    auto v = A<H>::val(typename A<H>::T{h.find("Goodbye")});
    ZuCheckRT(v == -42 || v == -46);
  }
  h.del("Goodbye", -42);
  h.findAdd("Goodbye", -46);
  ZuCheckRT(A<H>::val(typename A<H>::T{h.find("Goodbye")}) == -46);
  {
    auto v = A<H>::val(typename A<H>::T{h.find("Goodbye")});
    ZuCheckRT(v == -42 || v == -46);
  }
  ZuCheckRT(h.count_() == 1);

  out("ITERDEL 44 43 42");
  add(h);
  iter(h, 42+43+44, 44);
  iter(h, 42+43, 43);
  iter(h, 42, 42);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL 43 44 42");
  add(h); iter(h, 42+43+44, 43); iter(h, 42+44, 44); iter(h, 42, 42);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL 42 44 43");
  add(h); iter(h, 42+43+44, 42); iter(h, 43+44, 44); iter(h, 43, 43);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL 44 42 43");
  add(h); iter(h, 42+43+44, 44); iter(h, 42+43, 42); iter(h, 43, 43);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL 43 42 44");
  add(h); iter(h, 42+43+44, 43); iter(h, 42+44, 42); iter(h, 44, 44);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL 42 43 44");
  add(h); iter(h, 42+43+44, 42); iter(h, 43+44, 43); iter(h, 44, 44);
  ZuCheckRT(h.count_() == 1);

  out("ITERDEL2 44 43 42");
  add(h); iter2(h, 42+43+44, 44); iter2(h, 42+43, 43); iter2(h, 42, 42);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL2 43 44 42");
  add(h); iter2(h, 42+43+44, 43); iter2(h, 42+44, 44); iter2(h, 42, 42);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL2 42 44 43");
  add(h); iter2(h, 42+43+44, 42); iter2(h, 43+44, 44); iter2(h, 43, 43);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL2 44 42 43");
  add(h); iter2(h, 42+43+44, 44); iter2(h, 42+43, 42); iter2(h, 43, 43);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL2 43 42 44");
  add(h); iter2(h, 42+43+44, 43); iter2(h, 42+44, 42); iter2(h, 44, 44);
  ZuCheckRT(h.count_() == 1);
  out("ITERDEL2 42 43 44");
  add(h); iter2(h, 42+43+44, 42); iter2(h, 43+44, 43); iter2(h, 44, 44);
  ZuCheckRT(h.count_() == 1);
}

template <typename H, template <typename> class A> void funcTest()
{
  for (unsigned bits = 1; bits < 8; bits++) {
    funcTest_<H, A>(bits, 0.5);
    funcTest_<H, A>(bits, 1.0);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  funcTest<Hash, HashAdapter>();
  funcTest<LHash, LHashAdapter>();
}
