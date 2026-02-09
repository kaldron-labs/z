//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTest.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuStruct.hh>
// #include <zlib/ZuDemangle.hh>

bool verbose = false;

ZuStructFacet(JSON);

namespace ZuFieldProp::JSON {
template <ZuString> struct ID { };
// GetID<Field> - ZuStringT
// - gets the JSON-specific ID for the field
// - the Field is passed because the value defaults to Field::id()
template <
  typename Field,
  bool = HasValue<typename Field::Props, ID>{}>
struct GetID_ {
  using T = ZuStringT<Field::id()>;
};
template <typename Field>
struct GetID_<Field, true> {
  using T = GetValue<typename Field::Props, ID>;
};
template <typename Field>
using GetID = typename GetID_<Field>::T;

// obtain a consteval typelist of field IDs suitable for use with ZuMatcher_<>
template <typename> struct GetIDs_;
template <typename ...Field>
struct GetIDs_<ZuTypeList<Field...>> {
  using T = ZuTypeList<GetID<Field>...>;
};
template <typename O>
struct GetIDs_ : public GetIDs_<ZuFields<O>> { };
template <typename U>
using GetIDs = typename GetIDs_<U>::T;
} // ZuFieldProp::JSON

namespace Foo {
  struct B {
    int i = 42;
    const char *j_ = "hello";
    const char *j() const { return j_; }
    double k = 42.0;
  };

  ZuStruct((B, JSON),
      ((i, Rd), (Keys<0>, JSON::ID<"i-JSON">)),
      ((j, RdFn), (Keys<0>, JSON::ID<"j-JSON">)),
      ((k, LambdaRd, ([](const B &b) { return b.k; }))));
}

static void usage()
{
  std::cerr << "usage: ZuBoxTest [-v]\n";
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2) usage();
  if (argc == 2) {
    if (strcmp(argv[1], "-v")) usage();
    verbose = true;
  }

  ZuTestMain();

  // std::cerr << ZuDemangle<decltype(names)>{} << '\n';
  {
    constexpr auto matcher = ZuMatcher<"foo", "foh", "bar", "baz">();
    if (verbose) {
      std::cerr << "nodes.size()=" << matcher.size() << "\n";
      std::cerr << "nodes.length()=" << matcher.length() << "\n";
      std::cerr << "sizeof(Node)=" << sizeof(matcher.automaton.nodes[0]) << "\n";
      std::cerr << "Begin='" << char(matcher.automaton.Begin) << "' "
	<< ZuBox<unsigned>(matcher.automaton.Begin).hex() << "\n";
      std::cerr << "End='" << char(matcher.automaton.End) << "' "
	<< ZuBox<unsigned>(matcher.automaton.End).hex() << "\n";
    }
    ZuCheck((matcher.match("fo") < 0));
    ZuCheck((matcher.match("foo") == 0));
    ZuCheck((matcher.match("foo!") == 0));
    ZuCheck((matcher.match("foh") == 1));
    ZuCheck((matcher.match("bar") == 2));
    ZuCheck((matcher.match("baz") == 3));
    ZuCheck((matcher.match("xbaz") == -1));
    ZuCheck((matcher.find("xbaz") == ZuTuple<int, int>{1, 3}));
    ZuCheck((matcher.find("x_baz") == ZuTuple<int, int>{2, 3}));
  }
  {
    constexpr auto matcher = ZuMatcher<"fooh", "oox">();
    ZuCheck((matcher.match("foox") < 0));
    ZuCheck((matcher.find("foox") == ZuTuple<int, int>{1, 1}));
  }
  {
    constexpr auto &x = "x";
    constexpr auto matcher = ZuMatcher<x>();
    ZuCheck((matcher.match("x") == 0));
  }
  {
    constexpr auto matcher = ZuMatcher<ZuFieldProp::JSON::GetIDs<Foo::B>>();
    char buf[32];
    strcpy(buf, "i-JSON");
    ZuCheck((matcher.match({&buf[0], unsigned(strlen(buf))}) == 0));
    ZuCheck((matcher.match("i-JSON") == 0));
    ZuCheck((matcher.match("j-JSON") == 1));
    ZuCheck((matcher.match("k") == 2));
    ZuCheck((matcher.match("baz") == -1));
  }
  {
    // this large automaton from ZrlEditor fails to build with clang's
    // default -fconstexpr-steps of 1,000,000
    constexpr auto matcher = ZuMatcher<"Null", "Nop", "Syn", "Mode", "Push", "Pop", "Error", "EndOfFile", "SigInt", "SigQuit", "SigSusp", "Enter", "Up", "Down", "Left", "Right", "Home", "End", "FwdWord", "RevWord", "FwdWordEnd", "RevWordEnd", "MvMark", "ClrVis", "InsToggle", "Insert", "Over", "Clear", "Redraw", "Paste", "Yank", "Rotate", "Glyph", "InsGlyph", "OverGlyph", "BackSpace", "Edit", "EditRep", "ArgDigit", "Register", "Undo", "Redo", "EmacsUndo", "EmacsAbort", "Repeat", "TransGlyph", "TransWord", "TransUnixWord", "CapGlyph", "LowerWord", "UpperWord", "CapWord", "LowerVis", "UpperVis", "CapVis", "XchMark", "FwdGlyphSrch", "RevGlyphSrch", "Complete", "RevComplete", "ListComplete", "Next", "Prev", "ClrIncSrch", "FwdIncSrch", "RevIncSrch", "PromptSrch", "EnterSrchFwd", "EnterSrchRev", "AbortSrch", "FwdSearch", "RevSearch">();
    ZuCheck((matcher.match("Nop") == 1));
    ZuCheck((matcher.match("Next") == 61));
  }
}
