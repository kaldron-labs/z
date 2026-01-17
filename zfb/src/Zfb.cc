//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// fastbuffers file I/O

#include <zlib/Zfb.hh>

#include <zlib/ZiFile.hh>

ZfbExtern int Zfb::Save::save(
  const Zi::Path &path, Builder &fbb, unsigned mode, ZeError *e)
{
  ZiFile f;
  int i;

  if ((i = f.open(path, ZiFile::Write | ZiFile::GC, mode)) != Zi::OK) {
    if (e) *e = f.error();
    return i;
  }

  const uint8_t *data = fbb.GetBufferPointer();
  int len = fbb.GetSize();

  if (!data || len <= 0) {
    if (e) *e = ZiENOMEM;
    return Zi::IOError;
  }
  i = f.write(data, len);
  if (i != Zi::OK && e) *e = f.error();
  return i;
}

ZfbExtern int Zfb::Load::load(
  const Zi::Path &path,
  Zfb::Load::LoadFn fn, Zi::Offset maxSize, ZeError *e)
{
  ZiFile f;
  int i;

  if ((i = f.open(path, ZiFile::ReadOnly | ZiFile::GC, 0)) != Zi::OK) {
    if (e) *e = f.error();
    return i;
  }
  ZiFile::Offset len = f.size();
  if (!len || len >= maxSize) {
    if (e) *e = ZiENOMEM;
    return Zi::IOError;
  }
  auto data = ZmAlloc(uint8_t, len);
  if (!data) {
    if (e) *e = ZiENOMEM;
    return Zi::IOError;
  }
  if ((i = f.read(&data[0], len)) < len) {
    if (e) *e = f.error();
    return Zi::IOError;
  }
  f.close();
  if (!fn(ZuBSpan{&data[0], unsigned(len)})) {
    if (e) *e = ZiEINVAL;
    return Zi::IOError;
  }
  return Zi::OK;
}
