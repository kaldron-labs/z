//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdint.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZfTreeLoad.hh>

using namespace ZuTestUtil;

using AnyNode = ZfTree::AnyNode;

static_assert(ZuIs_<ZfCf::AnyNode, ZfTree::AnyNode>{});
static_assert(ZfTree::ValueTC::Array == 0);
static_assert(ZfTree::ValueTC::Object == 1);
static_assert(ZfTree::ValueTC::String == 2);
static_assert(ZfTree::ValueTC::DateTime == 3);
static_assert(sizeof(AnyNode) == 3 * sizeof(void *));
static_assert(AnyNode::StringSize > 0);
static_assert(AnyNode::ArraySize > 0);
static_assert(AnyNode::ObjectSize > 0);

struct BadType { };

struct Policy {
  static unsigned badTypes;

  static bool scalar(int type, ZfTreeLoad::ScalarMask::T mask) {
    switch (type) {
      case ZfTree::ScalarTC::String:
	return mask & ZfTreeLoad::ScalarMask::String();
      case ZfTree::ScalarTC::Number:
	return mask & ZfTreeLoad::ScalarMask::Number();
      case ZfTree::ScalarTC::False:
      case ZfTree::ScalarTC::True:
	return mask & ZfTreeLoad::ScalarMask::Bool();
      case ZfTree::ScalarTC::Null:
	return mask & ZfTreeLoad::ScalarMask::Null();
      default:
	return false;
    }
  }

  template <typename Props>
  using GetTimeFmt = ZuFieldProp::JSON::GetTimeFmt<Props>;

  [[noreturn]] static void badType(const AnyNode *, ZuCSpan) {
    ++badTypes;
    throw BadType{};
  }

  [[noreturn]] static void badValue(const AnyNode *, ZuCSpan, ZuCSpan) {
    throw BadType{};
  }
};

unsigned Policy::badTypes = 0;

static void representation()
{
  ZuTestScope(representation);

  auto array = ZfTree::newNode<AnyNode::Array>(nullptr);
  auto object = ZfTree::newNode<AnyNode::Object>(nullptr);
  auto string = ZfTree::newNode<AnyNode::String>(nullptr, "value");
  ZuDateTime value{2024, 2, 29, 12, 34, 56, 123456789};
  auto dateTime = ZfTree::newNode<AnyNode::DateTime>(nullptr, value);

  ZuCheck(array->has<AnyNode::Array>());
  ZuCheck(object->has<AnyNode::Object>());
  ZuCheck(string->has<AnyNode::String>());
  ZuCheck(dateTime->has<AnyNode::DateTime>());
  ZuCheck(array->type == ZfTree::ValueTC::Array);
  ZuCheck(object->type == ZfTree::ValueTC::Object);
  ZuCheck(string->type == ZfTree::ValueTC::String);
  ZuCheck(dateTime->type == ZfTree::ValueTC::DateTime);
  ZuCheck(array->scalarType == ZfTree::ScalarTC::None);
  ZuCheck(object->scalarType == ZfTree::ScalarTC::None);
  ZuCheck(string->scalarType == ZfTree::ScalarTC::String);
  ZuCheck(dateTime->scalarType == ZfTree::ScalarTC::None);
  ZuCheck(dateTime->data == value);
  ZuCheck((ZfTreeLoad::loadValue<
    Policy, ZuFacet::Cf, ZfFieldFilter::Load,
    ZfFieldTC::DateTime, ZuTypeList<>, ZuDateTime>(dateTime)) == value);
  ZuCheck((ZfTreeLoad::loadValue<
    Policy, ZuFacet::Cf, ZfFieldFilter::Load,
    ZfFieldTC::Time, ZuTypeList<>, ZuTime>(dateTime)) == value.as_time());
  ZuCheck(sizeof(AnyNode::String) == AnyNode::SNodeSize);
  ZuCheck(sizeof(AnyNode::Array) <= AnyNode::LNodeSize);
  ZuCheck(sizeof(AnyNode::Object) <= AnyNode::LNodeSize);
  ZuCheck(ZuCSpan{ZfTree::Node_HeapID{}()} == "ZfTree.Node");

  string->scalarType = ZfTree::ScalarTC::None;
  ZuCheck(string->scalarType == ZfTree::ScalarTC::None);
  string->scalarType = ZfTree::ScalarTC::String;
  ZuCheck(string->scalarType == ZfTree::ScalarTC::String);
  string->scalarType = ZfTree::ScalarTC::Number;
  ZuCheck(string->scalarType == ZfTree::ScalarTC::Number);
  string->scalarType = ZfTree::ScalarTC::False;
  ZuCheck(string->scalarType == ZfTree::ScalarTC::False);
  string->scalarType = ZfTree::ScalarTC::True;
  ZuCheck(string->scalarType == ZfTree::ScalarTC::True);
  string->scalarType = ZfTree::ScalarTC::Null;
  ZuCheck(string->scalarType == ZfTree::ScalarTC::Null);
}

