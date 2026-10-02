//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time formatted field printing/scanning
// - see ZfJSON for JSON-specific formatting

#ifndef ZtFmt_HH
#define ZtFmt_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuFmt.hh>
#include <zlib/ZuDateTime.hh>

#include <zlib/ZmSpecific.hh>

namespace ZtFmt {

using ZuFmt::MaxWidth;
using ZuFmt::MaxNDP;
using ZuFmt::Just;

struct Default : public ZuFmt::Default {
  static ZuDateTimeScan::Any &DateScan_() {
    return ZmTLS([]{ return ZuDateTimeScan::Any{}; });
  }
  static ZuDateTimeFmt::Any &DatePrint_() {
    return ZmTLS([]{ return ZuDateTimeFmt::Any{}; });
  }
  static constexpr ZuCSpan FlagsDelim() { return "|"; }

  // vector formatting
  static constexpr ZuCSpan VecPrefix() { return "["; }
  static constexpr ZuCSpan VecDelim() { return ","; }
  static constexpr ZuCSpan VecSuffix() { return "]"; }
};

// Scalar directives retain the extended defaults unless a base is supplied.
template <unsigned Width, char Pad = '\0', typename NTP = Default>
using Left = ZuFmt::Left<Width, Pad, NTP>;
template <unsigned Width, char Pad = '0', typename NTP = Default>
using Right = ZuFmt::Right<Width, Pad, NTP>;
template <unsigned Width, unsigned NDP, char Trim = '\0', typename NTP = Default>
using Frac = ZuFmt::Frac<Width, NDP, Trim, NTP>;
template <bool Upper = false, typename NTP = Default>
using Hex = ZuFmt::Hex<Upper, NTP>;
template <bool Enable, bool Upper = false, typename NTP = Default>
using HexEnable = ZuFmt::HexEnable<Enable, Upper, NTP>;
template <char Char = ',', typename NTP = Default>
using Comma = ZuFmt::Comma<Char, NTP>;
template <typename NTP = Default>
using Alt = ZuFmt::Alt<NTP>;
template <bool Enable = true, typename NTP = Default>
using AltEnable = ZuFmt::AltEnable<Enable, NTP>;
template <int NDP = -MaxNDP, char Trim = '\0', typename NTP = Default>
using FP = ZuFmt::FP<NDP, Trim, NTP>;

// NTP - date/time scan format
template <auto Scan, typename NTP = Default>
struct DateScan : public NTP {
  static constexpr auto DateScan_ = Scan;
};

// NTP - date/time print format
template <auto Print, typename NTP = Default>
struct DatePrint : public NTP {
  static constexpr auto DatePrint_ = Print;
};

// NTP - flags formatting
template <auto Delim, typename NTP = Default>
struct Flags : public NTP {
  static constexpr auto FlagsDelim = Delim;
};

// NTP - vector formatting (none of these should have leading white space)
template <auto Prefix, auto Delim, auto Suffix, typename NTP = Default>
struct Vec : public NTP {
  static constexpr auto VecPrefix = Prefix;
  static constexpr auto VecDelim = Delim;
  static constexpr auto VecSuffix = Suffix;
};

} // ZtFmt

// run-time dynamic polymorphic
struct ZtVFmt {
  ZuVFmt		scalar;			// scalar format (print only)
  ZuDateTimeScan::Any	dateScan;		// date/time scan format
  ZuDateTimeFmt::Any	datePrint;		// date/time print format
  ZuCSpan		flagsDelim = "|";	// flags delimiter

  // none of these should have leading white space
  ZuCSpan		vecPrefix = "[";	// vector prefix
  ZuCSpan		vecDelim = ", ";	// vector delimiter
  ZuCSpan		vecSuffix = "]";	// vector suffix

  ZtVFmt() = default;

  template <typename Fmt>
  ZtVFmt(Fmt fmt) :
    scalar{fmt},
    dateScan{Fmt::DateScan_()},
    datePrint{Fmt::DatePrint_()},
    flagsDelim{Fmt::FlagsDelim()},
    vecPrefix{Fmt::VecPrefix()},
    vecDelim{Fmt::VecDelim()},
    vecSuffix{Fmt::VecSuffix()} { }

  ZtVFmt(const ZtVFmt &) = default;
  ZtVFmt &operator =(const ZtVFmt &) = default;
  ZtVFmt(ZtVFmt &&) = default;
  ZtVFmt &operator =(ZtVFmt &&) = default;
};

#endif /* ZtFmt_HH */
