//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// hex dump
//
// ZuBSpan b{ptr, length};
// std::cout << ZtHexDump_{b} << std::flush;		// b not copied
// std::cout << ZtHexDump{"b", b} << std::flush;	// b is copied
//
// ZtHexDump_ is a low-level hex dumper by reference that does NOT copy
// prefix or data
//
// ZtHexDump takes a copy of the prefix and data - this is used for
// troubleshooting / logging; ZiLog printing is deferred to a later time
// by the logger, which runs in a different thread and stack; both the
// prefix and the data need to reliably remain in scope until logged

#ifndef ZtHexDump_HH
#define ZtHexDump_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZtString.hh>

ZuDerive(ZtHexDumpStr, ZtString<ZtStringHeapID<"ZtHexDump">>);

template <typename T> struct ZtHexDump_Size { enum { N = sizeof(T) }; };
template <> struct ZtHexDump_Size<void> { enum { N = 1 }; };
class ZtAPI ZtHexDump_ {
protected:
  ZtHexDump_() = default;

public:
  template <typename T>
  ZtHexDump_(ZuSpan<T> data) :
      m_data{reinterpret_cast<const uint8_t *>(data.data())},
      m_length(data.length() * sizeof(T)) { }

  template <typename T>
  ZtHexDump_(const T *data, unsigned length) :
      m_data{reinterpret_cast<const uint8_t *>(data)},
      m_length{length * ZtHexDump_Size<T>::N} { }

  ZtHexDump_(const ZtHexDump_ &d) : m_data{d.m_data}, m_length{d.m_length} { }
  ZtHexDump_ &operator =(const ZtHexDump_ &d) {
    if (ZuLikely(this != &d)) {
      m_data = d.m_data;
      m_length = d.m_length;
    }
    return *this;
  }

public:
  void print(ZuVStream &s) const;
  struct Print : public ZuPrintDelegate {
    template <typename S>
    static void print(S &s_, const ZtHexDump_ &d) {
      ZuVStream s{s_};
      d.print(s);
    }
    static void print(ZuVStream &s, const ZtHexDump_ &d) {
      d.print(s);
    }
  };
  friend Print ZuPrintType(ZtHexDump_ *);

protected:
  const uint8_t	*m_data = nullptr;
  unsigned	m_length = 0;
};

class ZtHexDump : private ZmVHeap<"ZtHexDump">, public ZtHexDump_ {
  ZtHexDump() = delete;

public:
  template <typename T>
  ZtHexDump(ZtHexDumpStr prefix, ZuSpan<T> data) : m_prefix(ZuMv(prefix)) {
    m_length = data.length() * sizeof(T);
    init_(reinterpret_cast<const uint8_t *>(data.data()));
  }

  template <typename T>
  ZtHexDump(ZtHexDumpStr prefix, const T *data, unsigned length) :
      m_prefix{ZuMv(prefix)} {
    m_length = length * ZtHexDump_Size<T>::N;
    init_(reinterpret_cast<const uint8_t *>(data));
  }

  ~ZtHexDump() {
    if (ZuLikely(m_data)) vfree(m_data);
  }

  ZtHexDump(ZtHexDump &&d) noexcept : m_prefix{ZuMv(d.m_prefix)} {
    m_data = d.m_data;
    m_length = d.m_length;
    d.m_prefix = "";
    d.m_data = nullptr;
    d.m_length = 0;
  }
  ZtHexDump &operator =(ZtHexDump &&d) noexcept {
    if (ZuLikely(this != &d)) {
      m_prefix = ZuMv(d.m_prefix);
      m_data = d.m_data;
      m_length = d.m_length;
      d.m_prefix = "";
      d.m_data = nullptr;
      d.m_length = 0;
    }
    return *this;
  }
  ZtHexDump(const ZtHexDump &) = delete;
  ZtHexDump &operator =(const ZtHexDump &) = delete;

private:
  void init_(const uint8_t *data_) {
    uint8_t *data;
    if (ZuLikely(data = static_cast<uint8_t *>(valloc(m_length)))) {
      memcpy(data, data_, m_length);
      m_data = data;
    } else {
      m_data = nullptr;
      m_length = 0;
    }
  }

public:
  struct Print : public ZuPrintDelegate {
    template <typename S>
    static void print(S &s, const ZtHexDump &d) {
      s << d.m_prefix << static_cast<const ZtHexDump_ &>(d);
    }
  };
  friend Print ZuPrintType(ZtHexDump *);

private:
  ZtHexDumpStr	m_prefix;
};

#endif /* ZtHexDump_HH */
