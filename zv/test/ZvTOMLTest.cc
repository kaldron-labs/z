//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>

#include <zlib/ZvTOML.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

struct FileTOML { ZtString<> value; int number = 0; };
ZfStruct((FileTOML, TOML),
  (((value), (Ctor<0>, Keys<0>)), (String)),
  (((number), (Ctor<1>, Mutable)), (Int32)));

struct FileItem { ZtString<> name; };
ZfStruct((FileItem, TOML),
  (((name), (Ctor<0>)), (String)));
struct FileItems : public ZtArray<FileItem> {
  using ZtArray<FileItem>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(FileItems *);
};
struct FileGroup { ZtString<> name; FileItems items; };
ZfStruct((FileGroup, TOML),
  (((name), (Ctor<0>)), (String)),
  (((items), (Ctor<1>, TOML::Tables)), (UDT)));
struct FileGroups : public ZtArray<FileGroup> {
  using ZtArray<FileGroup>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(FileGroups *);
};
struct FileCatalog { FileGroups groups; };
ZfStruct((FileCatalog, TOML),
  (((groups), (Ctor<0>, TOML::Tables)), (UDT)));

static Zi::Path g_dir;

static Zi::Path path(ZuCSpan name)
{
  return ZiFile::append(g_dir, Zi::Path{name});
}

static void write(const Zi::Path &path_, ZuCSpan data)
{
  ZiFile file;
  if (file.open(path_, ZiFile::Write | ZiFile::GC) != Zi::OK ||
      file.write(data.data(), data.length()) != Zi::OK) Zm::exit(1);
}

static ZtString<> exception(auto &&fn)
{
  try { fn(); }
  catch (const ZeException &e) { ZtString<> text; text << e; return text; }
  return {};
}

static void files()
{
  ZuTestScope(files);
  auto rootPath = path("root.toml");
  write(rootPath,
    "value = \"mapped\"\n"
    "[nested]\nitems = [\"one\", \"two\"]\n");
  auto result = ZvTOML::load(rootPath);
  ZuCheck(result.p<0>() == ZiStat{rootPath}.size());
  auto root = ZuMv(result.p<1>());
  ZuCheck(root->resolve("value")->data<ZfTOML::AnyNode::String>() == "mapped");
  ZuCheck(root->resolve("nested.items[1]")->data<ZfTOML::AnyNode::String>() ==
    "two");

  auto nestedPath = path("nested.toml");
  write(nestedPath,
    "[[groups]]\nname = \"tools\"\n"
    "[[groups.items]]\nname = \"hammer\"\n"
    "[[groups.items]]\nname = \"nail\"\n");
  auto nestedResult = ZvTOML::load(nestedPath);
  auto catalog = ZfTOML::handler<FileCatalog>(nestedResult.p<1>()).ctor();
  ZuCheck(catalog.groups.length() == 1 &&
    catalog.groups[0].items.length() == 2 &&
    catalog.groups[0].items[1].name == "nail");
#ifndef _WIN32
  ZuCheck(::chmod(rootPath, 0444) == 0);
  result = ZvTOML::load(rootPath);
  ZuCheck(result.p<1>()->resolve("value")->
    data<ZfTOML::AnyNode::String>() == "mapped");
  ::chmod(rootPath, 0644);
#endif
  ZiFile::remove(rootPath);
  ZiFile::remove(nestedPath);
}

static void errorsSave()
{
  ZuTestScope(errorsSave);
  auto emptyPath = path("empty.toml");
  write(emptyPath, {});
  auto result = ZvTOML::load(emptyPath);
  ZuCheck(result.p<0>() == 0);
  ZuCheck(result.p<1>()->has<ZfTOML::AnyNode::Object>());

  auto missingPath = path("missing.toml");
  auto message = exception([missingPath] { ZvTOML::load(missingPath); });
  ZuCheck(message.find("open(") >= 0);

  auto badPath = path("bad.toml");
  write(badPath, "ok = 1\nbad = [1,,2]\n");
  message = exception([badPath] { ZvTOML::load(badPath); });
  ZuCheck(message.find("bad.toml\":2:10") >= 0);
  ZuCheck(message.find("offset 16") >= 0);
  ZuCheck(message.find("invalid value") >= 0);

  auto bigPath = path("big.toml");
  { ZiFile file; ZuCheck(file.open(bigPath, ZiFile::Write | ZiFile::GC) == Zi::OK);
    ZuCheck(file.truncate(1<<20) == Zi::OK); }
  message = exception([bigPath] { ZvTOML::load(bigPath); });
  ZuCheck(message.find("file too big") >= 0);

#ifndef _WIN32
  auto unreadablePath = path("unreadable.toml");
  write(unreadablePath, "value = \"hidden\"\n");
  ZuCheck(::chmod(unreadablePath, 0000) == 0);
  message = exception([unreadablePath] { ZvTOML::load(unreadablePath); });
  ZuCheck(message.find("mmap(") >= 0);
  ::chmod(unreadablePath, 0644);
  ZiFile::remove(unreadablePath);
#endif

  FileTOML saved{"file stream", 42};
  auto savePath = path("saved.toml");
  ZvTOML::save(savePath, saved);
  result = ZvTOML::load(savePath);
  auto loaded = ZfTOML::handler<FileTOML>(result.p<1>()).ctor();
  ZuCheck(loaded.value == saved.value && loaded.number == saved.number);

  auto updPath = path("updated.toml"), delPath = path("deleted.toml");
  ZvTOML::saveUpd(updPath, saved);
  ZvTOML::saveDel(delPath, saved);
  result = ZvTOML::load(updPath);
  auto updated = ZfTOML::handler<FileTOML>(result.p<1>()).ctor();
  ZuCheck(updated.value == saved.value && updated.number == saved.number);
  result = ZvTOML::load(delPath);
  auto deleted = ZfTOML::handler<FileTOML>(result.p<1>()).ctor();
  ZuCheck(deleted.value == saved.value && ZuNull(deleted.number));

  auto badSave = ZiFile::append(path("missing"), "saved.toml");
  message = exception([badSave, &saved] { ZvTOML::save(badSave, saved); });
  ZuCheck(message.find("open(") >= 0);
  ZuCheck(message.find("saved.toml") >= 0);

  ZiFile::remove(delPath);
  ZiFile::remove(updPath);
  ZiFile::remove(savePath);
  ZiFile::remove(bigPath);
  ZiFile::remove(badPath);
  ZiFile::remove(emptyPath);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZvTOMLTest");
  g_dir = ZiTestResidue::dir("fixtures");
  ZuTestMain();
  ZuTestCall(files);
  ZuTestCall(errorsSave);
}
