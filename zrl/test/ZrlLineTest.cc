#include <stdio.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZrlLine.hh>

using namespace ZuTestUtil;

#define CHECK(x) ZuCHECK(x, #x)

char ascii(char c) {
  return (c >= 0x20 && c < 0x7f) ? c : '.';
}

static void line()
{
  ZuTestScope(line);
  Zrl::Line l;
  auto &data = l.data();

  ZuCArray<12> j;
  {
    uint32_t u = 0x1f404;
    j.length(ZuUTF<uint8_t, uint32_t>::cvt(j.span(), {&u, 1}));
  }
  data << 'x' << j;

  l.reflow(0, 2);
  CHECK(l.align(1) == 0);
  CHECK(l.align(2) == 2);
  CHECK(l.align(3) == 2);
  CHECK(l.align(4) == 4);
  l.reflow(0, 3);
  CHECK(l.align(1) == 1);
  CHECK(l.align(2) == 1);
  CHECK(l.align(3) == 3);

  data = "hello "; data << j << " world(,);";

  l.reflow(0, 20);

  CHECK(l.align(7) == 6);

  CHECK(l.fwdGlyph(6) == 10);
  CHECK(l.revGlyph(6) == 5);
  CHECK(l.fwdGlyph(7) == 10);
  CHECK(l.revGlyph(10) == 6);
  CHECK(l.revGlyph(9) == 5);
  CHECK(l.fwdGlyph(19) == 20);
  CHECK(l.fwdGlyph(20) == 20);

  CHECK(l.fwdWord(6) == 11);
  CHECK(l.fwdWord(10) == 11);
  CHECK(l.fwdWord(12) == 16);
  CHECK(l.fwdWordEnd(12, false) == 15);
  CHECK(l.revWordEnd(12, false) == 6);
  CHECK(l.fwdWord(16) == 20);
  CHECK(l.fwdWordEnd(16, false) == 19);
  CHECK(l.revWord(20) == 16);
  CHECK(l.revWordEnd(20, false) == 19);
  CHECK(l.revWord(16) == 11);
  CHECK(l.revWordEnd(16, false) == 15);

  CHECK(l.fwdUnixWord(6) == 11);
  CHECK(l.fwdUnixWord(10) == 11);
  CHECK(l.fwdUnixWord(12) == 20);
  CHECK(l.fwdUnixWordEnd(12, false) == 19);
  CHECK(l.revUnixWordEnd(12, false) == 6);
  CHECK(l.fwdUnixWord(16) == 20);
  CHECK(l.fwdUnixWordEnd(16, false) == 19);
  CHECK(l.revUnixWord(20) == 11);
  CHECK(l.revUnixWordEnd(20, false) == 19);
  CHECK(l.revUnixWord(16) == 11);
  CHECK(l.revUnixWordEnd(16, false) == 6);

  CHECK(l.fwdWord(7) == 11);
  CHECK(l.revWord(7) == 0);
  CHECK(l.fwdWordEnd(7, false) == 15);
  CHECK(l.revWordEnd(7, false) == 4);
  CHECK(l.fwdUnixWord(7) == 11);
  CHECK(l.revUnixWord(7) == 0);
  CHECK(l.fwdUnixWordEnd(7, false) == 19);
  CHECK(l.revUnixWordEnd(7, false) == 4);

  CHECK(l.fwdWord(0) == 6);
  CHECK(l.revWord(0) == 0);
  CHECK(l.fwdWordEnd(0, false) == 4);
  CHECK(l.revWordEnd(0, false) == 0);
  CHECK(l.fwdUnixWord(0) == 6);
  CHECK(l.revUnixWord(0) == 0);
  CHECK(l.fwdUnixWordEnd(0, false) == 4);
  CHECK(l.revUnixWordEnd(0, false) == 0);

  CHECK(l.fwdWord(21) == 20);
  CHECK(l.revWord(21) == 16);
  CHECK(l.fwdWordEnd(21, false) == 19);
  CHECK(l.revWordEnd(21, false) == 19);
  CHECK(l.fwdUnixWord(21) == 20);
  CHECK(l.revUnixWord(21) == 11);
  CHECK(l.fwdUnixWordEnd(21, false) == 19);
  CHECK(l.revUnixWordEnd(21, false) == 19);

  l.reflow(0, 7);

  ZuCHECK(l.width() == 7, "narrow reflow width");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(line);
  return 0;
}
