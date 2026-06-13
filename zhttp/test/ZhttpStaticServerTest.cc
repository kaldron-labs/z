//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <filesystem>
#include <zlib/ZuTestUtil.hh>

#include "../example/ZhttpStaticServer.hh"
#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;
using namespace ZhttpStatic;

namespace {

using Zhttp::Test::TempDir;

bool writeFile(const std::filesystem::path &path, ZuCSpan body)
{
  ZiFile f;
  if (f.open(path.string().c_str(), ZiFile::Write, 0666) != Zi::OK)
    return false;
  if (f.write(body.data(), body.length()) != Zi::OK) return false;
  f.close();
  return true;
}

void initState(State &state, const std::filesystem::path &root)
{
  state.options.root = root.string().c_str();
  state.mime.init(state.options);
}

RequestData req(ZuCSpan target)
{
  RequestData r;
  r.method = Zhttp::Method::GET;
  r.target = target;
  r.host = "localhost";
  return r;
}

void testPathNormalize()
{
  ZuTestScope(testPathNormalize);
  std::string out, err;
  ZuCHECK(decodeNormalizePath("/a//b/./c", false, out, err) &&
      out == "/a/b/c", "path normalization failed");
  ZuCHECK(!decodeNormalizePath("/../x", false, out, err),
    "literal traversal accepted");
  ZuCHECK(!decodeNormalizePath("/%2e%2e/x", false, out, err),
    "encoded traversal accepted");
  ZuCHECK(!decodeNormalizePath("/bad%", false, out, err),
    "malformed percent escape accepted");
  ZuCHECK(!decodeNormalizePath("/%00", false, out, err),
    "decoded NUL accepted");
  ZuCHECK(!decodeNormalizePath("/.hidden", true, out, err),
    "hidden dotfile accepted with hide-dotfiles");
  ZuCHECK(decodeNormalizePath("/.hidden", false, out, err),
    "visible dotfile rejected without hide-dotfiles");
}

void testMime()
{
  ZuTestScope(testMime);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpStaticMime"), "temporary directory failed");
  auto root = std::filesystem::path(static_cast<const char *>(temp.path));
  State state;
  initState(state, root);
  ZuCHECK(state.mime.lookup(state.options, "index.HTML") == "text/html",
    "case-insensitive builtin MIME lookup failed");
  ZuCHECK(state.mime.lookup(state.options, "file.unknown") ==
      "application/octet-stream", "default MIME lookup failed");
  state.options.defaultMimetype = "text/plain";
  ZuCHECK(state.mime.lookup(state.options, "file.unknown") == "text/plain",
    "custom default MIME lookup failed");
}

void testPlannerFiles()
{
  ZuTestScope(testPlannerFiles);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpStaticFiles"), "temporary directory failed");
  auto root = std::filesystem::path(static_cast<const char *>(temp.path));
  ZuCHECK(writeFile(root / "hello.txt", "hello\n"), "file write failed");
  State state;
  initState(state, root);
  StaticPlanner planner{&state};

  auto r = req("/hello.txt");
  auto p = planner.plan(r);
  ZuCHECK(p.status == 200 && p.file && p.contentLength == 6 &&
      p.contentType == "text/plain", "file GET planning failed");

  r.method = Zhttp::Method::HEAD;
  p = planner.plan(r);
  ZuCHECK(p.status == 200 && p.file && !p.sendBody && p.contentLength == 6,
    "file HEAD planning failed");

  r = req("/missing");
  p = planner.plan(r);
  ZuCHECK(p.status == 404, "missing file did not return 404");
}

void testDirectory()
{
  ZuTestScope(testDirectory);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpStaticDir"), "temporary directory failed");
  auto root = std::filesystem::path(static_cast<const char *>(temp.path));
  std::filesystem::create_directory(root / "dir");
  ZuCHECK(writeFile(root / "dir" / "b.txt", "b"), "b write failed");
  ZuCHECK(writeFile(root / "dir" / "a.txt", "a"), "a write failed");
  State state;
  initState(state, root);
  StaticPlanner planner{&state};

  auto p = planner.plan(req("/dir"));
  ZuCHECK(p.status == 301 && p.location == "/dir/",
    "directory slash redirect failed");
  p = planner.plan(req("/dir/"));
  {
    auto body = str(p.body);
    ZuCHECK(p.status == 200 && p.generated && p.contentType == "text/html" &&
	body.find("a.txt") != std::string::npos &&
	body.find("b.txt") != std::string::npos,
      "directory listing failed");
  }
  ZuCHECK(writeFile(root / "dir" / "index.html", "index"), "index write failed");
  p = planner.plan(req("/dir/"));
  ZuCHECK(p.status == 200 && p.file && p.filePath,
    "directory index did not win over listing");
}

void testPolicy()
{
  ZuTestScope(testPolicy);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpStaticPolicy"), "temporary directory failed");
  auto root = std::filesystem::path(static_cast<const char *>(temp.path));
  ZuCHECK(writeFile(root / "hello.txt", "hello\n"), "file write failed");
  State state;
  initState(state, root);
  state.options.authUser = "u";
  state.options.authPass = "p";
  StaticPlanner planner{&state};

  auto p = planner.plan(req("/hello.txt"));
  ZuCHECK(p.status == 401 && p.wwwAuthenticate,
    "missing auth did not return 401");
  auto r = req("/hello.txt");
  r.authorization = zstr(basicAuthValue(state.options));
  p = planner.plan(r);
  ZuCHECK(p.status == 200, "valid auth did not pass");

  state.options.forwardAll = "https://example.invalid";
  p = planner.plan(req("/hello.txt?q=1"));
  ZuCHECK(p.status == 301 && p.location == "https://example.invalid/hello.txt?q=1",
    "forward-all redirect failed");
}

void testRanges()
{
  ZuTestScope(testRanges);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpStaticRange"), "temporary directory failed");
  auto root = std::filesystem::path(static_cast<const char *>(temp.path));
  ZuCHECK(writeFile(root / "data.bin", "0123456789"), "range file write failed");
  State state;
  initState(state, root);
  StaticPlanner planner{&state};
  auto r = req("/data.bin");
  r.range = "bytes=2-5";
  auto p = planner.plan(r);
  ZuCHECK(p.status == 206 && p.fileOffset == 2 && p.fileLength == 4 &&
      p.contentRange == "bytes 2-5/10", "explicit range failed");
  r.range = "bytes=-3";
  p = planner.plan(r);
  ZuCHECK(p.status == 206 && p.fileOffset == 7 && p.fileLength == 3,
    "suffix range failed");
  r.range = "bytes=99-100";
  p = planner.plan(r);
  ZuCHECK(p.status == 416 && p.contentRange == "bytes */10",
    "unsatisfiable range failed");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPathNormalize);
  ZuTestCall(testMime);
  ZuTestCall(testPlannerFiles);
  ZuTestCall(testDirectory);
  ZuTestCall(testPolicy);
  ZuTestCall(testRanges);
}
