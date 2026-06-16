//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <cstdint>
#include <zlib/ZuTestUtil.hh>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "../example/Zhttpd.hh"
#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;
using namespace Zhttpd;

namespace {

using Zhttp::Test::TempDir;

Zi::Path tempRoot(const TempDir &temp)
{
  return static_cast<const char *>(temp.path);
}

bool writeFile(const Zi::Path &path, ZuCSpan body)
{
  ZiFile f;
  if (f.open(path, ZiFile::Write, 0666) != Zi::OK)
    return false;
  if (f.write(body.data(), body.length()) != Zi::OK) return false;
  f.close();
  return true;
}

bool initState(State &state, const Zi::Path &root)
{
  state.options.root = root;
  HdrString error;
  if (!initFileState(state, error)) return false;
  state.mime.init(state.options);
  return true;
}

RequestData req(ZuCSpan target)
{
  RequestData r;
  r.method = Zhttp::Method::GET;
  r.target = target;
  r.host = "localhost";
  return r;
}

bool load(Options &options, std::initializer_list<const char *> args)
{
  ZtArray<char *> argv;
  for (auto arg : args) argv.push(const_cast<char *>(arg));
  bool help = false;
  return loadOptions(options, int(argv.length()), argv.data(), help) && !help;
}

void testPathNormalize()
{
  ZuTestScope(testPathNormalize);
  HdrString out, err;
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

void testCLI()
{
  ZuTestScope(testCLI);
  {
    Options options;
    ZuCHECK(load(options, {"zhttpd", "/tmp/www", "--port", "8081"}),
      "--port value failed");
    ZuCheck(options.root == "/tmp/www");
    ZuCheck(options.port == 8081);
  }
  {
    Options options;
    ZuCHECK(load(options, {"zhttpd", "/tmp/www", "--port=8082"}),
      "--port=value failed");
    ZuCheck(options.port == 8082);
  }
  {
    Options options;
    ZuCHECK(!load(options, {"zhttpd"}), "missing root accepted");
    ZuCHECK(!load(options, {"zhttpd", "/a", "/b"}),
      "duplicate root accepted");
    ZuCHECK(!load(options, {"zhttpd", "/tmp/www", "--port=70000"}),
      "overflow port accepted");
  }
  {
    Options options;
    ZuCHECK(load(options, {
	"zhttpd", "/tmp/www",
	"--forward", "a.example,https://a.invalid",
	"--forward=b.example,https://b.invalid"}),
      "repeated forward failed");
    ZuCheck(options.forwards.length() == 2);
    ZuCheck(options.forwards[0].host == "a.example");
    ZuCheck(options.forwards[1].url == "https://b.invalid");
  }
  {
    Options options;
    ZuCHECK(load(options, {"zhttpd", "/tmp/www", "--auth", "u:p"}),
      "auth failed");
    ZuCheck(options.authUser == "u");
    ZuCheck(options.authPass == "p");
    Options bad;
    ZuCHECK(!load(bad, {"zhttpd", "/tmp/www", "--auth", "missingcolon"}),
      "invalid auth accepted");
  }
  {
    Options options;
    ZuCHECK(load(options, {"zhttpd", "/tmp/www", "--https",
	"--cert", "c", "--key", "k"}),
      "https default load failed");
    ZuCheck(!options.http);
    ZuCheck(options.https);
    Options both;
    ZuCHECK(load(both, {"zhttpd", "/tmp/www", "--http", "--https",
	"--cert", "c", "--key", "k"}),
      "explicit http+https load failed");
    ZuCheck(both.http);
    ZuCheck(both.https);
  }
}

void testMime()
{
  ZuTestScope(testMime);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpdMime"), "temporary directory failed");
  Zi::Path root = tempRoot(temp);
  State state;
  ZuCHECK(initState(state, root), "state initialization failed");
  ZuCHECK(state.mime.lookup(state.options, "index.HTML") == "text/html",
    "case-insensitive builtin MIME lookup failed");
  ZuCHECK(state.mime.lookup(state.options, "file.unknown") ==
      "application/octet-stream", "default MIME lookup failed");
  state.options.defaultMimetype = "text/plain";
  ZuCHECK(state.mime.lookup(state.options, "file.unknown") == "text/plain",
    "custom default MIME lookup failed");
  auto types = ZiFile::append(root, "mime.types");
  ZuCHECK(writeFile(types, "text/custom html foo\n"),
    "mimetypes write failed");
  State custom;
  custom.options.root = root;
  custom.options.mimetypes = types;
  HdrString error;
  ZuCHECK(initFileState(custom, error),
    "custom state initialization failed");
  custom.mime.init(custom.options);
  ZuCHECK(custom.mime.lookup(custom.options, "index.html") == "text/custom",
    "custom MIME override failed");
  ZuCHECK(custom.mime.lookup(custom.options, "file.foo") == "text/custom",
    "custom MIME lookup failed");
}

void testPlannerFiles()
{
  ZuTestScope(testPlannerFiles);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpdFiles"), "temporary directory failed");
  Zi::Path root = tempRoot(temp);
  ZuCHECK(writeFile(ZiFile::append(root, "hello.txt"), "hello\n"),
    "file write failed");
  State state;
  ZuCHECK(initState(state, root), "state initialization failed");
  StaticPlanner planner{&state};

  auto r = req("/hello.txt");
  auto p = planner.plan(r);
  ZuCHECK(p.status == 200 && p.file && p.contentLength == 6 &&
      p.contentType == "text/plain" && p.fileHandle,
    "file GET planning failed");

  r.method = Zhttp::Method::HEAD;
  p = planner.plan(r);
  ZuCHECK(p.status == 200 && p.file && !p.sendBody && p.contentLength == 6,
    "file HEAD planning failed");

  r = req("/missing");
  p = planner.plan(r);
  ZuCHECK(p.status == 404, "missing file did not return 404");
}

void testSymlinkRejected()
{
  ZuTestScope(testSymlinkRejected);
#ifndef _WIN32
  TempDir temp;
  ZuCHECK(temp.init("ZhttpdSymlink"), "temporary directory failed");
  Zi::Path root = tempRoot(temp);
  auto outside = ZiFile::append(ZiFile::dirname(root), "outside.txt");
  ZuCHECK(writeFile(outside, "outside\n"), "outside write failed");
  auto link = ZiFile::append(root, "link.txt");
  ZuCHECK(::symlink(outside.ndata(), link.ndata()) == 0,
    "symlink creation failed");
  State state;
  ZuCHECK(initState(state, root), "state initialization failed");
  StaticPlanner planner{&state};
  auto p = planner.plan(req("/link.txt"));
  ZuCHECK((p.status == 403 || p.status == 404) && !p.file,
    "final symlink was served");
  ZiFile::remove(outside);
#endif
}

void testDirectory()
{
  ZuTestScope(testDirectory);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpdDir"), "temporary directory failed");
  Zi::Path root = tempRoot(temp);
  auto dir = ZiFile::append(root, "dir");
  ZuCHECK(ZiFile::mkdir(dir) == Zi::OK, "directory creation failed");
  ZuCHECK(writeFile(ZiFile::append(dir, "b.txt"), "b"), "b write failed");
  ZuCHECK(writeFile(ZiFile::append(dir, "a.txt"), "a"), "a write failed");
  State state;
  ZuCHECK(initState(state, root), "state initialization failed");
  StaticPlanner planner{&state};

  auto p = planner.plan(req("/dir"));
  ZuCHECK(p.status == 301 && p.location == "/dir/",
    "directory slash redirect failed");
  p = planner.plan(req("/dir?q=1"));
  ZuCHECK(p.status == 301 && p.location == "/dir/?q=1",
    "directory slash redirect query failed");
  p = planner.plan(req("/dir/"));
  {
    ZuCHECK(p.status == 200 && p.generated && p.contentType == "text/html" &&
	!!strstr(p.body.ndata(), "a.txt") &&
	!!strstr(p.body.ndata(), "b.txt"),
      "directory listing failed");
  }
  ZuCHECK(writeFile(ZiFile::append(dir, "index.html"), "index"),
    "index write failed");
  p = planner.plan(req("/dir/"));
  ZuCHECK(p.status == 200 && p.file && p.filePath,
    "directory index did not win over listing");
}

void testIfModifiedSince()
{
  ZuTestScope(testIfModifiedSince);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpdIMS"), "temporary directory failed");
  Zi::Path root = tempRoot(temp);
  ZuCHECK(writeFile(ZiFile::append(root, "hello.txt"), "hello\n"),
    "file write failed");
  State state;
  ZuCHECK(initState(state, root), "state initialization failed");
  StaticPlanner planner{&state};
  auto r = req("/hello.txt");
  r.ifModifiedSince = httpDate(time(nullptr) + 60);
  auto p = planner.plan(r);
  ZuCHECK(p.status == 304 && !p.sendBody && p.contentLength == 0,
    "If-Modified-Since did not return 304");
}

void testPolicy()
{
  ZuTestScope(testPolicy);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpdPolicy"), "temporary directory failed");
  Zi::Path root = tempRoot(temp);
  ZuCHECK(writeFile(ZiFile::append(root, "hello.txt"), "hello\n"),
    "file write failed");
  State state;
  ZuCHECK(initState(state, root), "state initialization failed");
  state.options.authUser = "u";
  state.options.authPass = "p";
  StaticPlanner planner{&state};

  auto p = planner.plan(req("/hello.txt"));
  ZuCHECK(p.status == 401 && p.wwwAuthenticate,
    "missing auth did not return 401");
  auto r = req("/hello.txt");
  r.authorization = basicAuthValue(state.options);
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
  ZuCHECK(temp.init("ZhttpdRange"), "temporary directory failed");
  Zi::Path root = tempRoot(temp);
  ZuCHECK(writeFile(ZiFile::append(root, "data.bin"), "0123456789"),
    "range file write failed");
  State state;
  ZuCHECK(initState(state, root), "state initialization failed");
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

void testSingleFile()
{
  ZuTestScope(testSingleFile);
  TempDir temp;
  ZuCHECK(temp.init("ZhttpdSingle"), "temporary directory failed");
  Zi::Path root = tempRoot(temp);
  auto file = ZiFile::append(root, "only.txt");
  ZuCHECK(writeFile(file, "only"), "single file write failed");
  State state;
  state.options.root = file;
  state.options.singleFile = true;
  HdrString error;
  ZuCHECK(initFileState(state, error), "single file state failed: ", error);
  state.mime.init(state.options);
  StaticPlanner planner{&state};
  auto p = planner.plan(req("/"));
  ZuCHECK(p.status == 200 && p.file && p.contentLength == 4,
    "single-file / failed");
  p = planner.plan(req("/only.txt"));
  ZuCHECK(p.status == 200 && p.file, "single-file leaf failed");
  p = planner.plan(req("/other.txt"));
  ZuCHECK(p.status == 404, "single-file extra path accepted");
}

void testFileChunks()
{
  ZuTestScope(testFileChunks);
  uint64_t len = uint64_t(UINT32_MAX) + FileChunk + 7;
  uint64_t total = 0;
  unsigned chunks = 0;
  unsigned max = 0;
  fileChunks(len, [&](unsigned n) {
    total += n;
    if (n > max) max = n;
    ++chunks;
  });
  ZuCHECK(total == len, "chunk total mismatch");
  ZuCHECK(max == FileChunk, "chunk max mismatch: ", max);
  ZuCHECK(chunks > 1, "large length was not chunked");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPathNormalize);
  ZuTestCall(testCLI);
  ZuTestCall(testMime);
  ZuTestCall(testPlannerFiles);
  ZuTestCall(testSymlinkRejected);
  ZuTestCall(testDirectory);
  ZuTestCall(testPolicy);
  ZuTestCall(testIfModifiedSince);
  ZuTestCall(testRanges);
  ZuTestCall(testSingleFile);
  ZuTestCall(testFileChunks);
}
