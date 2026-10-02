//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuStruct.hh>
// #include <zlib/ZuDemangle.hh>

using namespace ZuTestUtil;

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

struct BasicIDs {
  using Keys = ZuStringTL<"foo", "foh", "bar", "baz">;
};
struct FailureIDs { using Keys = ZuStringTL<"fooh", "oox">; };
struct PrefixIDs { using Keys = ZuStringTL<"f", "fo", "foo">; };
struct OneIDs { using Keys = ZuStringTL<"x">; };
struct EmptyIDs { using Keys = ZuTypeList<>; };
struct EmptyKeyIDs { using Keys = ZuStringTL<"">; };
struct FieldIDs {
  using Keys = ZuFieldProp::JSON::GetIDs<Foo::B>;
};
struct ChainIDs { using Keys = ZuStringTL<"a", "abcd">; };
struct SuffixIDs { using Keys = ZuStringTL<"he", "shell">; };
struct DirectIDs { using Keys = ZuStringTL<"he", "she">; };
struct SameA : public ZuStringT<"same"> { };
struct SameB : public ZuStringT<"same"> { };
struct DuplicateIDs {
  using Key = ZuStringT<"same">;
  using Keys = ZuTypeList<Key, Key>;
};
struct SameBytesIDs { using Keys = ZuTypeList<SameA, SameB>; };
struct BinaryIDs {
  using Keys = ZuStringTL<"a\0b", "\1\2">;
};
struct MultiEmptyIDs { using Keys = ZuStringTL<"", "a">; };
struct EditorIDs {
  using Keys = ZuStringTL<"Null", "Nop", "Syn", "Mode", "Push", "Pop",
    "Error", "EndOfFile", "SigInt", "SigQuit", "SigSusp", "Enter", "Up",
    "Down", "Left", "Right", "Home", "End", "FwdWord", "RevWord",
    "FwdWordEnd", "RevWordEnd", "MvMark", "ClrVis", "InsToggle", "Insert",
    "Over", "Clear", "Redraw", "Paste", "Yank", "Rotate", "Glyph",
    "InsGlyph", "OverGlyph", "BackSpace", "Edit", "EditRep", "ArgDigit",
    "Register", "Undo", "Redo", "EmacsUndo", "EmacsAbort", "Repeat",
    "TransGlyph", "TransWord", "TransUnixWord", "CapGlyph", "LowerWord",
    "UpperWord", "CapWord", "LowerVis", "UpperVis", "CapVis", "XchMark",
    "FwdGlyphSrch", "RevGlyphSrch", "Complete", "RevComplete", "ListComplete",
    "Next", "Prev", "ClrIncSrch", "FwdIncSrch", "RevIncSrch", "PromptSrch",
    "EnterSrchFwd", "EnterSrchRev", "AbortSrch", "FwdSearch", "RevSearch">;
};

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  {
    constexpr ZuString L("l");
    // constexpr ZuString R("r");
    using LR = ZuFieldProp::JSON::ID<L + "r"_z>;
    (void)sizeof(LR);
    ZuCHECK(((L + "r"_z) == "lr"));
  }
  // std::cerr << ZuDemangle<decltype(names)>{} << '\n';
  {
    constexpr auto matcher = ZuMatcher<BasicIDs>();
    if (verbose) {
      std::cerr << "nodes.size()=" << matcher.size() << "\n";
      std::cerr << "nodes.length()=" << matcher.length() << "\n";
      std::cerr << "sizeof(Node)=" << sizeof(matcher.automaton.nodes[0]) << "\n";
      std::cerr << "Begin='" << char(matcher.automaton.Begin) << "' "
	<< ZuBox<unsigned>(matcher.automaton.Begin).hex() << "\n";
      std::cerr << "End='" << char(matcher.automaton.End) << "' "
	<< ZuBox<unsigned>(matcher.automaton.End).hex() << "\n";
    }
    ZuCHECK((matcher.match("fo") < 0));
    ZuCHECK((matcher.match("foo") == 0));
    ZuCHECK((matcher.match("foo!") == 0));
    ZuCHECK((matcher.match("foh") == 1));
    ZuCHECK((matcher.match("bar") == 2));
    ZuCHECK((matcher.match("baz") == 3));
    ZuCHECK((matcher.match("xbaz") == -1));
    ZuCHECK((matcher.exact("fo") < 0));
    ZuCHECK((matcher.exact("foo") == 0));
    ZuCHECK((matcher.exact("foo!") < 0));
    ZuCHECK((matcher.exact("foh") == 1));
    ZuCHECK((matcher.exact("baz") == 3));
    ZuCHECK((matcher.exact("xbaz") == -1));
    ZuCHECK((matcher.find("xbaz") == ZuTuple<int, int>{1, 3}));
    ZuCHECK((matcher.find("x_baz") == ZuTuple<int, int>{2, 3}));
  }
  {
    constexpr auto matcher = ZuMatcher<FailureIDs>();
    ZuCHECK((matcher.match("foox") < 0));
    ZuCHECK((matcher.find("foox") == ZuTuple<int, int>{1, 1}));
  }
  {
    constexpr auto matcher = ZuMatcher<PrefixIDs>();
    ZuCHECK((matcher.match("foo") == 2));
    ZuCHECK((matcher.match("foo!") == 2));
    ZuCHECK((matcher.exact("f") == 0));
    ZuCHECK((matcher.exact("fo") == 1));
    ZuCHECK((matcher.exact("foo") == 2));
    ZuCHECK((matcher.exact("foo!") < 0));
  }
  {
    constexpr auto matcher = ZuMatcher<OneIDs>();
    ZuCHECK((matcher.match("x") == 0));
    ZuCHECK((matcher.match("y") == -1));
    ZuCHECK((matcher.exact("x") == 0));
    ZuCHECK((matcher.exact("xx") == -1));
    ZuCHECK((matcher.find("abcx") == ZuTuple<int, int>{3, 0}));
    ZuCHECK((matcher.find("abc") == ZuTuple<int, int>{-1, -1}));
  }
  {
    constexpr auto matcher = ZuMatcher<FieldIDs>();
    char buf[32];
    strcpy(buf, "i-JSON");
    ZuCHECK((matcher.match({&buf[0], unsigned(strlen(buf))}) == 0));
    ZuCHECK((matcher.match("i-JSON") == 0));
    ZuCHECK((matcher.match("j-JSON") == 1));
    ZuCHECK((matcher.match("k") == 2));
    ZuCHECK((matcher.match("baz") == -1));
  }
  {
    // this large automaton from ZrlEditor fails to build with clang's
    // default -fconstexpr-steps of 1,000,000
    constexpr auto matcher = ZuMatcher<EditorIDs>();
    ZuCHECK((matcher.match("Nop") == 1));
    ZuCHECK((matcher.match("Next") == 61));
  }

  {
    constexpr auto empty = ZuMatcher<EmptyIDs>();
    static_assert(empty.match("anything") == -1);
    static_assert(empty.exact("") == -1);
    static_assert(empty.find("anything") == ZuTuple<int, int>{-1, -1});
    static_assert(ZuIsSame<decltype(empty.match({})), int>{});
    static_assert(ZuIsSame<decltype(empty.exact({})), int>{});
    static_assert(ZuIsSame<decltype(empty.find({})), ZuTuple<int, int>>{});
    char input[] = {'x', 'y'};
    ZuCHECK(empty.match({input, 2}) == -1);
    ZuCHECK(empty.exact({input, 2}) == -1);
    ZuCHECK((empty.find({input, 2}) == ZuTuple<int, int>{-1, -1}));
  }
  {
    constexpr auto one = ZuMatcher<OneIDs>();
    static_assert(one.match("x") == 0);
    static_assert(one.exact("x") == 0);
    static_assert(one.find("_x") == ZuTuple<int, int>{1, 0});
    static_assert(ZuIsSame<decltype(one.match({})), int>{});
    static_assert(ZuIsSame<decltype(one.exact({})), int>{});
    static_assert(ZuIsSame<decltype(one.find({})), ZuTuple<int, int>>{});
    char hit[] = {'x'}, miss[] = {'y'}, longer[] = {'x', 'x'};
    ZuCHECK(one.match({hit, 1}) == 0);
    ZuCHECK(one.match({miss, 1}) == -1);
    ZuCHECK(one.match({}) == -1);
    ZuCHECK(one.match({longer, 2}) == 0);
    ZuCHECK(one.exact({longer, 2}) == -1);
    ZuCHECK((one.find({longer, 2}) == ZuTuple<int, int>{0, 0}));
  }
  {
    constexpr auto emptyKey = ZuMatcher<EmptyKeyIDs>();
    static_assert(emptyKey.match("") == 0);
    static_assert(emptyKey.match("x") == 0);
    static_assert(emptyKey.exact("") == 0);
    static_assert(emptyKey.exact("x") == -1);
    static_assert(emptyKey.find("x") == ZuTuple<int, int>{0, 0});
    ZuCHECK(emptyKey.match({}) == 0);
  }
  {
    constexpr auto chain = ZuMatcher<ChainIDs>();
    constexpr auto suffix = ZuMatcher<SuffixIDs>();
    constexpr auto direct = ZuMatcher<DirectIDs>();
    static_assert(chain.match("abX") == -1);
    static_assert(suffix.match("she") == 0);
    static_assert(direct.find("she") == ZuTuple<int, int>{0, 1});
    ZuCHECK(chain.match("abX") == -1);
    ZuCHECK(suffix.match("she") == 0);
    ZuCHECK((direct.find("she") == ZuTuple<int, int>{0, 1}));
  }
  {
    constexpr auto duplicate = ZuMatcher<DuplicateIDs>();
    constexpr auto sameBytes = ZuMatcher<SameBytesIDs>();
    ZuCHECK(duplicate.exact("same") == 0);
    ZuCHECK(sameBytes.exact("same") == 1);
  }
  {
    constexpr auto binary = ZuMatcher<BinaryIDs>();
    char key0[] = {'a', '\0', 'b'};
    char key1[] = {'_', '\1', '\2', '_'};
    ZuCHECK(binary.exact({key0, 3}) == 0);
    ZuCHECK((binary.find({key1, 4}) == ZuTuple<int, int>{1, 1}));
  }
  {
    constexpr auto matcher = ZuMatcher<MultiEmptyIDs>();
    ZuCHECK(matcher.match("") == 0);
    ZuCHECK(matcher.exact("") == 0);
  }
  {
    constexpr auto matcher = ZuMatcher<BasicIDs>();
    char outside[] = {char(0xff), 'f', 'o', 'o'};
    ZuCHECK(matcher.match({outside, 4}) == -1);
    ZuCHECK((matcher.find({outside, 4}) == ZuTuple<int, int>{1, 0}));
    ZuCHECK(matcher.automaton.nodes[0].chars.length() == 2);
    bool outputs = true, failures = true, transitions = true;
    for (unsigned i = 0; i < matcher.length(); ++i) {
      outputs &= matcher.output_(i) == matcher.automaton.nodes[i].output;
      failures &= matcher.fail_(i) == matcher.automaton.nodes[i].fail;
      for (unsigned c = 0; c < 256; ++c) {
	uint16_t expected = c < matcher.begin() || c >= matcher.end() ?
	  uint16_t(!i) : matcher.automaton.nodes[i].next(c);
	transitions &= matcher.next_(i, c) == expected;
      }
    }
    ZuCHECK(outputs);
    ZuCHECK(failures);
    ZuCHECK(transitions);
  }
}
