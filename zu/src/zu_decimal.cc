//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZuDecimal - C API

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/Zu_ntoa.hh>

#include <zlib/zu_decimal.h>

int zu_decimal_in(zu_decimal *v_, const char *s)
{
  ZuDecimal v;
  auto n = v.scan(s);
  v_->value = v.value;
  return n;
}
unsigned zu_decimal_out_len(const zu_decimal *)
{
  return 40;
}

char *zu_decimal_out(char *s_, unsigned n, const zu_decimal *v_)
{
  ZuDecimal v{ZuDecimal::Unscaled{v_->value}};
  if (ZuUnlikely(!n)) return nullptr; // should never happen
  ZuStream s{s_, n - 1};
  s << v;
  *s.data() = 0;
  return s.data();
}

int64_t zu_decimal_to_int(const zu_decimal *v_)
{
  ZuDecimal v{ZuDecimal::Unscaled{v_->value}};
  return v.floor();
}

zu_decimal *zu_decimal_from_int(zu_decimal *v_, int64_t i)
{
  ZuDecimal v{i};
  v_->value = v.value;
  return v_;
}

double zu_decimal_to_double(const zu_decimal *v_)
{
  ZuDecimal v{ZuDecimal::Unscaled{v_->value}};
  return v.as_fp();
}

zu_decimal *zu_decimal_from_double(zu_decimal *v_, double d)
{
  ZuDecimal v{d};
  v_->value = v.value;
  return v_;
}

int64_t zu_decimal_round(const zu_decimal *v_)
{
  ZuDecimal v{ZuDecimal::Unscaled{v_->value}};
  return v.round();
}

int zu_decimal_cmp(const zu_decimal *l_, const zu_decimal *r_)
{
  ZuDecimal l{ZuDecimal::Unscaled{l_->value}};
  ZuDecimal r{ZuDecimal::Unscaled{r_->value}};
  return l.cmp(r);
}

uint32_t zu_decimal_hash(const zu_decimal *v_)
{
  ZuDecimal v{ZuDecimal::Unscaled{v_->value}};
  return v.hash();
}

zu_decimal *zu_decimal_neg(zu_decimal *v_, const zu_decimal *p_)
{
  ZuDecimal p{ZuDecimal::Unscaled{p_->value}};
  ZuDecimal v = -p;
  v_->value = v.value;
  return v_;
}

zu_decimal *zu_decimal_add(
    zu_decimal *v_, const zu_decimal *l_, const zu_decimal *r_)
{
  ZuDecimal l{ZuDecimal::Unscaled{l_->value}};
  ZuDecimal r{ZuDecimal::Unscaled{r_->value}};
  ZuDecimal v = l + r;
  v_->value = v.value;
  return v_;
}
zu_decimal *zu_decimal_sub(
    zu_decimal *v_, const zu_decimal *l_, const zu_decimal *r_)
{
  ZuDecimal l{ZuDecimal::Unscaled{l_->value}};
  ZuDecimal r{ZuDecimal::Unscaled{r_->value}};
  ZuDecimal v = l - r;
  v_->value = v.value;
  return v_;
}
zu_decimal *zu_decimal_mul(
    zu_decimal *v_, const zu_decimal *l_, const zu_decimal *r_)
{
  ZuDecimal l{ZuDecimal::Unscaled{l_->value}};
  ZuDecimal r{ZuDecimal::Unscaled{r_->value}};
  ZuDecimal v = l * r;
  v_->value = v.value;
  return v_;
}
zu_decimal *zu_decimal_div(
    zu_decimal *v_, const zu_decimal *l_, const zu_decimal *r_)
{
  ZuDecimal l{ZuDecimal::Unscaled{l_->value}};
  ZuDecimal r{ZuDecimal::Unscaled{r_->value}};
  ZuDecimal v = l / r;
  v_->value = v.value;
  return v_;
}
