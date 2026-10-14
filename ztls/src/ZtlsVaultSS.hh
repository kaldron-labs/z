//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private Secret Service D-Bus bodies. Routing belongs to ZdbusClient calls.

#ifndef ZtlsVaultSS_HH
#define ZtlsVaultSS_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZmRBTree.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfDBUS.hh>

namespace Ztls_::SS {

using Text = ZtString<ZtStringHeapID<"Ztls.Vault.SS.Text">>;
using Paths = ZtArray<Text, ZtArrayHeapID<"Ztls.Vault.SS.Paths">>;

struct Empty { };
ZuTypeList<> ZuFields_(Empty *, ZuFacet::DBUS *);

struct Attrs {
  ZuCSpan service;
  ZuCSpan account;
  ZuCSpan key;
};
using AttrMap = ZmRBTreeKV<Text, Text,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"Ztls.Vault.SS.Attr">>>;

// Outbound attributes borrow their actual values; received arbitrary keys
// belong to the decoded message's tree.
struct AttrsView {
  using Key = ZuCSpan;
  using Val = ZuCSpan;
  const Attrs *attrs = nullptr;
  AttrMap owned;

  struct Entry {
    ZuCSpan name;
    ZuCSpan value;
    ZuCSpan key() const { return name; }
    ZuCSpan val() const { return value; }
  };
  struct Iter {
    const Attrs *attrs;
    AttrMap::CIter<> iter;
    Entry entry;
    unsigned offset = 0;

    const Entry *operator ()() {
      if (!attrs) {
	if (auto node = iter()) {
	  entry = {node->key(), node->val()};
	  return &entry;
	}
	return nullptr;
      }
      switch (offset++) {
	case 0: entry = {"service", attrs->service}; break;
	case 1: entry = {"account", attrs->account}; break;
	case 2: entry = {"key", attrs->key}; break;
	default: return nullptr;
      }
      return &entry;
    }
  };

  AttrsView() = default;
  AttrsView(const Attrs *attrs_) : attrs{attrs_} { }
  AttrsView(AttrsView &&) = default;
  AttrsView &operator =(AttrsView &&) = default;
  auto citer() const { return Iter{attrs, owned.citer(), {}}; }
  void clean() { attrs = nullptr; owned.clean(); }
  void add(ZuCSpan key, ZuCSpan value) {
    if (auto node = owned.find(key)) node->val() = value;
    else owned.add(Text{key}, Text{value});
  }
};
inline ZfDBUS::AsMap<> ZfDBUS_Fmt(AttrsView *);

struct Variant : ZuUnion<Text, AttrsView, ZfDBUS::Any> {
  ZuDerive_(Variant, (ZuUnion<Text, AttrsView, ZfDBUS::Any>));
  friend ZuDefaultRDecayer ZuRDecayer(Variant *);
  friend ZfDBUS::AsVariant ZfDBUS_Fmt(Variant *);
};

ZtlsAPI int propID(ZuCSpan);

template <bool Item>
struct Props {
  using Key = ZuCSpan;
  using Val = Variant;
  Variant label;
  Variant attributes;

  struct Entry {
    ZuCSpan name;
    const Variant *value;
    ZuCSpan key() const { return name; }
    const Variant &val() const { return *value; }
  };
  struct Iter {
    const Props *props;
    Entry entry;
    unsigned offset = 0;

    const Entry *operator ()() {
      switch (offset++) {
	case 0:
	  entry = {Item ? "org.freedesktop.Secret.Item.Label" :
	    "org.freedesktop.Secret.Collection.Label", &props->label};
	  break;
	case 1:
	  if constexpr (Item) {
	    entry = {"org.freedesktop.Secret.Item.Attributes", &props->attributes};
	    break;
	  }
	  return nullptr;
	default: return nullptr;
      }
      return &entry;
    }
  };

