#include <limits.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtBitWindow.hh>

using namespace ZuTestUtil;

namespace {

constexpr unsigned N = 64;
unsigned head[N];
unsigned tail[N];

void initSequences()
{
  for (unsigned i = 0; i < N; i++) {
    head[i] = (i * 17) % N;
    tail[i] = (N - 1 - i);
  }
}

template <unsigned Bits, uint64_t Value>
void test()
{
  ZuTestScope(test);

  ZtBitWindow<Bits> map;
  for (unsigned i = 0; i < 256; i++) map.set(i);

  int badValue = -1, badClear = -1;
  for (unsigned base = 0; base < 4096; base += N) {
    for (unsigned j = 0; j < N; j++) {
      auto h = base + 256 + head[j];
      auto t = base + tail[j];
      map.set(h, Value);
      if (map.val(h) != Value) { badValue = h; goto bad; }
      map.clr(t);
      if (map.val(t)) { badClear = t; goto bad; }
    }
  }
bad:
  ZuCheck(badValue == -1, log_("unexpected value at ", badValue));
  ZuCheck(badClear == -1, log_("expected cleared value at ", badClear));

  badValue = -1;
  unsigned c = 0;
  map.all([&c, &badValue](uint64_t i, uint64_t v) {
    if (v != Value) { badValue = i; return false; }
    c++;
    return true;
  });

  ZuCheck(badValue == -1, log_("bad value at ", badValue));
  ZuCheck(c == 256);
  log("bits=", Bits, " value=", Value, " count=", c);
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  initSequences();
  ZuTestCall_("1-bit", (test<1, 1>));
  ZuTestCall_("2-bit", (test<2, 3>));
  ZuTestCall_("3-bit", (test<3, 5>));
  ZuTestCall_("4-bit", (test<4, 10>));
  ZuTestCall_("5-bit", (test<5, 25>));
  ZuTestCall_("8-bit", (test<8, 129>));
  ZuTestCall_("10-bit", (test<10, 735>));
  ZuTestCall_("12-bit", (test<12, 3051>));
  ZuTestCall_("16-bit", (test<16, 32771>));
  ZuTestCall_("32-bit", (test<32, 1073741827>));
  ZuTestCall_("64-bit", (test<64, static_cast<uint64_t>(1152921504606846979ULL)>));
  return 0;
}
