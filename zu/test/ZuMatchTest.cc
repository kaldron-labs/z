#include <zlib/ZuLib.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuStruct.hh>
// #include <zlib/ZuDemangle.hh>

#include <iostream>

inline void out(const char *s) {
  std::cout << s << '\n' << std::flush;
}

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

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

constexpr auto names = { "x", "y" };

int main() {
  // std::cout << ZuDemangle<decltype(names)>{} << '\n';
  {
    constexpr auto matcher = ZuMatcher<"foo", "foh", "bar", "baz">();
    std::cout << "nodes.size()=" << matcher.size() << "\n";
    std::cout << "nodes.length()=" << matcher.length() << "\n";
    std::cout << "sizeof(Node)=" << sizeof(matcher.automaton.nodes[0]) << "\n";
    std::cout << "Begin='" << char(matcher.automaton.Begin) << "' "
      << ZuBox<unsigned>(matcher.automaton.Begin).hex() << "\n";
    std::cout << "End='" << char(matcher.automaton.End) << "' "
      << ZuBox<unsigned>(matcher.automaton.End).hex() << "\n";
    CHECK((matcher.match("fo") < 0));
    CHECK((matcher.match("foo") == 0));
    CHECK((matcher.match("foo!") == 0));
    CHECK((matcher.match("foh") == 1));
    CHECK((matcher.match("bar") == 2));
    CHECK((matcher.match("baz") == 3));
    CHECK((matcher.match("xbaz") == -1));
    CHECK((matcher.find("xbaz") == ZuTuple<int, int>{1, 3}));
    CHECK((matcher.find("x_baz") == ZuTuple<int, int>{2, 3}));
  }
  {
    constexpr auto matcher = ZuMatcher<"fooh", "oox">();
    CHECK((matcher.match("foox") < 0));
    CHECK((matcher.find("foox") == ZuTuple<int, int>{1, 1}));
  }
  {
    constexpr auto &x = "x";
    constexpr auto matcher = ZuMatcher<x>();
    CHECK((matcher.match("x") == 0));
  }
  {
    constexpr auto matcher = ZuMatcher<ZuFieldProp::JSON::GetIDs<Foo::B>>();
    char buf[32];
    strcpy(buf, "i-JSON");
    CHECK((matcher.match({&buf[0], unsigned(strlen(buf))}) == 0));
    CHECK((matcher.match("i-JSON") == 0));
    CHECK((matcher.match("j-JSON") == 1));
    CHECK((matcher.match("k") == 2));
    CHECK((matcher.match("baz") == -1));
  }
  {
    // this large automaton from ZrlEditor fails to build with clang's
    // default -fconstexpr-steps of 1,000,000
    constexpr auto matcher = ZuMatcher<"Null", "Nop", "Syn", "Mode", "Push", "Pop", "Error", "EndOfFile", "SigInt", "SigQuit", "SigSusp", "Enter", "Up", "Down", "Left", "Right", "Home", "End", "FwdWord", "RevWord", "FwdWordEnd", "RevWordEnd", "MvMark", "ClrVis", "InsToggle", "Insert", "Over", "Clear", "Redraw", "Paste", "Yank", "Rotate", "Glyph", "InsGlyph", "OverGlyph", "BackSpace", "Edit", "EditRep", "ArgDigit", "Register", "Undo", "Redo", "EmacsUndo", "EmacsAbort", "Repeat", "TransGlyph", "TransWord", "TransUnixWord", "CapGlyph", "LowerWord", "UpperWord", "CapWord", "LowerVis", "UpperVis", "CapVis", "XchMark", "FwdGlyphSrch", "RevGlyphSrch", "Complete", "RevComplete", "ListComplete", "Next", "Prev", "ClrIncSrch", "FwdIncSrch", "RevIncSrch", "PromptSrch", "EnterSrchFwd", "EnterSrchRev", "AbortSrch", "FwdSearch", "RevSearch">();
    CHECK((matcher.match("Nop") == 1));
    CHECK((matcher.match("Next") == 61));
  }
}
