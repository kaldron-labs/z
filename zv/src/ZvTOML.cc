//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZvTOML.hh>

namespace ZvTOML {

enum { MaxFileSize = 1<<20 }; // mainstream configuration-file ceiling; Cf parity

struct Mapped {
  ZiMMapFile	file;
  ZuCSpan	span;

  Mapped(const Mapped &) = delete;
  Mapped &operator =(const Mapped &) = delete;
  Mapped(Mapped &&) = delete;
  Mapped &operator =(Mapped &&) = delete;

  Mapped() {
    static const char empty = 0;
    span = {&empty, 0};
  }
  Mapped(const Zi::Path &path, Zi::Offset length) {
    if (file.mmap(
	path, ZiFile::ReadOnly | ZiFile::GC, length, false) != Zi::OK)
      throw ZvTOML_EXCEPT(
	ZvTOMLError::fileError<"mmap">(path, file.error()));
    span = file.cspan();
  }
};

static Mapped map(const Zi::Path &path)
{
  ZiStat stat{path};
  auto length = stat.size();
  if (stat.error())
    throw ZvTOML_EXCEPT(
      ZvTOMLError::fileError<"stat">(path, stat.error()));
  if (length >= MaxFileSize)
    throw ZvTOML_EXCEPT(ZvTOMLError::file2Big(path));
  if (!length) return Mapped{};
  return Mapped{path, length};
}

ZuTuple<int, ZuPtr<const AnyNode>> load(
    const Zi::Path &path, ZfTOML::Limits limits)
{
  auto resolved = ZiFile::canonical(path);
  if (!resolved)
    throw ZvTOML_EXCEPT(
      ZvTOMLError::fileError<"open">(path, ZeLastError));
  auto mapped = map(resolved);
  ZfTOML::Scan scan{mapped.span, limits};
  auto out = scan.scan();
  if (out.p<0>() >= 0) return out;
  const auto &error = scan.error();
  throw ZeMkException(
    Ze::Error, __FILE__, __LINE__, ZuFnName, ZfTOMLError::Component,
    ZfTOMLError::badSyntax(
      error.line, error.column, error.offset, error.ch, error.code, resolved));
}

} // ZvTOML
