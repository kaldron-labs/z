//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_app_enrollment_HH
#define zumd_saga_app_enrollment_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

struct AppEnrollmentArgs {
  AppID		appID = 0;
  String	appName;
  String	appLabel;
  String	audience;
  SignKey	signKey;
  String	clientID;
  Bytes		secretDigest;
  int64_t	created = 0;
  ActionID	catalogPublishOp = 0;
  ActionID	operationQueryOp = 0;
  IdemRequest	request;
};

Zdb_::SagaDef appEnrollmentDef();
SagaRef appEnrollment(ZuBSpan);
SagaRef appEnrollment(AppEnrollmentArgs);

} // namespace Zum

#endif /* zumd_saga_app_enrollment_HH */
