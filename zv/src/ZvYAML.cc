//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZvYAML.hh>

namespace ZvYAML {

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
      throw ZvYAML_EXCEPT(
	ZvYAMLError::fileError<"mmap">(path, file.error()));
    span = file.cspan();
  }
};

static Mapped map(const Zi::Path &path)
{
  ZiStat stat{path};
  auto length = stat.size();
  if (stat.error())
    throw ZvYAML_EXCEPT(
      ZvYAMLError::fileError<"stat">(path, stat.error()));
  if (length >= MaxFileSize)
    throw ZvYAML_EXCEPT(ZvYAMLError::file2Big(path));
  if (!length) return Mapped{};
  return Mapped{path, length};
}

ZuTuple<int, ZuPtr<AnyNode>> load(
    const Zi::Path &path, ZfYAML::Limits limits)
{
  auto resolved = ZiFile::canonical(path);
  if (!resolved)
    throw ZvYAML_EXCEPT(
      ZvYAMLError::fileError<"open">(path, ZeLastError));
  auto mapped = map(resolved);
  ZfYAML::Scan scan{mapped.span, limits};
  try {
    return scan.scan();
  } catch (const ZeException &) {
    const auto &error = scan.error();
    if (!error.failed) throw;
    throw ZeMkException(
      Ze::Error, __FILE__, __LINE__, ZuFnName, ZfYAMLError::Component,
      ZfYAMLError::badSyntax(
	error.line, error.column, error.offset, error.ch, error.code, resolved));
  }
}

} // ZvYAML
