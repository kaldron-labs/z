//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zjrpcexample_HH
#define zjrpcexample_HH

#include <zlib/Zjrpc.hh>
#include <zlib/ZtScratch.hh>

using TextScratch = ZtBArray<ZtArrayHeapID<"ZjrpcExample.Text">>;

struct AddRequest {
  int64_t lhs = 0;
  int64_t rhs = 0;
};
ZfStruct(, (AddRequest, JSON),
  (lhs, (Ctor<0>, Required), Int64),
  (rhs, (Ctor<1>, Required), Int64));

struct AddResult { int64_t value = 0; };
ZfStruct(, (AddResult, JSON), (value, (Ctor<0>, Required), Int64));

struct AddOK : public Zjrpc::Response { using Body = AddResult; };
struct Add : public Zjrpc::Request {
  using Object = AddRequest;
  using Method = ZuStringT<"add">;
  using Responses = ZuTypeList<AddOK>;
};
struct AddStream : public Add {
  using Method = ZuStringT<"add_stream">;
  enum { ResponseBody = Zjrpc::BodyPolicy::SSE };
};
using ExampleCatalog = ZuTypeList<Add, AddStream>;

#endif /* zjrpcexample_HH */
