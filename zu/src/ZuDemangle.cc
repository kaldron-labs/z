//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDemangle.hh>

static void *context = nullptr;
static ZuDemangle_::TransformFn transformFn__ = nullptr;
static ZuDemangle_::FinalizeFn finalFn = nullptr;

void ZuDemangle_::transformFn(
    void *context_, TransformFn transformFn_, FinalizeFn finalFn_)
{
  if (finalFn) (*finalFn)(context, transformFn__);
  context = context_;
  transformFn__ = transformFn_;
  finalFn = finalFn_;
}

void ZuDemangle_::transform(ZuSpan<char> &output)
{
  if (transformFn__) (*transformFn__)(context, output);
}
