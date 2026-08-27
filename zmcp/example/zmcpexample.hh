//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zmcpexample_HH
#define zmcpexample_HH

#include <zlib/Zmcp.hh>

struct AddRequest {
  int64_t lhs = 0;
  int64_t rhs = 0;
};
ZfStruct((AddRequest, JSON),
  (((lhs), (Ctor<0>, Required)), (Int64)),
  (((rhs), (Ctor<1>, Required)), (Int64)));

struct AddResult { int64_t value = 0; };
ZfStruct((AddResult, JSON),
  (((value), (Ctor<0>, Required)), (Int64)));

struct AddOK : public Zmcp::Response {
  using Body = AddResult;
  enum { Status = 200 };
};
struct AddUnauthorized : public Zmcp::Response {
  enum { Status = 401 };
};

struct Add : public Zmcp::Request {
  using Object = AddRequest;
  using OperationID = ZuStringT<"addNumbers">;
  using ToolID = ZuStringT<"add">;
  using Title = ZuStringT<"Add numbers">;
  using Description = ZuStringT<"Add two signed integers">;
  using Responses = ZuTypeList<AddOK, AddUnauthorized>;
};

struct AddStream : public Zmcp::Request {
  using Object = AddRequest;
  using OperationID = ZuStringT<"addNumbersStream">;
  using ToolID = ZuStringT<"add_stream">;
  using Title = ZuStringT<"Add numbers over SSE">;
  using Description = ZuStringT<"Add two signed integers using SSE">;
  using Responses = ZuTypeList<AddOK, AddUnauthorized>;
  enum { ResponseBody = Zmcp::BodyPolicy::SSE };
};

using ExampleCatalog = ZuTypeList<Add, AddStream>;

#endif /* zmcpexample_HH */
