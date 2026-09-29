//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// YAML configuration file I/O

#ifndef ZvYAML_HH
#define ZvYAML_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuString.hh>

#include <zlib/ZfYAML.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiFileTxStream.hh>

namespace ZvYAMLError {

constexpr auto Component = "ZvYAML"_Zu;

template <ZuString Op>
inline auto fileError(const Zi::Path &path, ZeError error) {
  return [path = Zi::Path{path}, error](auto &s) {
    s << Op.cspan() << "(\"" << path << "\"): " << error;
  };
}

inline auto file2Big(const Zi::Path &path) {
  return [path = Zi::Path{path}](auto &s) {
    s << '"' << path << "\": file too big";
  };
}

} // ZvYAMLError

#define ZvYAML_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZvYAMLError::Component, __VA_ARGS__)

namespace ZvYAML {

using namespace ZfYAML;
using ZfYAML::AnyNode;

ZvExtern ZuTuple<int, ZuPtr<AnyNode>> load(
  const Zi::Path &, ZfYAML::Limits = {});

template <
  typename Facet = ZuFacet::YAML,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename O>
inline void save(const Zi::Path &path, const O &v) {
  ZiFile file;
  if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK)
    throw ZvYAML_EXCEPT(
      ZvYAMLError::fileError<"open">(path, file.error()));
  ZiFileTxStream<> stream{file};
  ZfYAML::save<Facet, Filter>(stream, v);
  if (!stream.flush()) {
    if (file.error())
      throw ZvYAML_EXCEPT(
	ZvYAMLError::fileError<"write">(path, file.error()));
    throw ZvYAML_EXCEPT(
      ([path = Zi::Path{path}](auto &s) { s << '"' << path << "\": output failed"; }));
  }
}

template <typename Facet = ZuFacet::YAML, typename O>
ZuInline void saveUpd(const Zi::Path &path, const O &v) {
  save<Facet, ZfFieldFilter::Upd>(path, v);
}

template <typename Facet = ZuFacet::YAML, typename O>
ZuInline void saveDel(const Zi::Path &path, const O &v) {
  save<Facet, ZfFieldFilter::Del>(path, v);
}

} // ZvYAML

#endif /* ZvYAML_HH */