static void ancestry()
{
  ZuTestScope(ancestry);

  auto root = ZfTree::newNode<AnyNode::Object>(nullptr);
  auto array = ZfTree::newNode<AnyNode::Array>(root);
  auto leaf = ZfTree::newNode<AnyNode::String>(array, "leaf");
  auto leafPtr = leaf.ptr();
  ZuDateTime value{2024, 2, 29, 12, 34, 56, 123456789};
  auto dateTime = ZfTree::newNode<AnyNode::DateTime>(array, value);
  auto dateTimePtr = dateTime.ptr();
  leafPtr->scalarType = ZfTree::ScalarTC::True;
  array->data.push(ZuMv(leaf));
  array->data.push(ZuMv(dateTime));
  root->data.push(AnyNode::Field{"items", ZuMv(array)});

  ZtString<> path;
  leafPtr->path(path);
  ZuCheck(path == "items[0]");
  ZuCheck(root->resolve("items[0]") == leafPtr);
  path.null();
  dateTimePtr->path(path);
  ZuCheck(path == "items[1]");
  ZuCheck(root->resolve("items[1]") == dateTimePtr);
  ZuCheck(dateTimePtr->parent == leafPtr->parent);
  ZuCheck(dateTimePtr->data == value);
  ZuCheck(!root->resolve("items[2]"));
  ZuCheck(!root->resolve("items[-1]"));
  ZuCheck(!root->resolve("items[42949672960]"));
  ZuCheck(!root->resolve("items.[0]"));

  auto moved = ZuMv(root->data[0].p<1>());
  auto movedRoot = ZfTree::newNode<AnyNode::Object>(nullptr);
  moved->parent = movedRoot;
  movedRoot->data.push(AnyNode::Field{"moved", ZuMv(moved)});
  path.null();
  leafPtr->path(path);
  ZuCheck(path == "moved[0]");
  ZuCheck(movedRoot->resolve("moved[0]") == leafPtr);
  ZuCheck(leafPtr->scalarType == ZfTree::ScalarTC::True);

  movedRoot->parent = nullptr;
  path.null();
  movedRoot->path(path);
  ZuCheck(!path);
}

static void ownership()
{
  ZuTestScope(ownership);

  char source[] =
    "a string long enough to spill beyond the builtin node capacity";
  ZtString<> expected;
  expected << ZuCSpan{source, unsigned(sizeof(source) - 1)};
  auto string = ZfTree::newNode<AnyNode::String>(nullptr);
  string->data << ZuCSpan{source, unsigned(sizeof(source) - 1)};
  source[0] = 'X';
  ZuCheck(string->data == expected);
  ZuCheck(string->data.length() > unsigned(AnyNode::StringSize));

  ZuPtr<const AnyNode> immutable = ZuMv(string);
  ZuCheck(!string);
  ZuCheck(immutable->has<AnyNode::String>());
  ZuCheck(immutable->data<AnyNode::String>() == expected);
}

static void scalarPolicy()
{
  ZuTestScope(scalarPolicy);

  auto node = ZfTree::newNode<AnyNode::String>(nullptr, "scalar");
  node->scalarType = ZfTree::ScalarTC::String;
  ZuCheck(ZfTreeLoad::validScalar<Policy>(
    node, ZfTreeLoad::ScalarMask::String()));
  node->scalarType = ZfTree::ScalarTC::Number;
  ZuCheck(ZfTreeLoad::validScalar<Policy>(
    node, ZfTreeLoad::ScalarMask::Number()));
  node->scalarType = ZfTree::ScalarTC::False;
  ZuCheck(ZfTreeLoad::validScalar<Policy>(
    node, ZfTreeLoad::ScalarMask::Bool()));
  node->scalarType = ZfTree::ScalarTC::True;
  ZuCheck(ZfTreeLoad::validScalar<Policy>(
    node, ZfTreeLoad::ScalarMask::Bool()));
  node->scalarType = ZfTree::ScalarTC::Null;
  ZuCheck(!ZfTreeLoad::validScalar<Policy>(
    node, ZfTreeLoad::ScalarMask::String()));
  ZuCheck(ZfTreeLoad::validScalar<Policy>(
    node, ZfTreeLoad::ScalarMask::Null()));

  bool caught = false;
  try {
    (void)ZfTreeLoad::scalar<Policy>(
      node, ZfTreeLoad::ScalarMask::String(), "string");
  } catch (const BadType &) {
    caught = true;
  }
  ZuCheck(caught);
  ZuCheck(Policy::badTypes == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall(representation);
  ZuTestCall(ancestry);
  ZuTestCall(ownership);
  ZuTestCall(scalarPolicy);
}
