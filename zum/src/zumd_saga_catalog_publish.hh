//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_catalog_publish_HH
#define zumd_saga_catalog_publish_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd_catalog.hh>

namespace Zum {

struct CatalogPublishArgs {
  App before;
  App after;
  CatalogRows actions;
  CatalogRows roles;
  CatalogRows clients;
  CatalogRows clientAccess;
  IdemRequest request;
};

Zdb_::SagaDef catalogPublishDef();
SagaRef catalogPublish(ZuBSpan);
SagaRef catalogPublish(CatalogPublishArgs);

} // namespace Zum

#endif /* zumd_saga_catalog_publish_HH */
