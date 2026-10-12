//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZdbTypes.hh>
#include <zlib/ZdbMsg.hh>

namespace Zdb_ {

ZtEnumImplNS(RowState);
ZtEnumImplNS(OpResult);

namespace Msg {
ZfStructImpl(TableState);
ZfStructImpl(TableState, FB);
ZfStructImpl(DBState);
ZfStructImpl(DBState, FB);
ZfStructImpl(Heartbeat);
ZfStructImpl(Heartbeat, FB);
ZfStructImpl(Record);
ZfStructImpl(Record, FB);
ZfStructImpl(Commit);
ZfStructImpl(Commit, FB);
ZfStructImpl(Root);
ZfStructImpl(Root, FB);
} // Msg

} // namespace Zdb_
