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
#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZmScratch.hh>

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

// n should be the captureCount() (includes $& but not $` and $')
#define ZtRegexOVector(o, n) \
  auto o##_size = unsigned(n) * 3; \
  auto o = ZmScratch( \
    unsigned, o##_size, ZtRegex::OVector::VHeap)

// n is the number of explicit capture groups (excludes $&, $` and $')
#define ZtRegexCaptures(c, n) \
  auto c##_size = unsigned(n) + 3; \
  auto c = ZmScratch( \
    ZtRegex::Capture, c##_size, ZtRegex::Captures::VHeap)

// split() output is not bounded by the regular expression capture count
#define ZtRegexSplitCaptures(c, n) \
  auto c##_size = unsigned(n) + 2; \
  auto c = ZtScratch(ZtRegex::Captures, c##_size)

class ZtAPI ZtRegex {
  ZtRegex(const ZtRegex &) = delete;
  ZtRegex &operator =(const ZtRegex &) = delete;

  ZtRegex();

public:
  using Capture = ZuCSpan;
  using CaptureSpan = ZuSpan<const Capture>;
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

  // the capture count includes $& but not $` and $'
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
  template <typename Captures_>
  unsigned m(ZuCSpan s,
      Captures_ &captures, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    unsigned i = exec(s, offset, options, ovector);
    if (i) capture(s, ovector, captures);
    return i;
  }
  template <typename R>
  unsigned mg(ZuCSpan s, R &&r, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    ZtRegexCaptures(captures, m_captureCount - 1);
    unsigned n = 0;
    unsigned slength = s.length();

    while (offset < slength && exec(s, offset, options, ovector)) {
      capture(s, ovector, captures);
      r(captures.cspan());
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
      ZuDeclVal<CaptureSpan>(), [](ZuCSpan) { }))> :
    public ZuTrue { };
  template <typename L, typename R = void>
  using MatchCallable = ZuIfT<IsCallable<L>{}, R>;
  template <typename L, typename R = void>
  using MatchNotCallable = ZuIfT<!IsCallable<L>{}, R>;

  template <typename S, typename R>
  MatchCallable<R, unsigned> s(S &s, R &&r, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    ZtRegexCaptures(captures, m_captureCount - 1);
    unsigned i = exec(s, offset, options, ovector);
    if (i) {
      capture(s, ovector, captures);
      ZuFwd<R>(r)(captures.cspan(), [&s, &ovector](ZuCSpan r) {
	s.splice(ovector[0], ovector[1] - ovector[0], r);
      });
    }
    return i;
  }
  template <typename S, typename R>
  MatchNotCallable<R, unsigned> s(S &s, R &&r, unsigned offset = 0, int options = 0) const {
    return this->s(s, [r = ZuCSpan(r)]<typename Splice>(
	CaptureSpan, Splice &&splice) {
      splice(r);
    });
  }

  template <typename S, typename R>
  MatchCallable<R, unsigned> sg(S &s, R &&r, unsigned offset = 0, int options = 0) const {
    ZtRegexOVector(ovector, m_captureCount);
    ZtRegexCaptures(captures, m_captureCount - 1);
    unsigned n = 0;
    unsigned slength = s.length(), rlength;

    while (offset < slength && exec(s, offset, options, ovector)) {
      capture(s, ovector, captures);
      r(captures.cspan(), [&s, &ovector, &rlength](ZuCSpan r) {
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
    return sg(s, [r = ZuCSpan(r)]<typename Splice>(
	CaptureSpan, Splice &&splice) {
      splice(r);
    });
  }

  template <typename Captures_>
  unsigned split(ZuCSpan s, Captures_ &a, int options = 0) const {
    unsigned offset = 0, last = 0;
    ZtRegexOVector(ovector, m_captureCount);
    unsigned slength = s.length();

    while (offset < slength && exec(s, offset, options, ovector)) {
      if (offset || ovector[1] > ovector[0])
	new (a.push()) Capture(s.data() + last, ovector[0] - last);
      last = offset = ovector[1];
      if (ovector[1] == ovector[0]) offset++;
      options |= PCRE_NO_UTF8_CHECK;
    }
    if (last < slength)
      new (a.push()) Capture(s.data() + last, slength - last);

    return a.length();
  }

  int index(const char *name) const; // pcre_get_stringnumber()

private:
  template <typename OVector_>
  unsigned exec(
      ZuCSpan s, unsigned offset, int options, OVector_ &ovector) const {
    unsigned slength = s.length();

    if (slength <= offset) return 0;

    ovector.length(m_captureCount * 3);

    int c = pcre_exec(
	m_regex, m_extra, s.data(), slength,
	offset, options,
	reinterpret_cast<int *>(ovector.data()), ovector.length());

    if (c >= 0) return c;
    if (c == PCRE_ERROR_NOMATCH) return 0;
    throw ZtRegexError{nullptr, c, -1};
  }
  template <typename OVector_, typename Captures_>
  void capture(
      ZuCSpan s, const OVector_ &ovector, Captures_ &captures) const {
    unsigned slength = s.length();
    unsigned n = m_captureCount;

    captures.length(0);
    new (captures.push()) Capture(s.data(), ovector[0]); // $`
    for (unsigned i = 0; i < n; i++) {
      int offset = int(ovector[i<<1]);
      if (offset < 0)
	new (captures.push()) Capture();
      else
	new (captures.push()) Capture(
	  s.data() + offset, ovector[(i<<1) + 1] - offset);
    }
    new (captures.push())
      Capture(s.data() + ovector[1], slength - ovector[1]); // $'
  }

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
