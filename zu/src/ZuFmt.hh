//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// print/scan formatting

#ifndef ZuFmt_HH
#define ZuFmt_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

// compile-time formatting
namespace ZuFmt {
  enum { MaxWidth = 54 };
  enum { MaxNDP = 19 };

  struct Just { enum { None = 0, Left, Right, Frac }; };

  // NTP (named template parameters)

  // default formatting - directly used as ZuFmt::Default
  struct Default {
    enum { Justification_ = Just::None };
    enum { Hex_ = 0 };			// Hexadecimal
    enum { Upper_ = 0 };		// Upper-case (Hex only)
    enum { Alt_ = 0 };			// Prefix with 0x (Hex only)
    enum { Comma_ = 0 };		// Comma character (Decimal only)
    enum { Width_ = 0 };		// Maximum length (Left/Right/Frac)
    enum { Pad_ = 0 };			// Padding character (Left/Right)
    enum { NDP_ = -MaxNDP };		// Decimal places (FP/Frac)
    enum { Trim_ = 0 };			// Trim character (FP/Frac)
    enum { Negative_ = 0 };		// Negative
  };
  // NTP - left-justify within a fixed-width field
  template <unsigned Width, char Pad = '\0', typename NTP = Default>
  struct Left : public NTP {
    enum { Justification_ = Just::Left };
    enum { Width_ = Width > MaxWidth ? MaxWidth : Width };
    enum { Pad_ = Pad };
  };
  // NTP - right-justify within a fixed-width field
  template <unsigned Width, char Pad = '0', typename NTP = Default>
  struct Right : public NTP {
    enum { Justification_ = Just::Right };
    enum { Width_ = (int)Width > MaxWidth ? MaxWidth : (int)Width };
    enum { Pad_ = Pad };
  };
  // NTP - justify a fixed-point value within a fixed-width field
  template <
    unsigned Width, unsigned NDP, char Trim = '\0', typename NTP = Default>
  struct Frac : public NTP {
    enum { Justification_ = Just::Frac };
    enum { Width_ = Width };
    enum { NDP_ = int(NDP) > MaxNDP ? MaxNDP : int(NDP) };
    enum { Trim_ = Trim };
  };
  // NTP - specify hexadecimal
  template <bool Upper = 0, typename NTP = Default>
  struct Hex : public NTP {
    enum { Hex_ = 1 };
    enum { Upper_ = Upper };
  };
  template <bool Enable, bool Upper = 0, typename NTP = Default>
  struct HexEnable : public NTP {
    enum { Hex_ = Enable };
    enum { Upper_ = Upper };
  };
  // NTP - specify thousands comma character (decimal only, default is none)
  template <char Char = ',', typename NTP = Default>
  struct Comma : public NTP {
    enum { Comma_ = Char };
  };
  // NTP - specify 'alternative' format
  template <typename NTP = Default>
  struct Alt : public NTP {
    enum { Alt_ = 1 };
  };
  template <bool Enable = 1, typename NTP = Default>
  struct AltEnable : public NTP {
    enum { Alt_ = Enable };
  };
  // NTP - floating point format (optionally specifying #DP and padding)
  template <int NDP = -MaxNDP, char Trim = '\0', typename NTP = Default>
  struct FP : public NTP {
    enum { NDP_ = NDP < -MaxNDP ? -MaxNDP : NDP > MaxNDP ? MaxNDP : NDP };
    enum { Trim_ = Trim };
  };
};

// run-time variable formatting
#pragma pack(push, 1)
struct ZuVFmt {
  ZuVFmt() noexcept :
    m_justification{ZuFmt::Just::None},
    m_hex{0}, m_upper{0}, m_alt{0},
    m_comma{'\0'},
    m_width{0}, m_pad{-1},
    m_ndp{-ZuFmt::MaxNDP}, m_trim{'\0'} { }

  template <typename Fmt>
  ZuVFmt(Fmt) noexcept :
    m_justification{Fmt::Justification_},
    m_hex{Fmt::Hex_}, m_upper{Fmt::Upper_}, m_alt{Fmt::Alt_},
    m_comma{Fmt::Comma_},
    m_width{Fmt::Width_}, m_pad{Fmt::Pad_},
    m_ndp{Fmt::NDP_}, m_trim{Fmt::Trim_} { }

