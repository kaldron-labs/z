//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// PID file creation and ownership

#include <zlib/ZuArray.hh>
#include <zlib/ZuBox.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiPIDFile.hh>

#ifndef _WIN32
#include <errno.h>
#include <signal.h>
#else
#include <windows.h>
#endif

using PIDText = ZuCArray<sizeof(int) * 3 + 2>;

static bool dot(const Zi::Path &s)
{
#ifndef _WIN32
  return s == "." || s == "..";
#else
  return s == L"." || s == L"..";
#endif
}

static int openDir(ZiFile &file, const Zi::Path &path)
{
  return file.open(path,
    ZiFile::ReadOnly | ZiFile::Directory | ZiFile::NoFollow | ZiFile::GC);
}

static int resolvePath(
    Zi::Path &path, ZeError &error,
    const Zi::Path &dir, const Zi::Path &name)
{
  if (!dir || !ZiFile::absolute(dir) || !name || ZiFile::absolute(name)) {
    error = ZiEINVAL;
    return ZiPIDFile::Error;
  }

  ZiFile base;
  if (openDir(base, dir) != Zi::OK) {
    error = base.error();
    return ZiPIDFile::Error;
  }

  path = dir;
  auto tail = name.cspan();
  for (;;) {
    int64_t i = tail.find([](auto c) {
#ifndef _WIN32
      return c == '/';
#else
      return c == L'/' || c == L'\\';
#endif
    });
    bool last = i < 0;
    unsigned n = last ? tail.length() : unsigned(i);
    if (!n) {
      error = ZiEINVAL;
      return ZiPIDFile::Error;
    }
    Zi::Path part{tail.data(), n};
    if (dot(part)) {
      error = ZiEINVAL;
      return ZiPIDFile::Error;
    }
    path = ZiFile::append(path, part);
    if (last) break;
    tail = tail.offset(n + 1);

    ZiFile dir_;
    if (openDir(dir_, path) == Zi::OK) continue;
    if (ZiFile::mkdir(path, 0755, &error) != Zi::OK) {
      dir_ = {};
      if (openDir(dir_, path) != Zi::OK) {
	error = dir_.error();
	return ZiPIDFile::Error;
      }
      continue;
    }
    dir_ = {};
    if (openDir(dir_, path) != Zi::OK || dir_.mode(0755) != Zi::OK) {
      error = dir_.error();
      return ZiPIDFile::Error;
    }
  }
  return ZiPIDFile::OK;
}

static int readPID(ZiFile &file, int &pid)
{
  PIDText buf;
  int n = file.read(buf.data(), buf.size());
  pid = 0;
  if (n == Zi::EndOfFile) return Zi::OK;
  if (n < 0) return Zi::IOError;
  if (!n || n == int(buf.size())) return Zi::OK;
  ZuBox<int> parsed;
  if (parsed.scan(buf.data(), n) == n && parsed > 0) pid = parsed;
  return Zi::OK;
}

static bool pidRunning(int pid)
{
#ifndef _WIN32
  int i = ::kill(pid, 0);
  return i >= 0 || (i < 0 && errno == EPERM);
#else
  HANDLE h = OpenProcess(
    PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
  if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
  DWORD exitCode = 0;
  bool running =
    WaitForSingleObject(h, 0) == WAIT_TIMEOUT &&
    GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE;
  CloseHandle(h);
  return running;
#endif
}

ZiPIDFile::~ZiPIDFile()
{
  final();
}

void ZiPIDFile::final()
{
  if (m_owned) {
    ZiFile file;
    int pid;
    bool samePID =
      file.open(m_path, ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) ==
	Zi::OK && readPID(file, pid) == Zi::OK && pid == m_pid;
    file.close();
    if (samePID) ZiFile::remove(m_path);
  }
  m_path.null();
  m_error = ZeOK;
  m_pid = 0;
  m_owned = false;
}

int ZiPIDFile::init(const Zi::Path &name)
{
  return init(ZiFile::tmpDir(), name);
}

int ZiPIDFile::init(const Zi::Path &dir, const Zi::Path &name)
{
  final();
  if (resolvePath(m_path, m_error, dir, name) != OK) return Error;

retry:
  ZiFile file;
  if (file.open(m_path,
	ZiFile::WriteOnly | ZiFile::Create | ZiFile::Exclusive |
	ZiFile::NoFollow | ZiFile::Unpublished | ZiFile::GC, 0) == Zi::OK) {
    m_owned = true;
  } else {
    ZiStat stat{m_path};
    if (stat.isdir() || stat.islink()) {
      m_error = file.error();
      return Error;
    }
    file = {};
    if (file.open(m_path, ZiFile::NoFollow | ZiFile::GC) != Zi::OK) {
      Zm::sleep(1);
      goto retry;
    }
    stat = file.fstat();
    if (!stat) {
      m_error = stat.error();
      return Error;
    }
    if (stat.isdir()) {
      m_error = ZiEINVAL;
      return Error;
    }
    if (readPID(file, m_pid) != Zi::OK) {
      m_error = file.error();
      return Error;
    }
    if (m_pid > 0 && pidRunning(m_pid)) return Running;
    if (file.mode(0) != Zi::OK) {
      m_error = file.error();
      return Error;
    }
    m_owned = true;
  }

  m_pid = int(Zm::getPID());
  if (file.truncate(0) != Zi::OK || file.seek(0) != Zi::OK) goto error;
  {
    PIDText text;
    text << ZuBox<int>{m_pid};
    if (file.write(text.data(), text.length()) != Zi::OK) goto error;
  }
  if (file.mode(0644) != Zi::OK) goto error;
  return OK;

error:
  m_error = file.error();
  file.close();
  if (m_owned) ZiFile::remove(m_path);
  m_pid = 0;
  m_owned = false;
  return Error;
}
