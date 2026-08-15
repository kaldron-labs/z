//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>

#include <zlib/ZvYAML.hh>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace ZuTestUtil;

struct FileYAML {
  ZtString<>	value;
  int		number = 0;
};

ZfStruct((FileYAML, YAML),
  (((value),	(Ctor<0>)),	(String)),
  (((number),	(Ctor<1>)),	(Int32)));

static Zi::Path g_dir;

static Zi::Path path(ZuCSpan name)
{
  return ZiFile::append(g_dir, Zi::Path{name});
}

static void write(const Zi::Path &path, ZuCSpan data)
{
  ZiFile file;
  if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) {
    log_("open failed: ", path, ": ", file.error());
    Zm::exit(1);
  }
  if (file.write(data.data(), data.length()) != Zi::OK) {
    log_("write failed: ", path, ": ", file.error());
    Zm::exit(1);
  }
}

static ZuCSpan string(const ZfYAML::AnyNode *node)
{
  if (!node || !node->has<ZfYAML::AnyNode::String>()) return {};
  return node->data<ZfYAML::AnyNode::String>();
}

static ZtString<> exception(auto &&fn)
{
  try {
    fn();
  } catch (const ZeException &e) {
    ZtString<> text;
    text << e;
    return text;
  }
  return {};
}

static void files()
{
  ZuTestScope(files);
  auto rootPath = path("root.yaml");
  write(rootPath,
    "value: mapped\n"
    "nested:\n"
    "  items: [one, two]\n");
  auto result = ZvYAML::load(rootPath);
  ZuCheck(result.p<0>() == ZiStat{rootPath}.size());
  auto root = ZuMv(result.p<1>());
  ZuCheck(string(root->resolve("value")) == "mapped");
  ZuCheck(string(root->resolve("nested.items[1]")) == "two");

#ifndef _WIN32
  ZuCheck(::chmod(rootPath, 0444) == 0);
  result = ZvYAML::load(rootPath);
  ZuCheck(string(result.p<1>()->resolve("value")) == "mapped");
  ::chmod(rootPath, 0644);
#endif
  ZiFile::remove(rootPath);
}

static void transforms()
{
  ZuTestScope(transforms);
  auto rootPath = path("schema.yaml");
  write(rootPath,
    "components:\n"
    "  schemas:\n"
    "    Base: {type: object, required: [id]}\n"
    "schema:\n"
    "  allOf:\n"
    "    - {$ref: '#/components/schemas/Base'}\n"
    "    - {properties: {id: {type: string}}}\n");
  auto result = ZvYAML::load(rootPath);
  auto root = ZfYAML::flatten(ZfYAML::resolve(ZuMv(result.p<1>())));
  ZuCheck(string(root->resolve("schema.type")) == "object");
  ZuCheck(string(root->resolve("schema.required[0]")) == "id");
  auto id = root->resolve("schema.properties.id");
  ZuCheck(string(id->resolve("type")) == "string");
  ZeString nodePath;
  id->path(nodePath);
  ZuCheck(nodePath == "schema.properties.id");
  ZiFile::remove(rootPath);
}

static void emptyErrorsSave()
{
  ZuTestScope(emptyErrorsSave);
  auto emptyPath = path("empty.yaml");
  write(emptyPath, {});
  auto result = ZvYAML::load(emptyPath);
  ZuCheck(result.p<0>() == 0);
  ZuCheck(result.p<1>()->has<ZfYAML::AnyNode::Object>());

  auto missingPath = path("missing.yaml");
  auto message = exception([missingPath] { ZvYAML::load(missingPath); });
  ZuCheck(message.find("open(") >= 0);
  ZuCheck(message.find("missing.yaml") >= 0);

  auto badPath = path("bad.yaml");
  write(badPath, "ok: yes\nbad: [1,, 2]\n");
  message = exception([badPath] { ZvYAML::load(badPath); });
  ZuCheck(message.find("bad.yaml\":2:9 syntax error at offset 16") >= 0);

  auto bigPath = path("big.yaml");
  {
    ZiFile file;
    ZuCheck(file.open(bigPath, ZiFile::Write | ZiFile::GC) == Zi::OK);
    ZuCheck(file.truncate(1<<20) == Zi::OK);
  }
  message = exception([bigPath] { ZvYAML::load(bigPath); });
  ZuCheck(message.find("file too big") >= 0);

  FileYAML saved;
  saved.value = "file stream";
  saved.number = 42;
  auto savePath = path("saved.yaml");
  ZvYAML::save(savePath, saved);
  result = ZvYAML::load(savePath);
  auto loaded = ZfYAML::handler<FileYAML>(result.p<1>()).ctor();
  ZuCheck(loaded.value == saved.value);
  ZuCheck(loaded.number == saved.number);

  auto updPath = path("updated.yaml");
  auto delPath = path("deleted.yaml");
  ZvYAML::saveUpd(updPath, saved);
  ZvYAML::saveDel(delPath, saved);
  ZuCheck(ZiStat{updPath}.exists());
  ZuCheck(ZiStat{delPath}.exists());

  auto badSave = ZiFile::append(path("missing"), "saved.yaml");
  message = exception([badSave, &saved] { ZvYAML::save(badSave, saved); });
  ZuCheck(message.find("open(") >= 0);

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
  g_dir << ZiFile::cwd() << "/ZvYAMLTest." << ZuBoxed(Zm::getPID());
  ZiFile::rmdir(g_dir);
  if (ZiFile::mkdir(g_dir) != Zi::OK) {
    log_("mkdir failed: ", g_dir);
    return 1;
  }

  ZuTestMain();
  ZuTestCall(files);
  ZuTestCall(transforms);
  ZuTestCall(emptyErrorsSave);

  ZiFile::rmdir(g_dir);
}
