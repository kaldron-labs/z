//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtcFilter.hh>

using namespace ZuTestUtil;

void globalWildcard()
{
  ZuTestScope(globalWildcard);

  for (unsigned i = unsigned(Ztc::Group::Heap);
      i <= unsigned(Ztc::Group::Alert); ++i) {
    auto group = Ztc::Group::T(i);
    ZuCSpan key = group == Ztc::Group::Hub ? "*:*" :
      group == Ztc::Group::Queue ? "*:*:*" : "*";
    Ztc::Filter_::Filter filter;
    auto check = [&filter, group, key](ZuCSpan input) {
      ZuCheck(filter.compile(group, input, 32));
      ZuCheck(filter.key() == key);
      ZuCheck(filter.all());
    };
    check({});
    check(ZuCSpan{"", 0});
    check("*");
  }
}

void stringFilter()
{
  ZuTestScope(stringFilter);

  Ztc::Filter_::Filter filter;
  ZuCheck(filter.compile(Ztc::Group::App, "", 32));
  ZuCheck(filter.key() == "*");
  ZuCheck(filter.string("anything"));
  ZuCheck(filter.compile(Ztc::Group::Heap, "*", 32));
  ZuCheck(filter.string("anything"));
  ZuCheck(filter.compile(Ztc::Group::Hash, "alpha", 32));
  ZuCheck(filter.mode0() == Ztc::Filter_::Mode::Exact);
  ZuCheck(filter.text0().exact("alpha"));
  ZuCheck(filter.stringExact("alpha"));
  ZuCheck(filter.string("alpha"));
  ZuCheck(!filter.string("alphabet"));
  ZuCheck(filter.compile(Ztc::Group::Mx, "alpha*", 32));
  ZuCheck(filter.mode0() == Ztc::Filter_::Mode::Prefix);
  ZuCheck(filter.text0().exact("alpha"));
  ZuCheck(filter.stringPrefix("alphabet"));
  ZuCheck(filter.string("alpha"));
  ZuCheck(filter.string("alphabet"));
  ZuCheck(!filter.string("beta"));
  ZuCheck(filter.compile(Ztc::Group::DB, "a%3Ab", 32));
  ZuCheck(filter.key() == "a%3Ab");
  ZuCheck(filter.string("a:b"));
  ZuCheck(!filter.compile(Ztc::Group::App, "a:b", 32));
  ZuCheck(!filter.compile(Ztc::Group::App, "a**", 32));
  ZuCheck(!filter.compile(Ztc::Group::App, "a%3ab", 32));
  ZuCheck(!filter.compile(Ztc::Group::App, "%41", 32));
  ZuCheck(!filter.compile(Ztc::Group::App, "toolong", 3));
  ZuCheck(filter.compile(Ztc::Group::Alert, "", 32));
  ZuCheck(filter.key() == "*" && filter.all());
  ZuCheck(filter.compile(Ztc::Group::Alert, "*", 32));
  ZuCheck(!filter.compile(Ztc::Group::Alert, "alert", 32));
}

void typedFilter()
{
  ZuTestScope(typedFilter);

  Ztc::Filter_::Filter filter;
  ZuCheck(filter.compile(Ztc::Group::Thread, "*", 32));
  ZuCheck(filter.thread(42));
  ZuCheck(filter.compile(Ztc::Group::Thread, "42", 32));
  ZuCheck(filter.key() == "42");
  ZuCheck(filter.thread(42));
  ZuCheck(!filter.thread(43));
  ZuCheck(!filter.compile(Ztc::Group::Thread, "042", 32));

  ZuCheck(filter.compile(Ztc::Group::Hub, "TCP:hub*", 32));
  ZuCheck(filter.hub(Ztc::LinkType::TCP, "hub1"));
  ZuCheck(!filter.hub(Ztc::LinkType::TLS, "hub1"));
  ZuCheck(!filter.hub(Ztc::LinkType::TCP, "other"));
  ZuCheck(filter.compile(Ztc::Group::Hub, ":*", 32));
  ZuCheck(filter.key() == "*:*");
  ZuCheck(filter.hub(Ztc::LinkType::QUIC, "anything"));
  ZuCheck(filter.compile(Ztc::Group::Hub, "TCP:a%3Ab", 32));
  ZuCheck(filter.key() == "TCP:a%3Ab");
  ZuCheck(filter.hub(Ztc::LinkType::TCP, "a:b"));
  ZuCheck(!filter.compile(Ztc::Group::Hub, "Bad:*", 32));

  ZuCheck(filter.compile(Ztc::Group::Queue, "Rx:link*", 32));
  ZuCheck(filter.key() == "Rx:*:link*");
  ZuCheck(filter.queue(Ztc::QueueType::Rx, "hub", "link1"));
  ZuCheck(!filter.queue(Ztc::QueueType::Tx, "hub", "link1"));
  ZuCheck(!filter.compile(Ztc::Group::Queue, "Rx:x", 4));
  ZuCheck(filter.compile(Ztc::Group::Queue, "Rx:x", 6));
  ZuCheck(filter.key() == "Rx:*:x");
  ZuCheck(filter.compile(
    Ztc::Group::Queue, "Tx:hub:link", 32));
  ZuCheck(filter.mode0() == Ztc::Filter_::Mode::Exact);
  ZuCheck(filter.mode1() == Ztc::Filter_::Mode::Exact);
  ZuCheck(filter.mode2() == Ztc::Filter_::Mode::Exact);
  ZuCheck(filter.queue(Ztc::QueueType::Tx, "hub", "link"));
  ZuCheck(!filter.queue(Ztc::QueueType::Tx, "other", "link"));
  ZuCheck(filter.compile(
    Ztc::Group::Queue, "Tx:h%3Aub:l%2Aink", 32));
  ZuCheck(filter.key() == "Tx:h%3Aub:l%2Aink");
  ZuCheck(filter.queue(Ztc::QueueType::Tx, "h:ub", "l*ink"));
  ZuCheck(filter.compile(
    Ztc::Group::Queue, "Tx:h%25ub:%01%C3%A9", 32));
  ZuCheck(filter.key() == "Tx:h%25ub:%01%C3%A9");
  ZuCheck(filter.queue(
    Ztc::QueueType::Tx, "h%ub", ZuCSpan{"\x01\xc3\xa9", 3}));
  ZuCheck(filter.compile(Ztc::Group::Queue, "*:*:*", 32));
  ZuCheck(filter.key() == "*:*:*");
  ZuCheck(filter.queue(Ztc::QueueType::Thread, "mx", "1"));
  ZuCheck(!filter.compile(Ztc::Group::Queue, "Bad:*", 32));
}

int main()
{
  ZuTestMain();
  ZuTestCall(globalWildcard);
  ZuTestCall(stringFilter);
  ZuTestCall(typedFilter);
}
