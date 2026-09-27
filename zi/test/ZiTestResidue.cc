//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// test filesystem and shared-memory residue lifecycle

#include "ZiTestResidue.hh"

#include <stdlib.h>
#include <errno.h>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZuTest.hh>

#include <zlib/ZmGuard.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtPlatform.hh>

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
    Paths		  tmpFiles;
    Paths		  tmpDirs;
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

static bool hasSeparator_(ZuCSpan s)
{
  unsigned n = s.length();
  for (unsigned i = 0; i < n; ++i)
    if (separator_(s[i])) return true;
  return false;
}

static bool hasDotDot_(ZuCSpan s)
{
  unsigned n = s.length();
  for (unsigned i = 1; i < n; ++i)
    if (s[i - 1] == '.' && s[i] == '.') return true;
  return false;
}

static void validateName_(ZuCSpan name)
{
  ZiAssert(name.length(), "ZiTestResidue", (), "empty name", ::abort());
  auto alnum = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9');
  };
  ZiAssert(alnum(name[0]), "ZiTestResidue", (name),
      "name must start with a letter or digit", ::abort());
  for (unsigned i = 1; i < name.length(); ++i)
    ZiAssert(alnum(name[i]) || name[i] == '.' || name[i] == '_' ||
        name[i] == '-', "ZiTestResidue", (name),
        "invalid character in name", ::abort());
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
  unsigned pathLen = path.length();
  for (unsigned i = n; i < pathLen; ++i)
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

struct TmpEntry {
  Zi::Path path;
  ZuTime mtime;
  bool dir;
};

static void ageTmp_(const Zi::Name &testName)
{
  Zi::Path base = ZiFile::tmpDir();
  Zi::Path prefix;
  prefix << "ZiTest." << testName << '.';
#ifndef _WIN32
  int lock = ::open(base, O_RDONLY | O_DIRECTORY);
  ZiAssert(lock >= 0 && !::flock(lock, LOCK_EX), "ZiTestResidue", (base),
      "cannot lock temporary directory: " << base, ::abort());
#else
  Zi::Path lockPath = ZiFile::append(base, ".ZiTestResidue.lock");
  HANDLE lock = ::CreateFileW(lockPath.data(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  OVERLAPPED overlap{};
  ZiAssert(lock != INVALID_HANDLE_VALUE &&
      ::LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap),
      "ZiTestResidue", (lockPath), "cannot lock temporary directory", ::abort());
#endif
  ZtArray<TmpEntry> failed;
  ZiGlob glob;
  ZiAssert(glob.init(ZiFile::append(base, prefix)), "ZiTestResidue", (base),
      "cannot scan temporary directory: " << base, ::abort());
  while (auto entry = glob.iterate(true, false)) {
    const Zi::Path &name = entry->name;
    unsigned n = prefix.length(), i = n;
    unsigned pid = 0;
    while (i < name.length() && name[i] >= '0' && name[i] <= '9') {
      pid = (pid * 10) + unsigned(name[i++] - '0');
    }
    if (i == n || i == name.length() || name[i] != '.') continue;
    Zi::Path path = ZiFile::append(base, name);
    ZiStat stat{path};
    if (stat.islink() || !stat.exists()) continue;
    bool complete = name.length() >= 7;
    if (complete) {
      static const char suffix[] = ".failed";
      for (unsigned j = 0; j < 7; ++j)
        if (name[name.length() - 7 + j] != suffix[j]) complete = false;
    }
    if (!complete) {
#ifndef _WIN32
      if (pid == unsigned(Zm::getPID()) ||
          (!::kill(pid, 0) || errno == EPERM)) continue;
      Zi::Path archived;
      archived << path << ".failed";
      if (ZiFile::rename(path, archived) != Zi::OK) continue;
      path = ZuMv(archived);
      stat = ZiStat{path};
#else
      if (pid == unsigned(Zm::getPID())) continue;
      HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
          FALSE, pid);
      if (!process && ::GetLastError() == ERROR_ACCESS_DENIED) continue;
      if (process) {
        DWORD exitCode = STILL_ACTIVE;
        bool live = !::GetExitCodeProcess(process, &exitCode) ||
          exitCode == STILL_ACTIVE;
        ::CloseHandle(process);
        if (live) continue;
      }
      Zi::Path archived;
      archived << path << ".failed";
      if (ZiFile::rename(path, archived) != Zi::OK) continue;
      path = ZuMv(archived);
      stat = ZiStat{path};
#endif
    }
    failed.push(TmpEntry{ZuMv(path), stat.mtime(), stat.isdir()});
  }
  glob.final();
  if (failed.length() > Age) {
    ZuSort(failed.data(), failed.length(),
      [](const TmpEntry &a, const TmpEntry &b) {
        if (a.mtime > b.mtime) return -1;
        if (a.mtime < b.mtime) return 1;
        return 0;
      });
    for (unsigned i = Age; i < failed.length(); ++i) {
      const auto &entry = failed[i];
      if (entry.dir) ZiFile::removeTree(entry.path);
      else ZiFile::remove(entry.path);
    }
  }
#ifndef _WIN32
  ::flock(lock, LOCK_UN);
  ::close(lock);
#else
  ::UnlockFileEx(lock, 0, 1, 0, &overlap);
  ::CloseHandle(lock);
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

  auto env = Zt::getpath("ZI_LOGDIR");
  Zi::Path root = env && *env ? Zi::Path{env} : ZiFile::cwd();
  if (!ZiFile::absolute(root)) root = ZiFile::append(ZiFile::cwd(), root);
  mkdir_(root);
  state.rootDir = ZiFile::canonical(root);
  ZiAssert(state.rootDir.length(), "ZiTestResidue", (),
      "cannot canonicalize residue root", ::abort());
  state.testName = name;
  ageTmp_(name);
  Zi::Path leaf;
  leaf << state.testName << '.';
  state.basePath = ZiFile::append(state.rootDir, leaf);
  state.phase = State::Active;

  ::atexit(&cleanup);
  ZuTestMgr::finalFn(&final);
}

