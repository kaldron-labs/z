//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ZuTime - C API

#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/Zu_ntoa.hh>

#include <zlib/zu_time.h>

ZuAssert(sizeof(zu_time) == sizeof(ZuTime));

zu_time *zu_time_init(zu_time *v_)
{
  new (v_) ZuTime{};
  return v_;
}

bool zu_time_null(const zu_time *v_)
{
  auto v = reinterpret_cast<const ZuTime *>(v_);
  return !*v;
}

unsigned zu_time_in_csv(zu_time *v, const char *s)
{
  return reinterpret_cast<ZuTime *>(v)->scan(s);
}
unsigned zu_time_in_iso(zu_time *v_, const char *s)
{
  auto &v = *reinterpret_cast<ZuTime *>(v_);
  ZuDateTimeScan::ISO fmt;
  ZuDateTime t;
  auto n = t.scan(fmt, s);
  v = t.as_time();
  return n;
}
unsigned zu_time_in_fix(zu_time *v_, const char *s)
{
  auto &v = *reinterpret_cast<ZuTime *>(v_);
  ZuDateTimeScan::FIX fmt;
  ZuDateTime t;
  auto n = t.scan(fmt, s);
  v = t.as_time();
  return n;
}

unsigned zu_time_out_csv_len(const zu_time *v_)
{
  return 32;
}
char *zu_time_out_csv(char *s_, unsigned n, const zu_time *v_)
{
  thread_local ZuDateTimeFmt::CSV fmt;
  const auto &v = *reinterpret_cast<const ZuTime *>(v_);
  if (ZuUnlikely(!n)) return nullptr; // should never happen
  ZuDateTime d{v};
  ZuStream s{s_, n - 1};
  s << d.fmt(fmt);
  *s.data() = 0;
  return s.data();
}

unsigned zu_time_out_iso_len(const zu_time *v_)
{
  return 40;
}
char *zu_time_out_iso(char *s_, unsigned n, const zu_time *v_)
{
  thread_local ZuDateTimeFmt::ISO fmt;
  const auto &v = *reinterpret_cast<const ZuTime *>(v_);
  if (ZuUnlikely(!n)) return nullptr; // should never happen
  ZuDateTime d{v};
  ZuStream s{s_, n - 1};
  s << d.fmt(fmt);
  *s.data() = 0;
  return s.data();
}

unsigned zu_time_out_fix_len(const zu_time *v_)
{
  return 32;
}
char *zu_time_out_fix(char *s_, unsigned n, const zu_time *v_)
{
  thread_local ZuDateTimeFmt::FIX<-3> fmt;
  const auto &v = *reinterpret_cast<const ZuTime *>(v_);
  if (ZuUnlikely(!n)) return nullptr; // should never happen
  ZuDateTime d{v};
  ZuStream s{s_, n - 1};
  s << d.fmt(fmt);
  *s.data() = 0;
  return s.data();
}

zu_decimal *zu_time_to_decimal(zu_decimal *d_, const zu_time *v_)
{
  const auto &v = *reinterpret_cast<const ZuTime *>(v_);
  ZuDecimal d = v.as_decimal();
  d_->value = d.value;
  return d_;
}

zu_time *zu_time_from_decimal(zu_time *v_, const zu_decimal *d_)
{
  ZuDecimal d{ZuDecimal::Unscaled{d_->value}};
  new (v_) ZuTime{d};
  return v_;
}

int zu_time_cmp(const zu_time *l_, const zu_time *r_)
{
  const auto &l = *reinterpret_cast<const ZuTime *>(l_);
  const auto &r = *reinterpret_cast<const ZuTime *>(r_);
  return l.cmp(r);
}

uint32_t zu_time_hash(const zu_time *v_)
{
  const auto &v = *reinterpret_cast<const ZuTime *>(v_);
  return v.hash();
}

zu_time *zu_time_add(
    zu_time *v_, const zu_time *l_, const zu_decimal *r_)
{
  auto &v = *reinterpret_cast<ZuTime *>(v_);
  const auto &l = *reinterpret_cast<const ZuTime *>(l_);
  ZuDecimal r{ZuDecimal::Unscaled{r_->value}};
  v = l + r;
  return v_;
}
zu_time *zu_time_sub(
    zu_time *v_, const zu_time *l_, const zu_decimal *r_)
{
  auto &v = *reinterpret_cast<ZuTime *>(v_);
  const auto &l = *reinterpret_cast<const ZuTime *>(l_);
  ZuDecimal r{ZuDecimal::Unscaled{r_->value}};
  v = l - r;
  return v_;
}
zu_decimal *zu_time_delta(
    zu_decimal *v_, const zu_time *l_, const zu_time *r_)
{
  const auto &l = *reinterpret_cast<const ZuTime *>(l_);
  const auto &r = *reinterpret_cast<const ZuTime *>(r_);
  ZuDecimal v = (l - r).as_decimal();
  v_->value = v.value;
  return v_;
}
