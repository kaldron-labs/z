//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Union of a 64bit integer and a human-readable string
// (8-byte left-aligned zero-padded)

#ifndef ZuID_HH
#define ZuID_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSpan.hh>

// A ZuID is a union of a 64bit unsigned integer with an 8-byte
// zero-padded string; this permits short human-readable string identifiers
// to be compared and hashed very rapidly using integer 64bit operations
// without needing a map between names and numbers

// Note: the string will not be null-terminated if all 8 bytes are in use

// the implementation uses misaligned 64bit loads where available
// (i.e. any recent x86 64bit)

class ZuID {
public:
  constexpr ZuID() noexcept : m_val{.u = 0} { }

  constexpr ZuID(const ZuID &b) noexcept : m_val{.u = b.m_val.u} { }
  constexpr ZuID &operator =(const ZuID &b) noexcept {
    m_val.u = b.m_val.u;
    return *this;
  }

  template <typename S, decltype(ZuMatchString<S>(), int()) = 0>
  ZuID(S &&s) noexcept {
    init(ZuFwd<S>(s));
  }
  template <typename S>
  ZuMatchString<S, ZuID &> operator =(S &&s) noexcept {
    init(ZuFwd<S>(s));
    return *this;
  }

  // Note: convertibility and constructibility are the same for uint64_t
  template <typename V>
  struct IsUInt64 :
    public ZuBool<!ZuTraits<V>::IsString && ZuIsConvertible<V, uint64_t>{}> { };
  template <typename V, typename R = void>
  using MatchUInt64 = ZuIfT<IsUInt64<V>{}, R>;

  template <typename V, MatchUInt64<V, int> = 0>
  constexpr ZuID(V v) noexcept : m_val{.u = v} { }
  template <typename V>
  constexpr MatchUInt64<V, ZuID &> operator =(V v) noexcept {
    m_val.u = v;
    return *this;
  }

  void init(ZuCSpan s) {
    if (ZuLikely(s.length() == 8)) {
      auto ptr = ZuLaunder(reinterpret_cast<const uint64_t *>(s.data()));
#ifdef __x86_64__
      m_val.u = *ptr; // potentially misaligned load
#else
      memcpy(&m_val.b, ptr, 8);
#endif
      return;
    }
    m_val.u = 0;
    unsigned n = s.length();
    if (ZuUnlikely(!n)) return;
    if (ZuUnlikely(n > 8)) n = 8;
    memcpy(&m_val.b, s.data(), n);
  }

  char *data() { return &m_val.b[0]; }
  const char *data() const { return &m_val.b[0]; }

  unsigned length() const {
    if (!m_val.u) return 0U;
#if Zu_BIGENDIAN
    return (71U - ZuIntrin::ctz(m_val.u))>>3U;
#else
    return (71U - ZuIntrin::clz(m_val.u))>>3U;
#endif
  }

  operator ZuCSpan() const noexcept { return ZuCSpan(data(), length()); }

  ZuCSpan span() const { return ZuCSpan(data(), length()); }

  template <typename S> void print(S &s) const { s << span(); }

  constexpr operator uint64_t() const { return m_val.u; }

  constexpr int cmp(ZuID v) const {
    return (m_val.u > v.m_val.u) - (m_val.u < v.m_val.u);
  }
  template <typename L, typename R>
  friend constexpr ZuIs<L, ZuID, bool>
  operator ==(const L &l, const R &r) { return l.m_val.u == r.m_val.u; }
  template <typename L, typename R>
  friend constexpr ZuIs<L, ZuID, int>
  operator <(const L &l, const R &r) { return l.m_val.u < r.m_val.u; }
  template <typename L, typename R>
  friend constexpr ZuIs<L, ZuID, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

  constexpr bool operator !() const { return !m_val.u; }

  constexpr bool operator *() const { return m_val.u; }

  void null() { m_val.u = 0; }

  ZuID &update(ZuID id) {
    if (*id) m_val.u = id.m_val.u;
    return *this;
  }

  uint32_t hash() const { return ZuHash<uint64_t>::hash(m_val.u); }

  struct Traits : public ZuTraits<uint64_t> {
    enum { IsPrimitive = 0 };
  };
  friend Traits ZuTraitsType(ZuID *);

  friend ZuPrintFn ZuPrintType(ZuID *);

private:
  union {
    uint64_t	  u;
    char	  b[8];
  }		m_val;
};

// override ZuCmp to prevent default string-based comparison
template <> struct ZuCmp<ZuID> {
  template <typename L, typename R>
  static constexpr int cmp(const L &l, const R &r) { return l.cmp(r); }
  template <typename L, typename R>
  static constexpr bool equals(const L &l, const R &r) { return l == r; }
  template <typename L, typename R>
  static constexpr bool less(const L &l, const R &r) { return l < r; }
  static constexpr bool null(ZuID id) { return !id; }
  static constexpr ZuID null() { return {}; }
};

#endif /* ZuID_HH */