Zi::Path path(ZuCSpan name)
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

Zi::Path file(ZuCSpan name)
{
  Zi::Path result = path(name);
  ZiFile::age(result, Age);
  add(result);
  return result;
}

Zi::Path dir(ZuCSpan name)
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

static Zi::Path tmpPath_(ZuCSpan tag)
{
  validateName_(tag);
  State &state = state_();
  State::Guard guard(state.lock);
  ZiAssert(state.phase == State::Active, "ZiTestResidue", (),
      "residue manager is not active", ::abort());
  Zi::Path leaf;
  leaf << "ZiTest." << state.testName << '.' << ZuBoxed(Zm::getPID())
    << '.' << ZuBox<unsigned>(++state.counter) << '.' << tag;
  return ZiFile::append(ZiFile::tmpDir(), leaf);
}

Zi::Path tmpFile(ZuCSpan tag)
{
  State &state = state_();
  ageTmp_(state.testName);
  Zi::Path result = tmpPath_(tag);
  State::Guard guard(state.lock);
  ZiAssert(!ZiStat{result}.exists(), "ZiTestResidue", (result),
      "temporary file already exists: " << result, ::abort());
  state.tmpFiles.push(result);
  return result;
}

Zi::Path tmpDir(ZuCSpan tag)
{
  State &state = state_();
  ageTmp_(state.testName);
  Zi::Path result = tmpPath_(tag);
  State::Guard guard(state.lock);
  ZiAssert(!ZiStat{result}.exists(), "ZiTestResidue", (result),
      "temporary directory already exists: " << result, ::abort());
  state.tmpDirs.push(result);
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

Paths glob(const Zi::Path &dir, ZuCSpan prefix)
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

void del(const Zi::Path &dir, ZuCSpan prefix)
{
  Paths paths = glob(dir, prefix);
  for (const auto &path : paths)
    ZiAssert(ZiFile::remove(path) == Zi::OK, "ZiTestResidue", (path),
        "remove failed: " << path, ::abort());
}

Zi::Name uniqueName(const char *tag)
{
  if (tag && *tag) validateName_(ZuCSpan{tag});
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

void addShm(Zi::Name name)
{
  validateName_(name);
  State &state = state_();
  State::Guard guard(state.lock);
  ZiAssert(state.phase == State::Active, "ZiTestResidue", (),
      "residue manager is not active", ::abort());
  for (const auto &base : state.shmBases)
    ZiAssert(base != name, "ZiTestResidue", (name),
        "duplicate shared-memory name: " << name, ::abort());
  state.shmBases.push(ZuMv(name));
}

void final(bool passed)
{
  Paths files;
  Paths dirs;
  Paths tmpFiles;
  Paths tmpDirs;
  State::Names shmBases;
  Zi::Name testName;
  {
    State &state = state_();
    State::Guard guard(state.lock);
    if (state.phase != State::Active) return;
    files = ZuMv(state.files);
    dirs = ZuMv(state.dirs);
    tmpFiles = ZuMv(state.tmpFiles);
    tmpDirs = ZuMv(state.tmpDirs);
    shmBases = ZuMv(state.shmBases);
    testName = state.testName;
    state.phase = State::Final;
  }

  for (const auto &base : shmBases) cleanupShm_(base);
  if (!passed) {
    for (const auto &path : tmpFiles)
      if (ZiStat{path}.exists()) {
        Zi::Path archived;
        archived << path << ".failed";
        ZiAssert(ZiFile::rename(path, archived) == Zi::OK,
            "ZiTestResidue", (path), "retain temporary file failed", ::abort());
      }
    for (const auto &path : tmpDirs)
      if (ZiStat{path}.exists()) {
        Zi::Path archived;
        archived << path << ".failed";
        ZiAssert(ZiFile::rename(path, archived) == Zi::OK,
            "ZiTestResidue", (path), "retain temporary tree failed", ::abort());
      }
    ageTmp_(testName);
    return;
  }
  for (const auto &path : files)
    ZiAssert(ZiFile::remove(path) == Zi::OK, "ZiTestResidue", (path),
        "remove failed: " << path, ::abort());
  for (const auto &path : dirs)
    ZiAssert(ZiFile::removeTree(path) == Zi::OK, "ZiTestResidue", (path),
        "remove tree failed: " << path, ::abort());
  for (const auto &path : tmpFiles)
    if (ZiStat{path}.exists())
      ZiAssert(ZiFile::remove(path) == Zi::OK, "ZiTestResidue", (path),
          "remove temporary file failed: " << path, ::abort());
  for (const auto &path : tmpDirs)
    if (ZiStat{path}.exists())
      ZiAssert(ZiFile::removeTree(path) == Zi::OK, "ZiTestResidue", (path),
          "remove temporary tree failed: " << path, ::abort());
}

void cleanup()
{
  final(false);
}

} // ZiTestResidue
