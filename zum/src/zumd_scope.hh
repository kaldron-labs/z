//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// operation-owned scope selection and membership

#ifndef zumd_scope_HH
#define zumd_scope_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZmFn.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/zumd.hh>

namespace Zum {

struct DB;

ZtFlagsNS(ZumAPI, ScopeIDFlag, uint8_t, Access, Granted, Selected);
using ScopeIDs = ZmRBTreeKV<RoleID, ScopeIDFlag::T,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"Zum.ScopeIDs">>>;
ZtFlagsNS(ZumAPI, ScopeFlag, uint8_t, Identity, Granted, Requested, Selected);
// One owning node per name, shared by all four memberships.
using ScopeNames = ZmRBTreeKV<String, ScopeFlag::T,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"Zum.ScopeNames">>>;
using ScopeFn = ZmFn<void(int), ZmFnHeapID<"Zum.ScopeFn">>;

class ScopeSelect {
public:
  enum { Lookup = 3 };

  // Client/omitted-ID backing is owned by the stable caller until completion.
  // Scope names are owned by their tree nodes.
  // An omitted request selects the grant text in first-occurrence order.
  ZumAPI void init(const Client &, bool requestedPresent, ZuCSpan requested,
    const IDVec *grantedIDs = nullptr, const String *granted = nullptr);
  ZumAPI void begin(const ClientAccess &, bool restricted,
    const IDVec *omittedIDs = nullptr);
  ZumAPI int step();
  ZumAPI int role(const Role &);
  ZumAPI ScopeSelection take();

  bool byID() const { return m_omittedIDs; }
  RoleID id() const { return m_id; }
  ZuCSpan name() const { return m_name; }
  bool selected(RoleID id) const {
    auto node = m_ids.find(id);
    return node && (node->val() & ScopeIDFlag::Selected());
  }

  // Completion keeps the enclosing operation alive until all DB work ends.
  ZumAPI void run(DB *, const ClientAccess &, bool restricted,
    const IDVec *omittedIDs, ScopeFn);

private:
  ScopeNames::Node *scope_(ZuCSpan);
  ScopeIDs::Node *id_(RoleID);
  void request_(ScopeNames::Node *);
  void addName_(ScopeNames::Node *);
  void next_();
  void finish_(int);

  const Client	*m_client = nullptr;
  const ClientAccess *m_access = nullptr;
  const IDVec	*m_omittedIDs = nullptr;
  ScopeNames	m_scopes;
  using Tokens = ZtArray<ScopeNames::Node *, VecHeap>;
  Tokens	m_tokens;
  ScopeIDs	m_ids;
  ScopeSelection m_next;
  ZuCSpan	m_name;
  RoleID	m_id = 0;
  unsigned	m_offset = 0;
  bool		m_restricted = false;
  bool		m_hasGrant = false;
  bool		m_grantedText = false;
  DB	*m_db = nullptr;
  ScopeFn	m_complete;
};

} // namespace Zum

#endif /* zumd_scope_HH */
