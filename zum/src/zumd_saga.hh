//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Erased server saga reference shared by operation factories and callers.

#ifndef zumd_saga_HH
#define zumd_saga_HH

#include <zlib/ZdbSaga.hh>

namespace Zum {

struct DB;
using SagaRef = ZmRef<ZdbAnySaga<DB>>;

} // namespace Zum

#endif /* zumd_saga_HH */
