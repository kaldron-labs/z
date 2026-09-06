//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZfJSON.hh>

using namespace ZuTestUtil;

static constexpr ZfJSON::ScanLimits limits{256, 4, 16, 32};

static void strict()
{
  ZuTestScope(strict);

  char valid[] = "{\"id\":\"\\ud83d\\udc04\",\"n\":-1.5e+2}";
  auto result = ZfJSON::scanStrict(valid, limits);
  ZuCheck(bool(result));
  ZuCheck(result.offset == int(sizeof(valid) - 1));
  auto &object = (*result.root)[0]->data<ZfJSON::AnyNode::Object>();
  ZuCheck(object.length() == 2);
  ZuCheck(object[0].p<0>() == "id");
  ZuCSpan cow{"\xf0\x9f\x90\x84", 4};
  ZuCheck(object[0].p<1>()->data<ZfJSON::AnyNode::String>() == cow);

  char permissive[] = "{id:1,}";
  ZuCheck(ZfJSON::scan(permissive).p<0>() > 0);
  char strict_[] = "{id:1,}";
  ZuCheck(ZfJSON::scanStrict(strict_, limits).error ==
    ZfJSON::ScanError::Syntax);

  char duplicate[] = "{\"id\":1,\"\\u0069d\":2}";
  ZuCheck(ZfJSON::scanStrict(duplicate, limits).error ==
    ZfJSON::ScanError::Duplicate);
  char number[] = "[01]";
  ZuCheck(ZfJSON::scanStrict(number, limits).error ==
    ZfJSON::ScanError::Syntax);
  char escape[] = "[\"\\x\"]";
  ZuCheck(ZfJSON::scanStrict(escape, limits).error ==
    ZfJSON::ScanError::Syntax);
}

static void bounded()
{
  ZuTestScope(bounded);

  char size[] = "{}";
  ZuCheck(ZfJSON::scanStrict(size, {1, 1, 1, 1}).error ==
    ZfJSON::ScanError::Size);
  char depth[] = "[[]]";
  ZuCheck(ZfJSON::scanStrict(depth, {4, 1, 2, 1}).error ==
    ZfJSON::ScanError::Depth);
  char nodes[] = "[1]";
  ZuCheck(ZfJSON::scanStrict(nodes, {3, 1, 1, 1}).error ==
    ZfJSON::ScanError::Nodes);
  char string[] = "\"ab\"";
  ZuCheck(ZfJSON::scanStrict(string, {4, 0, 1, 1}).error ==
    ZfJSON::ScanError::String);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(strict);
  ZuTestCall(bounded);
  return 0;
}
