//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZiTestResidue_HH
#define ZiTestResidue_HH

#include <unistd.h>
#include <time.h>

#include <vector>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include <zlib/ZuBox.hh>

#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZiDir.hh>
#include <zlib/ZiFile.hh>

namespace ZiTestResidue {

namespace {
struct State {
  using Guard = ZmGuard<ZmLock>;

  ZmLock			lock;
  bool				initialized = false;
  bool				cleaned = false;
  Zi::Path			baseDir;
  Zi::Path			runDir;
  Zi::Path			manifest;
  Zi::Name			testName;
  unsigned			counter = 0;
  std::vector<Zi::Path>		files;
  std::vector<Zi::Path>		dirs;
  std::vector<Zi::Name>		shmBases;
};

inline State &state_()
{
  static State state;
  return state;
}

inline void mkdirIfNeeded_(const Zi::Path &path)
{
  if (ZiFile::isdir(path)) return;
  ZiFile::mkdir(path);
}

inline bool endsWith_(const Zi::Path &path, const char *suffix)
{
  unsigned n = path.length();
  unsigned m = static_cast<unsigned>(::strlen(suffix));
  if (m > n) return false;
  return !::memcmp(path.data() + (n - m), suffix, m);
}

inline void trimLine_(char *line)
{
  unsigned n = static_cast<unsigned>(::strlen(line));
  while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
    line[--n] = 0;
}

inline void cleanupShm_(const Zi::Name &base)
{
#ifndef _WIN32
  Zi::Path ctrl;
  ctrl << '/' << base << ".ctrl";
  ::shm_unlink(ctrl);
  Zi::Path data;
  data << '/' << base << ".data";
  ::shm_unlink(data);
#else
  static_cast<void>(base);
#endif
}

inline void writeManifest_(char kind, ZuCSpan payload)
{
  auto &state = state_();

  ZiFile file;
  if (file.open(state.manifest, ZiFile::Append | ZiFile::GC) != Zi::OK) return;
  file.write(&kind, 1);
  char space = ' ';
  file.write(&space, 1);
  file.write(payload.data(), payload.length());
  char nl = '\n';
  file.write(&nl, 1);
  file.close();
}

inline void cleanupFromManifest_(const Zi::Path &manifestPath)
{
  ZiFile file;
  if (file.open(manifestPath, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) {
    ZiFile::remove(manifestPath);
    return;
  }

  auto size_ = file.size();
  if (size_ <= 0) {
    ZiFile::remove(manifestPath);
    return;
  }

  std::vector<char> buf(static_cast<unsigned>(size_) + 1U);
  int n = file.read(buf.data(), static_cast<unsigned>(size_));
  if (n <= 0) {
    ZiFile::remove(manifestPath);
    return;
  }
  buf[n] = 0;

  std::vector<Zi::Path> dirs;

  char *line = buf.data();
  for (;;) {
    char *next = ::strchr(line, '\n');
    if (next) *next = 0;
    trimLine_(line);
    if (line[0] && line[1] == ' ') {
      char kind = line[0];
      const char *payload = line + 2;
      switch (kind) {
	case 'F':
	  ZiFile::remove(payload);
	  break;
	case 'D':
	  dirs.push_back(payload);
	  break;
	case 'S':
	  cleanupShm_(Zi::Name(payload));
	  break;
	default:
	  break;
      }
    }
    if (!next) break;
    line = next + 1;
  }

  for (auto i = dirs.rbegin(); i != dirs.rend(); ++i)
    ZiFile::rmdir(*i);

  ZiFile::remove(manifestPath);
}

inline void cleanupStale_()
{
  auto &state = state_();

  ZiDir dir;
  if (dir.open(state.baseDir) != Zi::OK) return;

  Zi::Path name;
  while (dir.read(name) == Zi::OK) {
    if (name == "." || name == "..") continue;
    if (!endsWith_(name, ".manifest")) continue;
    cleanupFromManifest_(ZiFile::append(state.baseDir, name));
  }

  dir.close();
}
} // namespace

inline void init(const char *testName)
{
  auto &state = state_();
  State::Guard guard(state.lock);
  if (state.initialized) return;

  state.baseDir = ".ZiTestResidue";
  mkdirIfNeeded_(state.baseDir);
  cleanupStale_();

  struct timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);

  state.testName = testName;
  state.runDir = {};
  state.runDir << state.baseDir << '/'
	       << testName << '.'
	       << ZuBox<unsigned>(::getpid()) << '.'
	       << ZuBox<long>(ts.tv_sec) << '.'
	       << ZuBox<long>(ts.tv_nsec);
  mkdirIfNeeded_(state.runDir);

  state.manifest = {};
  state.manifest << state.runDir << ".manifest";
  state.initialized = true;
  state.cleaned = false;

  state.dirs.push_back(state.runDir);
  writeManifest_('D', state.runDir);
}

inline Zi::Path tempRoot()
{
  auto &state = state_();
  State::Guard guard(state.lock);
  return state.runDir;
}

inline Zi::Name uniqueName(const char *tag)
{
  auto &state = state_();
  State::Guard guard(state.lock);
  Zi::Name name;
  name << "ZiTest." << state.testName << '.'
       << ZuBox<unsigned>(::getpid()) << '.'
       << ZuBox<unsigned>(++state.counter);
  if (tag && *tag) name << '.' << tag;
  return name;
}

inline void addFile(const Zi::Path &path)
{
  auto &state = state_();
  State::Guard guard(state.lock);
  state.files.push_back(path);
  writeManifest_('F', path);
}

inline void addDir(const Zi::Path &path)
{
  auto &state = state_();
  State::Guard guard(state.lock);
  state.dirs.push_back(path);
  writeManifest_('D', path);
}

inline void addShmBase(const Zi::Name &name)
{
  auto &state = state_();
  State::Guard guard(state.lock);
  state.shmBases.push_back(name);
  writeManifest_('S', name);
}

inline void cleanupNow()
{
  auto &state = state_();
  State::Guard guard(state.lock);
  if (!state.initialized || state.cleaned) return;

  state.cleaned = true;

  for (auto i = state.files.rbegin(); i != state.files.rend(); ++i)
    ZiFile::remove(*i);
  for (auto i = state.shmBases.rbegin(); i != state.shmBases.rend(); ++i)
    cleanupShm_(*i);
  for (auto i = state.dirs.rbegin(); i != state.dirs.rend(); ++i)
    ZiFile::rmdir(*i);

  ZiFile::remove(state.manifest);
}

} // namespace ZiTestResidue

#endif /* ZiTestResidue_HH */
