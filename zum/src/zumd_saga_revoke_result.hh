//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_revoke_result_HH
#define zumd_saga_revoke_result_HH

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

namespace Zum {

struct RevokeResult : public ZmNHeap<RevokeResult>, public ZmObject {
friend inline ZuStringT<"Zum.RevokeResult"> ZmNHeapID(RevokeResult *);

  unsigned error = 0;
};

} // namespace Zum

#endif /* zumd_saga_revoke_result_HH */
