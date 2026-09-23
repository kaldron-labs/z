//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stddef.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdbusAddress.hh>

using namespace ZuTestUtil;

static void path()
{
  ZuTestScope(path);
  ZdbusAddress address;
  ZuCheck(ZdbusAddress::parse(address, "unix:path=/run/dbus/a%20b"));
  ZuCheck(address.kind == Zdbus_::AddrKind::Path);
  ZuCheck(address.value == "/run/dbus/a b");
  sockaddr_un socket;
  socklen_t length = 0;
  ZuCheck(address.socketAddr(socket, length));
  ZuCheck(length == offsetof(sockaddr_un, sun_path) +
    address.value.length() + 1);
  ZuCheck(socket.sun_family == AF_UNIX);
  ZuCheck(!strcmp(socket.sun_path, "/run/dbus/a b"));
}

static void abstract()
{
  ZuTestScope(abstract);
  ZdbusAddress address;
  ZuCheck(ZdbusAddress::parse(address,
    "tcp:host=example.org;unix:abstract=bus%00name,guid=abc"));
  ZuCheck(address.kind == Zdbus_::AddrKind::Abstract);
  ZuCheck(address.value.length() == 8);
  sockaddr_un socket;
  socklen_t length = 0;
  ZuCheck(address.socketAddr(socket, length));
  ZuCheck(length == offsetof(sockaddr_un, sun_path) + 9);
  ZuCheck(!socket.sun_path[0]);
  ZuCheck(!memcmp(socket.sun_path + 1, "bus\0name", 8));
}

static void invalid()
{
  ZuTestScope(invalid);
  ZdbusAddress address;
  ZuCheck(!ZdbusAddress::parse(address, "unix:path=relative"));
  ZuCheck(!ZdbusAddress::parse(address, "unix:path=%00"));
  ZuCheck(!ZdbusAddress::parse(address, "unix:path=%Q0"));
  ZuCheck(!ZdbusAddress::parse(address, "unix:path=/x,abstract=y"));
  ZuCheck(!ZdbusAddress::parse(address, "unix:guid=abc"));
  ZdbusAddress::Text encoded{"unix:path=/"};
  for (unsigned i = 0; i < sizeof(sockaddr_un{}.sun_path); ++i)
    encoded << 'x';
  ZuCheck(!ZdbusAddress::parse(address, encoded));
  ZuCheck(!address);

  sockaddr_un socket;
  ZdbusAddress::Text longPath;
  for (unsigned i = 0; i < sizeof(socket.sun_path); ++i)
    longPath << 'x';
  address.kind = Zdbus_::AddrKind::Path;
  address.value = ZuMv(longPath);
  socklen_t length = 0;
  ZuCheck(!address.socketAddr(socket, length));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(path);
  ZuTestCall(abstract);
  ZuTestCall(invalid);
  return 0;
}
