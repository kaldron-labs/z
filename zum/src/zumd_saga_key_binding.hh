//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_key_binding_HH
#define zumd_saga_key_binding_HH

#include <zlib/zumd_saga.hh>
#include <zlib/ZumTypes.hh>

namespace Zum {

struct KeyBindingArgs {
  String issuer;
  Bytes beforeCheck;
  Bytes afterCheck;
  Bytes beforePending;
  Bytes afterPending;
};

Zdb_::SagaDef keyBindingDef();
SagaRef keyBinding(ZuBSpan);
SagaRef keyBinding(KeyBindingArgs);

} // namespace Zum

#endif /* zumd_saga_key_binding_HH */
