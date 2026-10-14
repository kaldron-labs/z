//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_client_edit_HH
#define zumd_saga_client_edit_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_saga_admin_result.hh>

namespace Zum {

struct ClientEditArgs {
  enum { Secret = 16 };
  Client before;
  String ifMatch;
  Client values;
  uint8_t fields = 0;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  uint32_t overlapSeconds = 0;
  ZmRef<AdminEditResult<Client>> result;
  bool unchanged() const { return stateOnly && state == before.state; }
};

Zdb_::SagaDef clientEditDef();
SagaRef clientEdit(ZuBSpan);
SagaRef clientEdit(ClientEditArgs);

} // namespace Zum

#endif /* zumd_saga_client_edit_HH */