  ZuVFmt(const ZuVFmt &) = default;
  ZuVFmt &operator =(const ZuVFmt &) = default;
  ZuVFmt(ZuVFmt &&) = default;
  ZuVFmt &operator =(ZuVFmt &&) = default;

  // initializers
  ZuVFmt &reset() {
    using namespace ZuFmt;
    m_justification = Just::None;
    m_hex = 0; m_upper = 0; m_alt = 0;
    m_comma = '\0';
    m_width = 0; m_pad = -1;
    m_ndp = -ZuFmt::MaxNDP; m_trim = '\0';
    return *this;
  }
  ZuVFmt &left(unsigned width, char pad = '\0') {
    using namespace ZuFmt;
    m_justification = Just::Left;
    m_width = ZuUnlikely(width > MaxWidth) ? MaxWidth : width;
    m_pad = pad;
    return *this;
  }
  ZuVFmt &right(unsigned width, char pad = '0') {
    using namespace ZuFmt;
    m_justification = Just::Right;
    m_width = ZuUnlikely(width > MaxWidth) ? MaxWidth : width;
    m_pad = pad;
    return *this;
  }
  ZuVFmt &frac(unsigned width, unsigned ndp, char trim = '\0') {
    using namespace ZuFmt;
    m_justification = Just::Frac;
    m_width = width;
    m_ndp = ZuUnlikely(ndp > MaxNDP) ? MaxNDP : ndp;
    m_trim = trim;
    return *this;
  }
  ZuVFmt &hex() {
    m_hex = 1;
    m_upper = 0;
    return *this;
  }
  ZuVFmt &hex(bool upper) {
    m_hex = 1;
    m_upper = upper;
    return *this;
  }
  ZuVFmt &hex(bool hex_, bool upper) {
    m_hex = hex_;
    m_upper = upper;
    return *this;
  }
  ZuVFmt &comma(char comma_ = ',') {
    m_comma = comma_;
    return *this;
  }
  ZuVFmt &alt() {
    m_alt = 1;
    return *this;
  }
  ZuVFmt &alt(bool alt_) {
    m_alt = alt_;
    return *this;
  }
  ZuVFmt &fp(int ndp = -ZuFmt::MaxNDP, char trim = '\0') {
    using namespace ZuFmt;
    m_ndp =
      ZuUnlikely(ndp < -MaxNDP) ? -MaxNDP :
      ZuUnlikely(ndp > MaxNDP) ? MaxNDP : ndp;
    m_trim = trim;
    return *this;
  }

  // accessors
  int justification() const { return m_justification; }
  bool hex() const { return m_hex; }
  bool upper() const { return m_upper; }
  bool alt() const { return m_alt; }
  char comma() const { return m_comma; }
  unsigned width() const { return m_width; }
  int pad() const { return m_pad; }
  int ndp() const { return m_ndp; }
  char trim() const { return m_trim; }

private:
  int8_t	m_justification;
  uint8_t	m_hex:1,
  		m_upper:1,
  		m_alt:1;
  char		m_comma;
  uint8_t	m_width;
  int8_t	m_pad;
  int8_t	m_ndp;
  char		m_trim;
};
#pragma pack(pop)

template <typename Impl> struct ZuVFmtWrapper {
  ZuVFmt	fmt;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Impl &reset() { fmt.reset(); return *impl(); }
  Impl &left(unsigned width, char pad = '\0') {
    fmt.left(width, pad);
    return *impl();
  }
  Impl &right(unsigned width, char pad = '0') {
    fmt.right(width, pad);
    return *impl();
  }
  Impl &frac(unsigned width, unsigned ndp, char trim = '\0') {
    fmt.frac(width, ndp, trim);
    return *impl();
  }
  Impl &hex() { fmt.hex(); return *impl(); }
  Impl &hex(bool upper) { fmt.hex(upper); return *impl(); }
  Impl &hex(bool hex_, bool upper) {
    fmt.hex(hex_, upper);
    return *impl();
  }
  Impl &comma(char comma_ = ',') { fmt.comma(comma_); return *impl(); }
  Impl &alt() { fmt.alt(); return *impl(); }
  Impl &alt(bool alt_) { fmt.alt(alt_); return *impl(); }
  Impl &fp(int ndp = -ZuFmt::MaxNDP, char trim = '\0') {
    fmt.fp(ndp, trim);
    return *impl();
  }
};

#endif /* ZuFmt_HH */
