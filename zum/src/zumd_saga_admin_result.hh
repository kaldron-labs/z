//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_admin_result_HH
#define zumd_saga_admin_result_HH

#include <zlib/ZmHeap.hh>

namespace Zum {

template <typename Record>
struct AdminEditResult : public ZmNHeap<AdminEditResult<Record>>, public ZmObject {
friend inline ZuStringT<"Zum.AdminEditResult"> ZmNHeapID(AdminEditResult *);

  unsigned error = 0;
  Record item;
};

} // namespace Zum

#endif /* zumd_saga_admin_result_HH */
