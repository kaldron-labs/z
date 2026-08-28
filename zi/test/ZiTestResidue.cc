//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// test filesystem and shared-memory residue lifecycle

#include "ZiTestResidue.hh"

#include <stdlib.h>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuTest.hh>

#include <zlib/ZmGuard.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiGlob.hh>

namespace ZiTestResidue {

struct State {
  enum { Uninit, Active, Final };

  using Guard = ZmGuard<ZmLock>;
  ZuDerive(Names, (
    ZtArray<Zi::Name, ZtArrayHeapID<"ZiTestResidue.Names">>));

  ZmLock		lock;
    Zi::Path		  rootDir;
    Zi::Path		  basePath;
    Zi::Name		  testName;
    Paths		  files;
    Paths		  dirs;
    Names		  shmBases;
    unsigned		  counter = 0;
    int			  phase = Uninit;
};

static State &state_()
{
  return *ZmSingleton<State>::instance();
}

static bool separator_(char c)
{
#ifdef _WIN32
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

template <typename S>
static bool hasSeparator_(const S &s)
{
  unsigned n = s.length();
  for (unsigned i = 0; i < n; ++i)
    if (separator_(s[i])) return true;
  return false;
}

template <typename S>
static bool hasDotDot_(const S &s)
{
  unsigned n = s.length();
  for (unsigned i = 1; i < n; ++i)
    if (s[i - 1] == '.' && s[i] == '.') return true;
  return false;
}

static void validateName_(const Zi::Name &name)
{
  ZiAssert(name.length(), "ZiTestResidue", (), "empty name", ::abort());
  ZiAssert(!hasSeparator_(name), "ZiTestResidue", (),
      "path separator in name", ::abort());
  ZiAssert(!hasDotDot_(name), "ZiTestResidue", (),
      "'..' in name", ::abort());
}

static void mkdir_(const Zi::Path &path)
{
  ZiStat stat{path};
  if (stat.isdir()) return;
  ZiAssert(!stat.exists(), "ZiTestResidue", (path),
      "path exists and is not a directory: " << path, ::abort());
  ZiAssert(ZiFile::mkdir(path) == Zi::OK, "ZiTestResidue", (path),
      "mkdir failed: " << path, ::abort());
}

static bool prefix_(const Zi::Path &path, const Zi::Path &prefix)
{
  unsigned n = prefix.length();
  if (path.length() <= n) return false;
  for (unsigned i = 0; i < n; ++i)
    if (path[i] != prefix[i]) return false;
  return true;
}

static bool child_(const Zi::Path &path, const Zi::Path &dir)
{
  unsigned n = dir.length();
  if (path.length() <= n) return false;
  for (unsigned i = 0; i < n; ++i)
    if (path[i] != dir[i]) return false;
  return separator_(path[n]);
}

static void validatePath_(const State &state, const Zi::Path &path)
{
  ZiAssert(prefix_(path, state.basePath), "ZiTestResidue", (path),
      "path outside test namespace: " << path, ::abort());
  unsigned n = state.basePath.length();
  for (unsigned i = n; i < path.length(); ++i)
    ZiAssert(!separator_(path[i]), "ZiTestResidue", (path),
        "nested exact path: " << path, ::abort());
}

static void validateDir_(const State &state, const Zi::Path &dir)
{
  validatePath_(state, dir);
  bool found = false;
  for (const auto &path : state.dirs)
    if (path == dir) found = true;
  ZiAssert(found, "ZiTestResidue", (dir),
      "unowned directory: " << dir, ::abort());
}

static void registerFile_(State &state, const Zi::Path &path)
{
  validatePath_(state, path);
  for (const auto &file : state.files)
    ZiAssert(file != path, "ZiTestResidue", (path),
        "duplicate file: " << path, ::abort());
  for (const auto &dir : state.dirs)
    ZiAssert(path != dir && !child_(path, dir), "ZiTestResidue", (path),
        "file overlaps directory: " << path, ::abort());
  state.files.push(path);
}

static void registerDir_(State &state, const Zi::Path &path)
{
  validatePath_(state, path);
  for (const auto &file : state.files)
    ZiAssert(file != path && !child_(file, path), "ZiTestResidue", (path),
        "directory overlaps file: " << path, ::abort());
  for (const auto &dir : state.dirs) {
    ZiAssert(dir != path, "ZiTestResidue", (path),
        "duplicate directory: " << path, ::abort());
    ZiAssert(!child_(dir, path), "ZiTestResidue", (path),
        "nested directory: " << path, ::abort());
    ZiAssert(!child_(path, dir), "ZiTestResidue", (path),
        "nested directory: " << path, ::abort());
  }
  state.dirs.push(path);
}

static void cleanupShm_(const Zi::Name &base)
{
#ifndef _WIN32
  Zi::Path ctrl;
  ctrl << '/' << base << ".ctrl";
  ::shm_unlink(ctrl);
  Zi::Path data;
  data << '/' << base << ".data";
  ::shm_unlink(data);
#else
  (void)base;
#endif
}

void init(const char *testName)
{
  State &state = state_();
  State::Guard guard(state.lock);
  if (state.phase != State::Uninit) {
    ZiAssert(state.testName == testName, "ZiTestResidue", (testName),
        "different repeated test name: " << testName, ::abort());
    return;
  }

  Zi::Name name{testName};
  validateName_(name);
  ZiAssert(name != "." && name != "..", "ZiTestResidue", (name),
      "invalid test name: " << name, ::abort());

  const char *env = ::getenv("ZI_LOGDIR");
  Zi::Path root = env && *env ? Zi::Path{env} : ZiFile::cwd();
  if (!ZiFile::absolute(root)) root = ZiFile::append(ZiFile::cwd(), root);
  mkdir_(root);
  state.rootDir = ZiFile::canonical(root);
  ZiAssert(state.rootDir.length(), "ZiTestResidue", (),
      "cannot canonicalize residue root", ::abort());
  state.testName = name;
  Zi::Path leaf;
  leaf << state.testName << '.';
  state.basePath = ZiFile::append(state.rootDir, leaf);
  state.phase = State::Active;

  ::atexit(&cleanup);
  ZuTestMgr::finalFn(&final);
}

Zi::Path path(const Zi::Name &name)
{
  validateName_(name);
  State &state = state_();
  State::Guard guard(state.lock);
  ZiAssert(state.phase == State::Active, "ZiTestResidue", (),
      "residue manager is not active", ::abort());
  Zi::Path path;
  path << state.basePath << name;
  return path;
}

Zi::Path file(const Zi::Name &name)
{
  Zi::Path result = path(name);
  ZiFile::age(result, Age);
  add(result);
  return result;
}

Zi::Path dir(const Zi::Name &name)
{
  Zi::Path result = path(name);
  ZiFile::ageTree(result, Age);
  ZiAssert(ZiFile::mkdir(result) == Zi::OK, "ZiTestResidue", (result),
      "mkdir failed: " << result, ::abort());
  State &state = state_();
  State::Guard guard(state.lock);
  ZiAssert(state.phase == State::Active, "ZiTestResidue", (),
      "residue manager is not active", ::abort());
  registerDir_(state, result);
  return result;
}

void add(const Zi::Path &path)
{
  State &state = state_();
  State::Guard guard(state.lock);
  ZiAssert(state.phase == State::Active, "ZiTestResidue", (),
      "residue manager is not active", ::abort());
  registerFile_(state, path);
}

Paths glob(const Zi::Path &dir, const Zi::Name &prefix)
{
  validateName_(prefix);
  {
    State &state = state_();
    State::Guard guard(state.lock);
    ZiAssert(state.phase == State::Active, "ZiTestResidue", (),
        "residue manager is not active", ::abort());
    validateDir_(state, dir);
  }

  Paths paths;
  ZiGlob glob;
  ZiAssert(glob.init(ZiFile::append(dir, prefix)), "ZiTestResidue",
      (dir, prefix), "glob failed: " << dir << '/' << prefix, ::abort());
  while (auto entry = glob.iterate(true, false)) {
    ZiAssert(!entry->isdir, "ZiTestResidue", (dir),
        "glob matched directory in: " << dir, ::abort());
    paths.push(ZiFile::append(dir, entry->name));
  }
  glob.final();
  return paths;
}

void del(const Zi::Path &dir, const Zi::Name &prefix)
{
  Paths paths = glob(dir, prefix);
  for (const auto &path : paths)
    ZiAssert(ZiFile::remove(path) == Zi::OK, "ZiTestResidue", (path),
        "remove failed: " << path, ::abort());
}

Zi::Name uniqueName(const char *tag)
{
  State &state = state_();
  State::Guard guard(state.lock);
  ZiAssert(state.phase == State::Active, "ZiTestResidue", (),
      "residue manager is not active", ::abort());
  Zi::Name name;
  name << "ZiTest." << state.testName << '.'
    << ZuBoxed(Zm::getPID()) << '.' << ZuBox<unsigned>(++state.counter);
  if (tag && *tag) name << '.' << tag;
  return name;
}

void addShm(const Zi::Name &name)
{
  State &state = state_();
  State::Guard guard(state.lock);
  ZiAssert(state.phase == State::Active, "ZiTestResidue", (),
      "residue manager is not active", ::abort());
  for (const auto &base : state.shmBases)
    ZiAssert(base != name, "ZiTestResidue", (name),
        "duplicate shared-memory name: " << name, ::abort());
  state.shmBases.push(name);
}

void final(bool passed)
{
  Paths files;
  Paths dirs;
  State::Names shmBases;
  {
    State &state = state_();
    State::Guard guard(state.lock);
    if (state.phase != State::Active) return;
    files = ZuMv(state.files);
    dirs = ZuMv(state.dirs);
    shmBases = ZuMv(state.shmBases);
    state.phase = State::Final;
  }

  for (const auto &base : shmBases) cleanupShm_(base);
  if (!passed) return;
  for (const auto &path : files)
    ZiAssert(ZiFile::remove(path) == Zi::OK, "ZiTestResidue", (path),
        "remove failed: " << path, ::abort());
  for (const auto &path : dirs)
    ZiAssert(ZiFile::removeTree(path) == Zi::OK, "ZiTestResidue", (path),
        "remove tree failed: " << path, ::abort());
}

void cleanup()
{
  final(false);
}

} // ZiTestResidue
