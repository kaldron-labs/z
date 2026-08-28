//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>

#include <zlib/ZvCf.hh>

#include "ZiTestResidue.hh"

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace ZuTestUtil;

struct FileCf {
  ZtString<>	value;
  int		number = 0;
};

ZfStruct((FileCf, Cf),
  (((value),	(Ctor<0>)),	(String)),
  (((number),	(Ctor<1>)),	(Int32)));

namespace {

Zi::Path g_dir;

Zi::Path path(ZuCSpan name)
{
  return ZiFile::append(g_dir, Zi::Path{name});
}

void write(const Zi::Path &path, ZuCSpan data)
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

ZuCSpan string(const ZfCf::AnyNode *node)
{
  if (!node || !node->has<ZfCf::AnyNode::String>()) return {};
  return node->data<ZfCf::AnyNode::String>();
}

ZtString<> exception(auto &&fn)
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

void files()
{
  ZuTestScope(files);
  auto nestedDir = path("nested");
  ZuCheck(ZiFile::mkdir(nestedDir) == Zi::OK);

  auto rootPath = path("root.cf");
  auto childPath = path("child.cf");
  auto grandPath = ZiFile::append(nestedDir, "grand.cf");
#ifdef _WIN32
  _putenv_s("ZVCF_NESTED", "nested/grand.cf");
#else
  setenv("ZVCF_NESTED", "nested/grand.cf", 1);
#endif

  write(grandPath,
    "grandCur: ${CURDIR}, grandTop: ${TOPDIR}, retained: mapped\n");
  write(childPath,
    "childCur: ${CURDIR},\n"
    "%include(${ZVCF_NESTED})\n");
  write(rootPath,
    "rootTop: ${TOPDIR}, rootCur: ${CURDIR},\n"
    "%include(${CURDIR}/child.cf)\n"
    "afterCur: ${CURDIR},\n"
    "%application(ok)\n");

  unsigned calls = 0;
  auto result = ZvCf::load(rootPath,
    ZfCf::PctFn{[&calls](ZfCf::Scan &, ZuCSpan directive,
	ZuSpan<const ZuCSpan> args, ZfCf::PctExpandFn expand) {
      ++calls;
      return directive == "application" &&
	args.length() == 1 && args[0] == "ok" &&
	expand("application: forwarded");
    }});
  ZuCheck(result.p<0>() == ZiStat{rootPath}.size());
  auto root = ZuMv(result.p<1>());
  ZuCheck(calls == 1);
  ZuCheck(string(root->resolve("rootTop")) == g_dir);
  ZuCheck(string(root->resolve("rootCur")) == g_dir);
  ZuCheck(string(root->resolve("childCur")) == g_dir);
  ZuCheck(string(root->resolve("grandCur")) == nestedDir);
  ZuCheck(string(root->resolve("grandTop")) == g_dir);
  ZuCheck(string(root->resolve("afterCur")) == g_dir);
  ZuCheck(string(root->resolve("application")) == "forwarded");
  ZuCheck(string(root->resolve("retained")) == "mapped");

  ZiFile::remove(grandPath);
  ZiFile::remove(childPath);
  ZiFile::remove(rootPath);
  ZiFile::rmdir(nestedDir);
#ifdef _WIN32
  _putenv_s("ZVCF_NESTED", "");
#else
  unsetenv("ZVCF_NESTED");
#endif
}

void emptyAndErrors()
{
  ZuTestScope(emptyAndErrors);
  auto emptyPath = path("empty.cf");
  write(emptyPath, {});
  auto result = ZvCf::load(emptyPath);
  ZuCheck(result.p<0>() == 0);
  ZuCheck(result.p<1>()->has<ZfCf::AnyNode::Object>());

  auto missingPath = path("missing.cf");
  auto message = exception([&] { ZvCf::load(missingPath); });
  ZuCheck(message && message.find("open(") >= 0);
  ZuCheck(message.find("missing.cf") >= 0);

  auto badPath = path("bad.cf");
  write(badPath, "ok: yes,\nbad");
  message = exception([&] { ZvCf::load(badPath); });
  ZuCheck(message.find("bad.cf") >= 0);
  ZuCheck(message.find(":2:1 syntax error at offset 9") >= 0);

  auto bigPath = path("big.cf");
  {
    ZiFile file;
    ZuCheck(file.open(bigPath, ZiFile::Write | ZiFile::GC) == Zi::OK);
    ZuCheck(file.truncate(1<<20) == Zi::OK);
  }
  message = exception([&] { ZvCf::load(bigPath); });
  ZuCheck(message.find("file too big") >= 0);

#ifndef _WIN32
  auto unreadablePath = path("unreadable.cf");
  write(unreadablePath, "value: hidden");
  ZuCheck(::chmod(unreadablePath, 0000) == 0);
  message = exception([&] { ZvCf::load(unreadablePath); });
  ZuCheck(message.find("mmap(") >= 0);
  ::chmod(unreadablePath, 0644);
  ZiFile::remove(unreadablePath);
#endif

  ZiFile::remove(bigPath);
  ZiFile::remove(badPath);
  ZiFile::remove(emptyPath);
}

void nestedObjectAndDuplicates()
{
  ZuTestScope(nestedObjectAndDuplicates);
  auto rootPath = path("object.cf");
  auto objectPath = path("object-include.cf");
  auto duplicatePath = path("duplicate.cf");
  write(objectPath, "inside: included");
  write(duplicatePath, "value: include,");
  write(rootPath,
    "value: before,\n"
    "%include(duplicate.cf)\n"
    "value: after,\n"
    "object: {\n"
    "%include(object-include.cf)\n"
    "}\n");
  auto result = ZvCf::load(rootPath);
  ZuCheck(string(result.p<1>()->resolve("value")) == "after");
  ZuCheck(string(result.p<1>()->resolve("object.inside")) == "included");

#ifndef _WIN32
  ZuCheck(::chmod(rootPath, 0444) == 0);
  result = ZvCf::load(rootPath);
  ZuCheck(string(result.p<1>()->resolve("value")) == "after");
  ::chmod(rootPath, 0644);
#endif

  ZiFile::remove(duplicatePath);
  ZiFile::remove(objectPath);
  ZiFile::remove(rootPath);
}

void saveLoad()
{
  ZuTestScope(saveLoad);
  auto savePath = path("saved.cf");
  FileCf saved;
  saved.value = "file stream";
  saved.number = 42;
  ZvCf::save(savePath, saved);

  auto result = ZvCf::load(savePath);
  auto loaded = ZfCf::handler<FileCf>(result.p<1>()).ctor();
  ZuCheck(loaded.value == saved.value);
  ZuCheck(loaded.number == saved.number);

  auto updPath = path("updated.cf");
  auto delPath = path("deleted.cf");
  ZvCf::saveUpd(updPath, saved);
  ZvCf::saveDel(delPath, saved);
  ZuCheck(ZiStat{updPath}.exists());
  ZuCheck(ZiStat{delPath}.exists());

  auto badPath = ZiFile::append(path("missing"), "saved.cf");
  auto message = exception([&] { ZvCf::save(badPath, saved); });
  ZuCheck(message.find("open(") >= 0);
  ZuCheck(message.find("saved.cf") >= 0);

  ZiFile::remove(delPath);
  ZiFile::remove(updPath);
  ZiFile::remove(savePath);
}

void includeErrors()
{
  ZuTestScope(includeErrors);
  auto a = path("a.cf");
  auto b = path("b.cf");
  write(a, "%include(b.cf)\n");
  write(b, "%include(a.cf)\n");
  auto message = exception([&] { ZvCf::load(a); });
  ZuCheck(message.find("recursive %include") >= 0);
  ZuCheck(message.find("a.cf") >= 0);
  ZuCheck(message.find("b.cf") >= 0);

  auto malformed = path("malformed.cf");
  write(malformed, "%include()\n");
  message = exception([&] { ZvCf::load(malformed); });
  ZuCheck(message.find("requires one non-empty argument") >= 0);

  auto unknown = path("unknown.cf");
  write(unknown, "%unknown()\n");
  message = exception([&] { ZvCf::load(unknown); });
  ZuCheck(message.find("unknown.cf") >= 0);
  ZuCheck(message.find("syntax error") >= 0);

  auto missing = path("missing-include.cf");
  write(missing, "%include(not-there.cf)\n");
  message = exception([&] { ZvCf::load(missing); });
  ZuCheck(message.find("not-there.cf") >= 0);
  ZuCheck(message.find("include chain:") >= 0);

  auto big = path("big-include.cf");
  {
    ZiFile file;
    ZuCheck(file.open(big, ZiFile::Write | ZiFile::GC) == Zi::OK);
    ZuCheck(file.truncate(1<<20) == Zi::OK);
  }
  auto includeBig = path("include-big.cf");
  write(includeBig, "%include(big-include.cf)\n");
  message = exception([&] { ZvCf::load(includeBig); });
  ZuCheck(message.find("file too big") >= 0);
  ZuCheck(message.find("include chain:") >= 0);

#ifndef _WIN32
  auto unreadable = path("unreadable-include.cf");
  write(unreadable, "value: hidden");
  ZuCheck(::chmod(unreadable, 0000) == 0);
  auto includeUnreadable = path("include-unreadable.cf");
  write(includeUnreadable, "%include(unreadable-include.cf)\n");
  message = exception([&] { ZvCf::load(includeUnreadable); });
  ZuCheck(message.find("mmap(") >= 0);
  ZuCheck(message.find("include chain:") >= 0);
  ::chmod(unreadable, 0644);
  ZiFile::remove(includeUnreadable);
  ZiFile::remove(unreadable);
#endif

  auto appFalse = path("app-false.cf");
  write(appFalse, "%application()\n");
  message = exception([&] {
    ZvCf::load(appFalse, ZfCf::PctFn{
      [](ZfCf::Scan &, ZuCSpan, ZuSpan<const ZuCSpan>,
	  ZfCf::PctExpandFn) { return false; }});
  });
  ZuCheck(message.find("app-false.cf") >= 0);
  ZuCheck(message.find("syntax error") >= 0);

  auto expandFalse = path("expand-false.cf");
  write(expandFalse, "%application()\n");
  message = exception([&] {
    ZvCf::load(expandFalse, ZfCf::PctFn{
      [](ZfCf::Scan &, ZuCSpan, ZuSpan<const ZuCSpan>,
	  ZfCf::PctExpandFn expand) { return expand("broken"); }});
  });
  ZuCheck(message.find("expand-false.cf") >= 0);
  ZuCheck(message.find("syntax error") >= 0);

  {
    ZuTestRepeat(includeSyntax, 4);
    for (auto invalid : {
	ZuCSpan{" %include(a.cf)\n"},
	ZuCSpan{"key: value, %include(a.cf)\n"},
	ZuCSpan{"%include(\na.cf)\n"},
	ZuCSpan{"%include(a.cf),\n"}}) {
      auto syntax = path("include-syntax.cf");
      write(syntax, invalid);
      message = exception([&] { ZvCf::load(syntax); });
      ZuCheck(message.find("syntax error") >= 0);
      ZiFile::remove(syntax);
    }
  }

  ZiFile::remove(expandFalse);
  ZiFile::remove(appFalse);
  ZiFile::remove(includeBig);
  ZiFile::remove(big);
  ZiFile::remove(missing);
  ZiFile::remove(unknown);
  ZiFile::remove(malformed);
  ZiFile::remove(b);
  ZiFile::remove(a);
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZvCfTest");
  g_dir = ZiTestResidue::dir("fixtures");

  ZuTestMain();
  ZuTestCall(files);
  ZuTestCall(emptyAndErrors);
  ZuTestCall(nestedObjectAndDuplicates);
  ZuTestCall(saveLoad);
  ZuTestCall(includeErrors);
}
