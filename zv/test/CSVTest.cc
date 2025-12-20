//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stddef.h>

#include <zlib/ZmList.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZeLog.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZvCSV.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

static const char testdata[] =
  "string,int,bool,float,enum,time,flags,func,A,B,C,,\n"
  "string,199,Y,1.234,serene,2011/11/11 12:00:00,Flag1,A,B,C,D,,,\n"
  "string2,23,N,0.00042,grey,2011/11/11 12:12:12.1234,SUP,,,\n"
  "\"-,>\"\"<,-\",2,,0.0000002,\"\"\"\"zone,,Flag1|Flag2,,,\n"
  "-->\",\"<--,3,N,3.1415926,\"event\"\"\",,Flag1,,,\n";

static const char filtered[] =
  "string,flags\n"
  "\"string\",\"Flag1\"\n"
  "\"string2\",\"SUP\"\n"
  "\"-,>\"\"<,-\",\"Flag1|Flag2\"\n"
  "\"-->,<--\",\"Flag1\"\n";

namespace Enums {
  ZtEnumValues(int8_t, Serene, Grey, Zone, Event, __);
  ZtEnumMap(Enums, Map, "serene", "grey", "\"zone", "event\"", "");
}

namespace DaFlags {
  ZtFlags(DaFlags, uint32_t, Flag1, Flag2, P, SUP);
}

struct Row_ {
  ZuCArray<24>	m_string;
  int		m_int;
  int		m_bool;
  double	m_float;
  int		m_enum;
  ZuDateTime	m_time;
  int		m_flags;
};

ZtStruct(Row_,
  (((string, Alias, m_string), (Ctor<0>)), (String)),
  (((int, Alias, m_int), (Ctor<1>)), (Int32)),
  (((bool, Alias, m_bool), (Ctor<2>)), (Bool)),
  (((float, Alias, m_float), (Ctor<3>, NDP<2>)), (Float)),
  (((enum, Alias, m_enum), (Ctor<4>, Enum<Enums::Map>)), (Int32)),
  (((time, Alias, m_time), (Ctor<5>)), (DateTime)),
  (((flags, Alias, m_flags), (Ctor<6>, Flags<DaFlags::Map>)), (UInt32)));

struct RowNode : public ZmPolymorph, public Row_ {
  ZuDerive_(RowNode, Row_);
};

ZuDerive(RowList, (ZmList<RowNode, ZmListNode<RowNode>>));
using Row = RowList::Node;

void gtfo()
{
  ZeLog::stop();
  exit(1);
}

int main()
{
  ZeLog::init("CSVTest");
  ZeLog::level(0);
  ZeLog::sink(ZeLog::fileSink(ZeSinkOptions{}.path("&2")));
  ZeLog::start();

  try {
    {
      ZiFile file;
      if (file.open("in.csv", ZiFile::Write) != Zi::OK)
	throw ZeEXCEPT(Error, ([e = file.error()](auto &s) {
	  s << "open(\"in.csv\") " << e;
	}));
      if (file.write(&testdata[0], sizeof(testdata) - 1) != Zi::OK)
	throw ZeEXCEPT(Error, ([e = file.error()](auto &s) {
	  s << "write(\"in.csv\") " << e;
	}));
    }
    RowList rows;
    {
      auto reader = ZvCSV::reader<Row>();
      auto r = reader.readFile("in.csv",
	[&rows](const auto &reader) {
	  rows.pushNode(new Row{reader.ctor()});
	});
      if (r.template is<ZeException>())
	throw ZuMv(r).template p<ZeException>();
    }
    {
      auto r = ZvCSV::writeFile<Row>("out.csv", [&rows](auto l) -> bool {
	if (auto node = rows.shift()) { l(*node); return true; }
	return false;
      });
      if (r.template is<ZeException>())
	throw ZuMv(r).template p<ZeException>();
    }
    {
      auto reader = ZvCSV::reader<Row>();
      auto r = reader.readFile("out.csv",
	[&rows](const auto &reader) {
	  rows.pushNode(new Row{reader.ctor()});
	});
      if (r.template is<ZeException>())
	throw ZuMv(r).template p<ZeException>();
    }
    {
      auto i = rows.citer();
      auto r = ZvCSV::writeFile<Row>("out2.csv", [&i](auto l) {
	if (auto node = i()) { l(*node); return true; }
	return false;
      });
      if (r.template is<ZeException>())
	throw ZuMv(r).template p<ZeException>();
    }
    {
      ZiFile file;
      if (file.open("out.csv", ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
	throw ZeEXCEPT(Error, ([e = file.error()](auto &s) {
	  s << "open(\"out.csv\") " << e;
	}));
      auto size = file.size();
      auto buf = ZmAlloc(char, size);
      if (file.read(&buf[0], size) < size)
	throw ZeEXCEPT(Error, ([e = file.error()](auto &s) {
	  s << "read(\"out.csv\") " << e;
	}));
      file.close();
      if (file.open("out2.csv", ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
	throw ZeEXCEPT(Error, ([e = file.error()](auto &s) {
	  s << "open(\"out2.csv\") " << e;
	}));
      auto size2 = file.size();
      auto buf2 = ZmAlloc(char, size2);
      if (file.read(&buf2[0], size2) < size)
	throw ZeEXCEPT(Error, ([e = file.error()](auto &s) {
	  s << "read(\"out2.csv\") " << e;
	}));
      CHECK(size == size2 && !memcmp(&buf[0], &buf2[0], size));
    }
    {
      auto i = rows.citer();
      auto r = ZvCSV::writeFile<Row>({
	ZtFieldIndex(Row_, string),
	ZtFieldIndex(Row_, flags)
      }, "filtered.csv", [&i](auto l) {
	if (auto node = i()) { l(*node); return true; }
	return false;
      });
      if (r.template is<ZeException>())
	throw ZuMv(r).template p<ZeException>();
    }
    {
      ZiFile file;
      if (file.open(
	  "filtered.csv", ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
	throw ZeEXCEPT(Error, ([e = file.error()](auto &s) {
	  s << "open(\"filtered.csv\") " << e;
	}));
      auto size = file.size();
      auto buf = ZmAlloc(char, size);
      if (file.read(&buf[0], size) < size)
	throw ZeEXCEPT(Error, ([e = file.error()](auto &s) {
	  s << "read(\"filtered.csv\") " << e;
	}));
      CHECK(
	size == sizeof(filtered) - 1 && !memcmp(&buf[0], &filtered[0], size));
    }
  } catch (const ZeException &e) {
    ZeLog::log(e);
    gtfo();
  } catch (...) {
    ZeLOG(Error, "unknown exception");
    gtfo();
  }

  ZeLog::stop();

  return 0;
}
