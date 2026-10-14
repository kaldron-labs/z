//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// recoverable application-owned catalog publication

#ifndef zumd_catalog_HH
#define zumd_catalog_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_saga_image.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

struct CatalogEdit {
  Bytes before;
  Bytes after;
};
ZfStruct(ZumAPI, (CatalogEdit, FB),
  (before,	(Ctor<0>),	Bytes),
  (after,	(Ctor<1>),	Bytes));
ZiFB::AsTable<"Zum.fbs.CatalogEdit"> ZiFB_Fmt(CatalogEdit *);

struct CatalogRows {
  BytesVec added;
  BytesVec changed;

  template <typename T> void add(const T &after) {
    added.push(SagaImage::save(after));
  }
  template <typename T> void change(const T &before, const T &after) {
    changed.push(SagaImage::save(CatalogEdit{
      SagaImage::save(before), SagaImage::save(after)}));
  }
};
ZfStruct(ZumAPI, (CatalogRows, FB),
  (added,	(Ctor<0>),	BytesVec),
  (changed,	(Ctor<1>),	BytesVec));
ZiFB::AsTable<"Zum.fbs.CatalogRows"> ZiFB_Fmt(CatalogRows *);


} // namespace Zum

#endif /* zumd_catalog_HH */
