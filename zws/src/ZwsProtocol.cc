//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZwsProtocol.hh>
#include <zlib/ZwsExtended.hh>
#include <zlib/ZwsH1.hh>

ZhttpHdrCatalogImpl(Zws::H1::ClientHdrCatalog)
ZhttpHdrCatalogImpl(Zws::H1::ServerHdrCatalog)
ZhttpHdrCatalogImpl(Zws::H1::RequestHdrCatalog)
ZhttpHdrCatalogImpl(Zws::H1::ResponseHdrCatalog)
ZhttpHdrCatalogImpl(Zws::Extended::ClientHdrCatalog)
ZhttpHdrCatalogImpl(Zws::Extended::ServerHdrCatalog)

namespace Zws {

ZtEnumImplStruct(Opcode);
ZtEnumImplStruct(CloseCode);
ZtEnumImplStruct(Failure);

} // namespace Zws
