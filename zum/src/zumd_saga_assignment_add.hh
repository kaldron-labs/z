//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_assignment_add_HH
#define zumd_saga_assignment_add_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/ZmHeap.hh>

namespace Zum {

struct AssignmentAddResult : public ZmNHeap<AssignmentAddResult>, public ZmObject {
friend inline ZuStringT<"Zum.AssignmentAddResult"> ZmNHeapID(AssignmentAddResult *);

  unsigned error = 503;
};

struct AssignmentAddArgs {
  AppID appID = 0;
  UserID userID = 0;
  int64_t created = 0;
  IdemRequest request;
  ZmRef<AssignmentAddResult> result;
};

Zdb_::SagaDef assignmentAddDef();
SagaRef assignmentAdd(ZuBSpan);
SagaRef assignmentAdd(AssignmentAddArgs);

} // namespace Zum

#endif /* zumd_saga_assignment_add_HH */
