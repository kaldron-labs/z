//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// configuration file I/O

#ifndef ZvCf_HH
#define ZvCf_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZfCf.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiFileTxStream.hh>

namespace ZvCfError {

constexpr auto Component = "ZvCf"_Zu;

template <ZuString Op>
inline auto fileError(
    const Zi::Path &path, ZeError error, ZuCSpan chain = {}) {
  return [
    path = Zi::Path{path}, error, chain = ZeString{chain}
  ](auto &s) {
    s << Op.cspan() << "(\"" << path << "\"): " << error;
    if (chain) s << " [include chain: " << chain << ']';
  };
}

inline auto file2Big(const Zi::Path &path, ZuCSpan chain = {}) {
  return [
    path = Zi::Path{path}, chain = ZeString{chain}
  ](auto &s) {
    s << '"' << path << "\": file too big";
    if (chain) s << " [include chain: " << chain << ']';
  };
}

inline auto includeArgs(
    const Zi::Path &path, ZuCSpan chain = {}) {
  return [
    path = Zi::Path{path}, chain = ZeString{chain}
  ](auto &s) {
    s << '"' << path << "\": %include requires one non-empty argument";
    if (chain) s << " [include chain: " << chain << ']';
  };
}

inline auto includeCycle(ZuCSpan chain) {
  return [chain = ZeString{chain}](auto &s) {
    s << "recursive %include [include chain: " << chain << ']';
  };
}

} // ZvCfError

#define ZvCf_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZvCfError::Component, __VA_ARGS__)

namespace ZvCf {

using namespace ZfCf;
using ZfCf::AnyNode;

ZvExtern ZuTuple<int, ZuPtr<const AnyNode>> load(
  const Zi::Path &, PctFn = {}, ZmRef<Defines> = new Defines());

template <
  typename Facet = ZuFacet::Cf,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename O>
inline void save(const Zi::Path &path, const O &v) {
  ZiFile file;
  if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK)
    throw ZvCf_EXCEPT(
      ZvCfError::fileError<"open">(path, file.error()));
  ZiFileTxStream<> stream{file};
  ZfCf::save<Facet, Filter>(stream, v);
  if (!stream.flush()) {
    if (file.error())
      throw ZvCf_EXCEPT(
	ZvCfError::fileError<"write">(path, file.error()));
    throw ZvCf_EXCEPT(
      ([path = Zi::Path{path}](auto &s) { s << '"' << path << "\": output failed"; }));
  }
}
template <
  typename Facet = ZuFacet::Cf,
  typename O>
ZuInline void saveUpd(const Zi::Path &path, const O &v) {
  save<Facet, ZfFieldFilter::Upd>(path, v);
}
template <
  typename Facet = ZuFacet::Cf,
  typename O>
ZuInline void saveDel(const Zi::Path &path, const O &v) {
  save<Facet, ZfFieldFilter::Del>(path, v);
}

} // ZvCf

#endif /* ZvCf_HH */
