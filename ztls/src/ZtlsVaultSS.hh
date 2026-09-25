//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private Secret Service D-Bus bodies. Routing belongs to ZdbusClient calls.

#ifndef ZtlsVaultSS_HH
#define ZtlsVaultSS_HH

#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfDBUS.hh>

namespace Ztls_::SS {

using Text = ZtString<ZtStringHeapID<"Ztls.Vault.SS.Text">>;
using Paths = ZtArray<Text, ZtArrayHeapID<"Ztls.Vault.SS.Paths">>;

struct Empty { };
ZuTypeList<> ZuFields_(Empty *, ZuFacet::DBUS *);

template <typename Heap = ZuVoid>
struct Attrs_ : Heap, ZmHashKV<Text, Text,
    ZmHashHeapID<"Ztls.Vault.SS.Attr">> { };
using AttrsHeap = ZmHeap<"Ztls.Vault.SS.Attrs", Attrs_<>>;
ZuDerive(Attrs, Attrs_<AttrsHeap>);
inline ZfDBUS::AsMap<> ZfDBUS_Fmt(Attrs *);

// A variant may borrow the same map used for SearchItems; no duplicate map.
struct AttrsView {
  using Key = Text;
  using Val = Text;
  const Attrs *attrs = nullptr;
  ZuPtr<Attrs> owned;

  AttrsView() = default;
  AttrsView(const Attrs *attrs_) : attrs{attrs_} { }
  AttrsView(AttrsView &&) = default;
  AttrsView &operator =(AttrsView &&) = default;
  auto citer() const { return attrs->citer(); }
  void clean() { owned = new Attrs; attrs = owned.ptr(); }
  void add(Text key, Text value) {
    if (!owned) clean();
    owned->add(ZuMv(key), ZuMv(value));
  }
};
inline ZfDBUS::AsMap<> ZfDBUS_Fmt(AttrsView *);

struct Variant : ZuUnion<Text, AttrsView, ZfDBUS::Any> {
  ZuDerive_(Variant, (ZuUnion<Text, AttrsView, ZfDBUS::Any>));
  friend ZuDefaultRDecayer ZuRDecayer(Variant *);
  friend ZfDBUS::AsVariant ZfDBUS_Fmt(Variant *);
};

struct Props : ZmHashKV<Text, Variant,
    ZmHashHeapID<"Ztls.Vault.SS.Prop">> { };
inline ZfDBUS::AsMap<> ZfDBUS_Fmt(Props *);

struct OpenSessionArg { ZuCSpan algorithm; Variant input; };
struct OpenSessionRes { Variant output; Text session; };
struct SearchArg { AttrsView attributes; };
struct SearchRes { Paths unlocked; Paths locked; };
struct UnlockArg { Paths objects; };
struct UnlockRes { Paths unlocked; Text prompt; };
struct ReadAliasArg { ZuCSpan name; };
struct ReadAliasRes { Text collection; };

struct Secret {
  Text	session;
  ZuBSpan parameters;
  ZuBSpan value;
  Text	contentType;
};
struct GetSecretArg { Text session; };
struct GetSecretRes { Secret secret; };
struct CreateCollectionArg { Props properties; ZuCSpan alias; };
struct CreateCollectionRes { Text collection; Text prompt; };
struct CreateItemArg { Props properties; Secret secret; bool replace; };
struct CreateItemRes { Text item; Text prompt; };
struct PromptArg { ZuCSpan window; };
struct PromptCompleted { bool dismissed; Variant result; };
struct PromptPath { Text path; };
struct PromptPaths { Paths paths; };

ZfStruct(, OpenSessionArg,
  (((algorithm), (Mutable)), (String)),
  (((input), (Mutable)), (UDT)));
ZfStructRender(, OpenSessionArg, DBUS, algorithm, input);
ZfStruct(, OpenSessionRes,
  (((output), (Mutable)), (UDT)),
  (((session), (Mutable)), (String)));
ZfStructRender(, OpenSessionRes, DBUS,
  output, (session, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, SearchArg, (((attributes), (Mutable)), (UDT)));
ZfStructRender(, SearchArg, DBUS, attributes);
ZfStruct(, SearchRes,
  (((unlocked), (Mutable)), (StringVec)),
  (((locked), (Mutable)), (StringVec)));
ZfStructRender(, SearchRes, DBUS,
  (unlocked, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)),
  (locked, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, UnlockArg,
  (((objects), (Mutable)), (StringVec)));
ZfStructRender(, UnlockArg, DBUS,
  (objects, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, UnlockRes,
  (((unlocked), (Mutable)), (StringVec)),
  (((prompt), (Mutable)), (String)));
ZfStructRender(, UnlockRes, DBUS,
  (unlocked, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)),
  (prompt, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, ReadAliasArg, (((name), (Mutable)), (String)));
ZfStructRender(, ReadAliasArg, DBUS, name);
ZfStruct(, ReadAliasRes, (((collection), (Mutable)), (String)));
ZfStructRender(, ReadAliasRes, DBUS,
  (collection, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, Secret,
  (((session), (Mutable)), (String)),
  (((parameters), (Mutable)), (Bytes)),
  (((value), (Mutable)), (Bytes)),
  (((contentType), (Mutable)), (String)));
ZfStructRender(, Secret, DBUS,
  (session, (DBUS::Type<ZfDBUS::Type::ObjectPath>)),
  parameters, value, contentType);
ZfStruct(, GetSecretArg, (((session), (Mutable)), (String)));
ZfStructRender(, GetSecretArg, DBUS,
  (session, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, GetSecretRes, (((secret), (Mutable)), (UDT)));
ZfStructRender(, GetSecretRes, DBUS, secret);
ZfStruct(, CreateCollectionArg,
  (((properties), (Mutable)), (UDT)),
  (((alias), (Mutable)), (String)));
ZfStructRender(, CreateCollectionArg, DBUS, properties, alias);
ZfStruct(, CreateCollectionRes,
  (((collection), (Mutable)), (String)),
  (((prompt), (Mutable)), (String)));
ZfStructRender(, CreateCollectionRes, DBUS,
  (collection, (DBUS::Type<ZfDBUS::Type::ObjectPath>)),
  (prompt, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, CreateItemArg,
  (((properties), (Mutable)), (UDT)),
  (((secret), (Mutable)), (UDT)),
  (((replace), (Mutable)), (Bool)));
ZfStructRender(, CreateItemArg, DBUS, properties, secret, replace);
ZfStruct(, CreateItemRes,
  (((item), (Mutable)), (String)),
  (((prompt), (Mutable)), (String)));
ZfStructRender(, CreateItemRes, DBUS,
  (item, (DBUS::Type<ZfDBUS::Type::ObjectPath>)),
  (prompt, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, PromptArg, (((window), (Mutable)), (String)));
ZfStructRender(, PromptArg, DBUS, window);
ZfStruct(, PromptCompleted,
  (((dismissed), (Mutable)), (Bool)),
  (((result), (Mutable)), (UDT)));
ZfStructRender(, PromptCompleted, DBUS, dismissed, result);
ZfStruct(, PromptPath, (((path), (Mutable)), (String)));
ZfStructRender(, PromptPath, DBUS,
  (path, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, PromptPaths, (((paths), (Mutable)), (StringVec)));
ZfStructRender(, PromptPaths, DBUS,
  (paths, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)));

} // Ztls_::SS

#endif /* ZtlsVaultSS_HH */
