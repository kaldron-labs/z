//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZiCSV.hh>
#include <zlib/ZiFile.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

struct CSVRow {
  ZuCArray<64>	text;
  int32_t	id = 0;
};

ZtStruct(CSVRow,
  (((text), (Ctor<0>)), (String)),
  (((id),   (Ctor<1>)), (Int32)));

namespace {
Zi::Path g_csv;
Zi::Path g_filtered;
Zi::Path g_overflow;
Zi::Path g_append;

bool contains(const ZtString<> &s, const char *needle)
{
  ZuCSpan span{s};
  return ::memmem(span.data(), span.length(), needle, ::strlen(needle));
}

void initPaths()
{
  auto root = ZiTestResidue::tempRoot();
  g_csv = ZiFile::append(root, "rows.csv");
  g_filtered = ZiFile::append(root, "filtered.csv");
  g_overflow = ZiFile::append(root, "overflow.csv");
  g_append = ZiFile::append(root, "append.csv");

  ZiTestResidue::addFile(g_csv);
  ZiTestResidue::addFile(g_filtered);
  ZiTestResidue::addFile(g_overflow);
  ZiTestResidue::addFile(g_append);
}

ZtString<> readFile(const Zi::Path &path)
{
  ZiFile f;
  if (f.open(path, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) {
    log_("open(", path, ") failed: ", f.error());
    Zm::exit(1);
  }
  auto n = f.size();
  auto buf = ZmAlloc(char, static_cast<unsigned>(n) + 1U);
  int r = f.read(&buf[0], static_cast<unsigned>(n));
  if (r < 0) {
    log_("read(", path, ") failed: ", f.error());
    Zm::exit(1);
  }
  ZtString<> out;
  out << ZuCSpan(&buf[0], static_cast<unsigned>(r));
  return out;
}

void testRoundTrip()
{
  ZuTestScope(testRoundTrip);

  unsigned i = 0;
  auto w = ZiCSV::writeFile<CSVRow>(g_csv, ZiCSV::Replace, [&i](auto emit) {
    CSVRow row;
    switch (i++) {
      case 0:
	row.text = "alpha";
	row.id = 1;
	emit(row);
	return true;
      case 1:
	row.text = "beta";
	row.id = 2;
	emit(row);
	return true;
      default:
	return false;
    }
  });
  ZuCheck(!w.template is<ZeException>());

  auto reader = ZiCSV::reader<CSVRow>();
  unsigned count = 0;
  auto r = reader.readFile(g_csv, [&count](const auto &scan) {
    CSVRow row = scan.ctor();
    if (!count) {
      ZuCheck(row.text == "alpha");
      ZuCheck(row.id == 1);
    } else if (count == 1) {
      ZuCheck(row.text == "beta");
      ZuCheck(row.id == 2);
    }
    ++count;
  });
  ZuCheck(!r.template is<ZeException>());
  ZuCheck(count == 2);
}

void testFilteredColumns()
{
  ZuTestScope(testFilteredColumns);

  unsigned i = 0;
  auto w = ZiCSV::writeFile<CSVRow>({
      ZtFieldIndex(CSVRow, text)
    }, g_filtered, ZiCSV::Replace, [&i](auto emit) {
      CSVRow row;
      switch (i++) {
	case 0:
	  row.text = "alpha";
	  row.id = 1;
	  emit(row);
	  return true;
	case 1:
	  row.text = "beta";
	  row.id = 2;
	  emit(row);
	  return true;
	default:
	  return false;
      }
    });
  ZuCheck(!w.template is<ZeException>());

  auto text = readFile(g_filtered);
  ZuCheck(contains(text, "alpha"));
  ZuCheck(contains(text, "beta"));
  ZuCheck(!contains(text, ",1"));
  ZuCheck(!contains(text, ",2"));
}

void testOverflowAndMissingFileErrors()
{
  ZuTestScope(testOverflowAndMissingFileErrors);

  unsigned i = 0;
  auto w = ZiCSV::writeFile<CSVRow, ZuFacet::Core, 16>(
      g_overflow, ZiCSV::Replace, [&i](auto emit) {
    if (i++) return false;
    CSVRow row;
    row.text = "this is a row that is intentionally too long";
    row.id = 7;
    emit(row);
    return true;
  });
  ZuCheck(w.template is<ZeException>());

  auto reader = ZiCSV::reader<CSVRow>();
  auto r = reader.readFile(ZiFile::append(ZiTestResidue::tempRoot(), "missing.csv"),
      [](const auto &) {
      });
  ZuCheck(r.template is<ZeException>());
}

void testAppend()
{
  ZuTestScope(testAppend);

  {
    auto w = ZiCSV::writeFile<CSVRow>(g_append, ZiCSV::Replace);
    CSVRow row;
    row.text = "alpha";
    row.id = 1;
    ZuCheck(w(row));
  }
  {
    auto w = ZiCSV::writeFile<CSVRow>({
	ZtFieldIndex(CSVRow, id),
	ZtFieldIndex(CSVRow, text)
      }, g_append, ZiCSV::Append);
    CSVRow row;
    row.text = "beta";
    row.id = 2;
    ZuCheck(w(row));
  }

  auto text = readFile(g_append);
  ZuCheck(contains(text, "text,id\n"));
  ZuCheck(contains(text, "\"alpha\",1\n"));
  ZuCheck(contains(text, "\"beta\",2\n"));

  {
    auto w = ZiCSV::writeFile<CSVRow>({
	ZtFieldIndex(CSVRow, text)
      }, g_append, ZiCSV::Append);
    ZuCheck(!w);
    ZuCheck(w.error);
  }
}

} // namespace

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiCSVTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanupNow);
  ZmTrap::trap();
  ::atexit(&ZiTestResidue::cleanupNow);

  initPaths();
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRoundTrip);
  ZuTestCall(testFilteredColumns);
  ZuTestCall(testOverflowAndMissingFileErrors);
  ZuTestCall(testAppend);
  return 0;
}
