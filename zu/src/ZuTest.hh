//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TAP-conformant test framework
// - https://testanything.org/tap-version-14-specification.html

#ifndef ZuTest_HH
#define ZuTest_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuSpan.hh>

struct ZuTest {
  const char	*file;
  unsigned	line;
  const char	*expr;
};

// intentionally single-threaded
class ZuAPI ZuTestMgr {
public:
  static ZuTestMgr &instance();

  void begin();

  unsigned count() const;
  unsigned idFor(const ZuTest *d) const;

  void run(const ZuTest *d, bool ok, ZuCSpan desc);

  void registerRange(const ZuTest *begin, const ZuTest *end);

private:
  struct Range_;

  Range_	*m_head = nullptr;
  Range_	*m_tail = nullptr;
  unsigned	m_total = 0;
  bool		m_finalized = false;
  bool		m_begun = false;

  ZuTestMgr();
  ZuTestMgr(const ZuTestMgr &) = delete;
  ZuTestMgr &operator =(const ZuTestMgr &) = delete;

  void finalize_();
  unsigned idFor_(const ZuTest *d) const;
};

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef _WIN32

#if __has_attribute(retain) || \
    (defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11)
#define ZuTest_Retain __attribute__((retain))
#else
#define ZuTest_Retain
#endif

#define ZuTest_SectionAttr __attribute__((section("ZuTest"), used)) \
  ZuTest_Retain

extern "C" {
  extern const ZuTest __start_ZuTest[] __attribute__((weak));
  extern const ZuTest __stop_ZuTest[] __attribute__((weak));
}

#else /* _WIN32 */

#define ZuTest_SectionAttr __attribute__((section(".ztest$M"), used))

extern "C" {
  __attribute__((section(".ztest$A"), used))
  inline const ZuTest ZuTest_begin = { nullptr, 0, nullptr };

  __attribute__((section(".ztest$Z"), used))
  inline const ZuTest ZuTest_end = { nullptr, 0, nullptr };
}

#endif /* _WIN32 */

#define ZuTest_Join__(a, b) a##b
#define ZuTest_Join_(a, b) ZuTest_Join__(a, b)

#define ZuCheck__(x, id) do { \
  static constinit const ZuTest \
    ZuTest_##id ZuTest_SectionAttr = { \
      __FILE__, __LINE__, #x \
    }; \
  ZuTestMgr::instance().run(ZuTest_##id, (x), #x); \
} while (0)

#define ZuCheck_(x, id) ZuCheck__(x, id)
#define ZuCheck(x) ZuCheck_(x, __LINE__)

namespace ZuTest_ {
  inline void registerImage_()
  {
#ifndef _WIN32
    const ZuTest *begin = __start_ZuTest;
    const ZuTest *end = __stop_ZuTest;
#else
    const ZuTest *begin = &ZuTest_begin + 1;
    const ZuTest *end = &ZuTest_end;
#endif
    if (!begin || !end || begin == end) return;
    ZuTestMgr::instance().registerRange(begin, end);
  }

  struct Register_ {
    Register_() { registerImage_(); }
  };

  __attribute__((used))
  inline Register_ register_;
}

#endif /* ZuTest_HH */
