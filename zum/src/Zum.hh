//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// user DB
// - local IAM service
// - RBAC

#ifndef Zum_HH
#define Zum_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuArray.hh>

#include <zlib/ZtString.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/Ztls.hh>
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsRandom.hh>

#include <zlib/ZumKey_fbs.h>
#include <zlib/ZumPerm_fbs.h>
#include <zlib/ZumRole_fbs.h>
#include <zlib/ZumUser_fbs.h>

#include <zlib/ZumLoginReq_fbs.h>
#include <zlib/ZumLoginAck_fbs.h>
#include <zlib/ZumRequest_fbs.h>
#include <zlib/ZumReqAck_fbs.h>

namespace Zum {

ZuDerive(String, ZtString<ZtStringHeapID<"Zum.String">>);
ZuDerive(StringVec, (ZtArray<String, ZtArrayHeapID<"Zum.String">>));

enum { IOBufSize = 512 }; // built-in buffer size

using IOBufAlloc = ZiIOBufAlloc<IOBufSize>;

struct IOBuilder : public Zfb::IOBuilder {
  ZuDerive_(IOBuilder, Zfb::IOBuilder)

  IOBuilder() : Zfb::IOBuilder{new IOBufAlloc{}} { }
};

using SeqNo = uint64_t;

constexpr Ztls::MDType::T KeyType = Ztls::MD::SHA256;

enum { KeySize = Ztls::HMAC<KeyType>::Size }; // 256 bit key
using KeyData = ZuArray<uint8_t, KeySize>;
enum { KeyIDSize = 16 };
using KeyIDData = ZuArray<uint8_t, KeyIDSize>;

using PermID = uint32_t;
using UserID = uint64_t;

enum { MaxQueryLimit = 1000 };	// maximum batch size for queries

enum { MaxAPIKeys = 10 };	// maximum number of API keys per user

// API key
struct Key {
  UserID		userID;
  KeyIDData		id;
  KeyData		secret;

  friend ZfStructPrint ZuPrintType(Key *);
};
ZfbStruct(Key,
  (((userID),	(Ctor<0>, Keys<0>, Group<0>)),	(UInt64)),
  (((id),	(Ctor<1>, (Keys<0, 1>))),	(Bytes)),
  (((secret),	(Ctor<2>, Mutable, Hidden)),	(Bytes)));

ZfbRoot(Key);

// individual permission (aka IAM action)
struct Perm {
  PermID		id;
  String		name;

  friend ZfStructPrint ZuPrintType(Perm *);
};
ZfbStruct(Perm,
  (((id),	(Ctor<0>, Keys<0>, Descend<0>)),	(UInt32)),
  (((name),	(Ctor<1>, Keys<1>, Mutable)),		(String)));

ZfbRoot(Perm);

ZtFlagsNS(ZumAPI, RoleFlags, uint8_t, Immutable);

// role (i.e. a combination of permitted actions)
struct Role {
  String		name;
  ZtBitmap		perms;
  ZtBitmap		apiperms;
  uint8_t		flags;		// RoleFlags

  friend ZfStructPrint ZuPrintType(Role *);
};
ZfbStruct(Role,
  (((name),	(Ctor<0>, Keys<0>)),				(String)),
  (((perms),	(Ctor<1>, Mutable)),				(Bitmap)),
  (((apiperms),	(Ctor<2>, Mutable)),				(Bitmap)),
  (((flags),	(Ctor<3>, Flags<RoleFlags::Map>, Mutable)),	(UInt8)));

ZfbRoot(Role);

ZtFlagsNS(ZumAPI, UserFlags, uint8_t,
  Immutable,
  Enabled,
  SuperUser,
  ChPass);		// user must change password

struct User {
  UserID		id;
  String		name;
  KeyData		secret;
  KeyData		hmac;
  StringVec		roles;
  uint32_t		failures = 0;
  UserFlags::T		flags = 0;	// UserFlags

  friend ZfStructPrint ZuPrintType(User *);
};
ZfbStruct(User,
  (((id),	(Ctor<0>, Keys<0>, Descend<0>)),		(UInt64)),
  (((name),	(Ctor<1>, Keys<1>, Mutable)),			(String)),
  (((secret),	(Ctor<2>, Mutable, Hidden)),			(Bytes)),
  (((hmac),	(Ctor<3>, Mutable)),				(Bytes)),
  (((roles),	(Ctor<4>, Mutable)),				(StringVec)),
  (((failures),	(Ctor<5>, Mutable)),				(UInt32, 0)),
  (((flags),	(Ctor<6>, Mutable, Flags<UserFlags::Map>)),	(UInt8, 0)));

ZfbRoot(User);

} // Zum

#endif /* Zum_HH */