  auto citer() const { return Iter{this, {}}; }
  void clean() { label = {}; attributes = {}; }
  void add(ZuCSpan key, Variant value) {
    switch (propID(key)) {
      case 0: if constexpr (!Item) label = ZuMv(value); break;
      case 1: if constexpr (Item) label = ZuMv(value); break;
      case 2: if constexpr (Item) attributes = ZuMv(value); break;
    }
  }
};
template <bool Item> inline ZfDBUS::AsMap<> ZfDBUS_Fmt(Props<Item> *);

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
struct CreateCollectionArg { Props<false> properties; ZuCSpan alias; };
struct CreateCollectionRes { Text collection; Text prompt; };
struct CreateItemArg { Props<true> properties; Secret secret; bool replace; };
struct CreateItemRes { Text item; Text prompt; };
struct PromptArg { ZuCSpan window; };
struct PromptCompleted { bool dismissed; Variant result; };
struct PromptPath { Text path; };
struct PromptPaths { Paths paths; };

ZfStruct(, OpenSessionArg,
  (algorithm, (Mutable),	String),
  (input, (Mutable),		UDT));
ZfStructRender(, OpenSessionArg, DBUS, algorithm, input);
ZfStruct(, OpenSessionRes,
  (output, (Mutable),		UDT),
  (session, (Mutable),		String));
ZfStructRender(, OpenSessionRes, DBUS,
  output, (session, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, SearchArg, (attributes, (Mutable), UDT));
ZfStructRender(, SearchArg, DBUS, attributes);
ZfStruct(, SearchRes,
  (unlocked, (Mutable),		StringVec),
  (locked, (Mutable),		StringVec));
ZfStructRender(, SearchRes, DBUS,
  (unlocked, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)),
  (locked, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, UnlockArg,
  (objects, (Mutable),		StringVec));
ZfStructRender(, UnlockArg, DBUS,
  (objects, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, UnlockRes,
  (unlocked, (Mutable),		StringVec),
  (prompt, (Mutable),		String));
ZfStructRender(, UnlockRes, DBUS,
  (unlocked, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)),
  (prompt, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, ReadAliasArg, (name, (Mutable), String));
ZfStructRender(, ReadAliasArg, DBUS, name);
ZfStruct(, ReadAliasRes, (collection, (Mutable), String));
ZfStructRender(, ReadAliasRes, DBUS,
  (collection, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, Secret,
  (session, (Mutable),		String),
  (parameters, (Mutable),	Bytes),
  (value, (Mutable),		Bytes),
  (contentType, (Mutable),	String));
ZfStructRender(, Secret, DBUS,
  (session, (DBUS::Type<ZfDBUS::Type::ObjectPath>)),
  parameters, value, contentType);
ZfStruct(, GetSecretArg, (session, (Mutable), String));
ZfStructRender(, GetSecretArg, DBUS,
  (session, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, GetSecretRes, (secret, (Mutable), UDT));
ZfStructRender(, GetSecretRes, DBUS, secret);
ZfStruct(, CreateCollectionArg,
  (properties, (Mutable),	UDT),
  (alias, (Mutable),		String));
ZfStructRender(, CreateCollectionArg, DBUS, properties, alias);
ZfStruct(, CreateCollectionRes,
  (collection, (Mutable),	String),
  (prompt, (Mutable),		String));
ZfStructRender(, CreateCollectionRes, DBUS,
  (collection, (DBUS::Type<ZfDBUS::Type::ObjectPath>)),
  (prompt, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, CreateItemArg,
  (properties, (Mutable),	UDT),
  (secret, (Mutable),		UDT),
  (replace, (Mutable),		Bool));
ZfStructRender(, CreateItemArg, DBUS, properties, secret, replace);
ZfStruct(, CreateItemRes,
  (item, (Mutable),		String),
  (prompt, (Mutable),		String));
ZfStructRender(, CreateItemRes, DBUS,
  (item, (DBUS::Type<ZfDBUS::Type::ObjectPath>)),
  (prompt, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, PromptArg, (window, (Mutable), String));
ZfStructRender(, PromptArg, DBUS, window);
ZfStruct(, PromptCompleted,
  (dismissed, (Mutable),	Bool),
  (result, (Mutable),		UDT));
ZfStructRender(, PromptCompleted, DBUS, dismissed, result);
ZfStruct(, PromptPath, (path, (Mutable), String));
ZfStructRender(, PromptPath, DBUS,
  (path, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, PromptPaths, (paths, (Mutable), StringVec));
ZfStructRender(, PromptPaths, DBUS,
  (paths, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)));

} // Ztls_::SS

#endif /* ZtlsVaultSS_HH */
