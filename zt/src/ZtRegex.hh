//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Perl compatible regular expressions (pcre)

#ifndef ZtRegex_HH
#define ZtRegex_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <pcre.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZmCleanup.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmAlloc.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtLocalArray.hh>

struct ZtAPI ZtRegexError {
  const char	*message = 0;
  int		code;
  int		offset;

  static const char *strerror(int);

  template <typename S> void print(S &s) const {
    if (message) {
      s << "ZtRegex Error \"" << message << "\" (" << code << ")"
	" at offset " << offset;
    } else {
      s << "ZtRegex pcre_exec() Error: " << strerror(code);
    }
  }
  friend ZuPrintFn ZuPrintType(ZtRegexError *);
};

#define ZtRegexOVector(o, n) \
  auto o##_size = unsigned(n) * 3; \
  auto o = ZtLocalArray(ZtRegex::OVector, o##_size)

#define ZtRegexCaptures(c, n) \
  auto c##_size = unsigned(n) + 2; \
  auto c = ZtLocalArray(ZtRegex::Captures, c##_size)

class ZtAPI ZtRegex {
  ZtRegex(const ZtRegex &) = delete;
  ZtRegex &operator =(const ZtRegex &) = delete;

  ZtRegex();

public:
  using Capture = ZuCSpan;
  ZuDerive(Captures, (ZtArray<Capture, ZtArrayHeapID<"ZtRegex.Captures">>));
  ZuDerive(OVector, (ZtArray<unsigned, ZtArrayHeapID<"ZtRegex.OVector">>));

  // pcre_compile() options
  ZtRegex(const char *pattern, int options = PCRE_UTF8);

  // ZtRegex is move-only (by design)
  ZtRegex(ZtRegex &&r) :
      m_regex(r.m_regex), m_extra(r.m_extra), m_captureCount(r.m_captureCount) {
    r.m_regex = 0;
    r.m_extra = 0;
    r.m_captureCount = 0;
  }
  ZtRegex &operator =(ZtRegex &&r) {
    if (this == &r) return *this;
    m_regex = r.m_regex;
    m_extra = r.m_extra;
    m_captureCount = r.m_captureCount;
    r.m_regex = 0;
    r.m_extra = 0;
    r.m_captureCount = 0;
    return *this;
  }

  ~ZtRegex();

  void study();

  unsigned captureCount() const { return m_captureCount; }

  // options below are pcre_exec() options

  // captures[0] is $`
  // captures[1] is $&
  // captures[2] is $1
  // captures[n - 1] is $' (where n = number of captured substrings + 3)
  // n is always >= 3 for a successful match, or 0 for no match
  // return value is number of captures excluding $` and $', i.e. (n - 2)
  //   0 implies no match
  //   1 implies $`, $&, $' captured (n == 3)
  //   2 implies $`, $&, $1, $' captured (n == 4)
  // etc.
  /*
     $& is the entire matched string.
     $` is everything before the matched string.
     $' is everything after the matched string.
  */

  unsigned m(ZuCSpan s, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    return exec(s, offset, options, ovector);
  }
  unsigned m(ZuCSpan s,
      Captures &captures, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    unsigned i = exec(s, offset, options, ovector);
    if (i) capture(s, ovector, captures);
    return i;
  }
  template <typename R>
  unsigned mg(ZuCSpan s, R &&r, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    ZtRegexCaptures(captures, m_captureCount);
    unsigned n = 0;
    unsigned slength = s.length();

    while (offset < slength && exec(s, offset, options, ovector)) {
      capture(s, ovector, captures);
      r(captures);
      offset = ovector[1];
      if (!captures[1]) ++offset;
      options |= PCRE_NO_UTF8_CHECK;
      ++n;
    }

    return n;
  }

  template <typename L, typename = void>
  struct IsCallable : public ZuFalse { };
  template <typename L>
  struct IsCallable<L, decltype(ZuDeclVal<L &>()(
      ZuDeclVal<const Captures &>(), [](ZuCSpan) { }))> :
    public ZuTrue { };
  template <typename L, typename R = void>
  using MatchCallable = ZuIfT<IsCallable<L>{}, R>;
  template <typename L, typename R = void>
  using MatchNotCallable = ZuIfT<!IsCallable<L>{}, R>;

  template <typename S, typename R>
  MatchCallable<R, unsigned> s(S &s, R &&r, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    ZtRegexCaptures(captures, m_captureCount);
    unsigned i = exec(s, offset, options, ovector);
    if (i) {
      capture(s, ovector, captures);
      ZuFwd<R>(r)(captures, [&s, &ovector](ZuCSpan r) {
	s.splice(ovector[0], ovector[1] - ovector[0], r.data(), r.length());
      });
    }
    return i;
  }
  template <typename S, typename R>
  MatchNotCallable<R, unsigned> s(S &s, R &&r, unsigned offset = 0, int options = 0) const {
    return this->s(s, [r = ZuCSpan(r)]<typename Splice>(const Captures &, Splice &&splice) {
      splice(r);
    });
  }

  template <typename S, typename R>
  MatchCallable<R, unsigned> sg(S &s, R &&r, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    ZtRegexCaptures(captures, m_captureCount);
    unsigned n = 0;
    unsigned slength = s.length(), rlength;

    while (offset < slength && exec(s, offset, options, ovector)) {
      capture(s, ovector, captures);
      r(captures, [&s, &ovector, &rlength](ZuCSpan r) {
	rlength = r.length();
	s.splice(
	  [](auto span) { }, ovector[0], ovector[1] - ovector[0], [r](auto span) {
	    memcpy(span.data(), r.data(), span.length());
	    return span.length();
	  }, rlength);
      });
      offset = ovector[0] + rlength;
      if (!captures[1] && !rlength) ++offset;
      options |= PCRE_NO_UTF8_CHECK;
      ++n;
    }

    return n;
  }
  template <typename S, typename R>
  MatchNotCallable<R, unsigned> sg(S &s, R &&r, unsigned offset = 0, int options = 0) const {
    return sg(s, [r = ZuCSpan(r)]<typename Splice>(const Captures &, Splice &&splice) {
      splice(r);
    });
  }

  unsigned split(ZuCSpan s, Captures &a, int options = 0) const;

  int index(const char *name) const; // pcre_get_stringnumber()

private:
  unsigned exec(ZuCSpan s,
      unsigned offset, int options, OVector &ovector) const;
  void capture(ZuCSpan s,
      const OVector &ovector, Captures &captures) const;

  pcre		*m_regex;
  pcre_extra	*m_extra;
  unsigned	m_captureCount;
};

// quote the pattern using the pre-processor to avoid having to double
// backslash the RE, then strip the leading/trailing double-quotes

#define ZtREGEX(pattern_, ...) ZmStatic<ZmCleanup::Platform>([]{ \
  static char pattern[] = #pattern_; \
  ZuAssert(sizeof(pattern) >= 2); \
  pattern[sizeof(pattern) - 2] = 0; \
  return new ZtRegex(&pattern[1] __VA_OPT__(,) __VA_ARGS__); \
})

#endif /* ZtRegex_HH */
