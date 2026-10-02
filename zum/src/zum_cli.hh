//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Administrative command arguments and request serialization.

#ifndef zum_cli_HH
#define zum_cli_HH

#include <zlib/ZumLib.hh>

#include <zlib/ZuBox.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZumMgmt.hh>

namespace ZumCLI {

using String = ZtString<ZtStringHeapID<"zumc.Args">>;

struct Options {
  String config;
  String command;
  String ifMatch;
  String ifNoneMatch;
  String idempotence;
  bool noBrowser = false;
  bool help = false;
  String arg1;
  String arg2;
  String arg3;
  String arg4;
  String arg5;
  String arg6;
  String arg7;
  String arg8;
  String arg9;
  String arg10;
  String actionID;
  String actionIDs;
  String actorID;
  String actorKind;
  String algorithm;
  String appID;
  String assignmentMaxAge;
  String audience;
  String before;
  String catalog;
  String claimSource;
  String clientID;
  String clientSecret;
  String clientType;
  String consentPolicy;
  String credentialID;
  String cursor;
  String digest;
  String eligibilityClaim;
  String eligibilityMode;
  String eligibilityValues;
  String email;
  uint8_t grants = 0;
  String id;
  String identityScopes;
  String issuer;
  String keyID;
  String label;
  String limit;
  String localFirst;
  String name;
  String notBefore;
  String operation;
  String operationIDs;
  String overlapSeconds;
  String privateMaterial;
  String profile;
  String providerID;
  String providerRef;
  String publicJwk;
  String redirectURIs;
  String refreshAllowed;
  String retireAfter;
  String revision;
  String roleClaim;
  String roleID;
  String roleIDs;
  String scopes;
  String sessionAbsolute;
  String sessionIdle;
  String source;
  String state;
  String tokenLifetime;
  String userID;
  String valueKey;
};

ZfStruct(, (Options, CLI),
  (((config), (CLI::Long<"config">)),				String),
  (((command), (CLI::Arg<1>)),					String),
  (((ifMatch), (CLI::Long<"if-match">)),			String),
  (((ifNoneMatch), (CLI::Long<"if-none-match">)),		String),
  (((idempotence), (CLI::Long<"idempotence">)),			String),
  (((noBrowser), (CLI::Long<"no-browser">)),			Bool),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)),		Bool),
  (((arg1), (CLI::Arg<2>)),					String),
  (((arg2), (CLI::Arg<3>)),					String),
  (((arg3), (CLI::Arg<4>)),					String),
  (((arg4), (CLI::Arg<5>)),					String),
  (((arg5), (CLI::Arg<6>)),					String),
  (((arg6), (CLI::Arg<7>)),					String),
  (((arg7), (CLI::Arg<8>)),					String),
  (((arg8), (CLI::Arg<9>)),					String),
  (((arg9), (CLI::Arg<10>)),					String),
  (((arg10), (CLI::Arg<11>)),					String),
  (((actionID), (CLI::Long<"action-id">)),			String),
  (((actionIDs), (CLI::Long<"action-ids">)),			String),
  (((actorID), (CLI::Long<"actor-id">)),			String),
  (((actorKind), (CLI::Long<"actor-kind">)),			String),
  (((algorithm), (CLI::Long<"algorithm">)),			String),
  (((appID), (CLI::Long<"app-id">)),				String),
  (((assignmentMaxAge), (CLI::Long<"assignment-max-age">)),	String),
  (((audience), (CLI::Long<"audience">)),			String),
  (((before), (CLI::Long<"before">)),				String),
  (((catalog), (CLI::Long<"catalog">)),				String),
  (((claimSource), (CLI::Long<"claim-source">)),		String),
  (((clientID), (CLI::Long<"client-id">)),			String),
  (((clientSecret), (CLI::Long<"client-secret">)),		String),
  (((clientType), (CLI::Long<"client-type">)),			String),
  (((consentPolicy), (CLI::Long<"consent-policy">)),		String),
  (((credentialID), (CLI::Long<"credential-id">)),		String),
  (((cursor), (CLI::Long<"cursor">)),				String),
  (((digest), (CLI::Long<"digest">)),				String),
  (((eligibilityClaim), (CLI::Long<"eligibility-claim">)),	String),
  (((eligibilityMode), (CLI::Long<"eligibility-mode">)),	String),
  (((eligibilityValues), (CLI::Long<"eligibility-values">)),	String),
  (((email), (CLI::Long<"email">)),				String),
  (((grants), (CLI::Long<"grants">, Flags<Zum::ClientGrant::Map>,
      CLI::Number<Zum::ClientGrant::Fmt>)),			UInt8),
  (((id), (CLI::Long<"id">)),					String),
  (((identityScopes), (CLI::Long<"identity-scopes">)),		String),
  (((issuer), (CLI::Long<"issuer">)),				String),
  (((keyID), (CLI::Long<"key-id">)),				String),
  (((label), (CLI::Long<"label">)),				String),
  (((limit), (CLI::Long<"limit">)),				String),
  (((localFirst), (CLI::Long<"local-first">)),			String),
  (((name), (CLI::Long<"name">)),				String),
  (((notBefore), (CLI::Long<"not-before">)),			String),
  (((operation), (CLI::Long<"operation">)),			String),
  (((operationIDs), (CLI::Long<"operation-ids">)),		String),
  (((overlapSeconds), (CLI::Long<"overlap-seconds">)),		String),
  (((privateMaterial), (CLI::Long<"private-material">)),	String),
  (((profile), (CLI::Long<"profile">)),				String),
  (((providerID), (CLI::Long<"provider-id">)),			String),
  (((providerRef), (CLI::Long<"provider-ref">)),		String),
  (((publicJwk), (CLI::Long<"public-jwk">)),			String),
  (((redirectURIs), (CLI::Long<"redirect-uris">)),		String),
  (((refreshAllowed), (CLI::Long<"refresh-allowed">)),		String),
  (((retireAfter), (CLI::Long<"retire-after">)),		String),
  (((revision), (CLI::Long<"revision">)),			String),
  (((roleClaim), (CLI::Long<"role-claim">)),			String),
  (((roleID), (CLI::Long<"role-id">)),				String),
  (((roleIDs), (CLI::Long<"role-ids">)),			String),
  (((scopes), (CLI::Long<"scopes">)),				String),
  (((sessionAbsolute), (CLI::Long<"session-absolute">)),	String),
  (((sessionIdle), (CLI::Long<"session-idle">)),		String),
  (((source), (CLI::Long<"source">)),				String),
  (((state), (CLI::Long<"state">)),				String),
  (((tokenLifetime), (CLI::Long<"token-lifetime">)),		String),
  (((userID), (CLI::Long<"user-id">)),				String),
  (((valueKey), (CLI::Long<"value-key">)),			String));

struct Spec {
  ZuCSpan required;
  ZuCSpan optional;
  ZuCSpan positional = {};
};

inline Spec spec(int op)
{
  using namespace Zum;
  switch (op) {
    case MgmtOp::issuerQuery: return {"", ""};
    case MgmtOp::operationQuery: return {"", "operation:s idempotence:s cursor:s limit:u"};
    case MgmtOp::appQuery: return {"", "id:q name:s cursor:s limit:u"};
    case MgmtOp::appEnroll: return {"name:s audience:s", "label:s client_type:s redirect_uris:S"};
    case MgmtOp::appUpdate: return {"app_id:q label:s", ""};
    case MgmtOp::appState: return {"app_id:q state:s", ""};
    case MgmtOp::userQuery: return {"", "id:q source:s cursor:s limit:u", "name:s"};
    case MgmtOp::userInvite: return {"name:s", "profile:s email:s"};
    case MgmtOp::userUpdate: return {"user_id:q", "profile:s email:s"};
    case MgmtOp::userState: return {"user_id:q state:s", ""};
    case MgmtOp::userRecover: return {"user_id:q", ""};
    case MgmtOp::credentialQuery: return {"", "id:s user_id:q cursor:s limit:u"};
    case MgmtOp::credentialUpdate: return {"credential_id:s label:s", ""};
    case MgmtOp::credentialState: return {"credential_id:s state:s", ""};
    case MgmtOp::assignmentQuery: return {"app_id:q", "user_id:q cursor:s limit:u"};
    case MgmtOp::assignmentAdd: return {"app_id:q user_id:q", ""};
    case MgmtOp::assignmentRoles: return {"app_id:q user_id:q role_ids:Q", ""};
    case MgmtOp::assignmentState: return {"app_id:q user_id:q state:s", ""};
    case MgmtOp::actionQuery: return {"app_id:q", "id:u name:s cursor:s limit:u"};
    case MgmtOp::actionAdd: return {"app_id:q name:s", "label:s"};
    case MgmtOp::actionState: return {"app_id:q action_id:u state:s", ""};
    case MgmtOp::roleQuery: return {"app_id:q", "id:u name:s cursor:s limit:u"};
    case MgmtOp::roleAdd: return {"app_id:q name:s", "label:s"};
    case MgmtOp::roleUpdate: return {"app_id:q role_id:q label:s", ""};
    case MgmtOp::roleActions: return {"app_id:q role_id:q action_ids:U", ""};
    case MgmtOp::roleState: return {"app_id:q role_id:q state:s", ""};
    case MgmtOp::roleDelete: return {"app_id:q role_id:q", ""};
    case MgmtOp::clientQuery: return {"", "id:s client_id:s cursor:s limit:u"};
    case MgmtOp::clientAdd: return {"app_id:q profile:s", "id:s label:s redirect_uris:S grants:g refresh_allowed:b identity_scopes:S"};
    case MgmtOp::clientUpdate: return {"client_id:s", "label:s redirect_uris:S grants:g identity_scopes:S"};
    case MgmtOp::clientState: return {"client_id:s state:s", ""};
    case MgmtOp::clientSecretRotate: return {"client_id:s", "overlap_seconds:u"};
    case MgmtOp::clientAccessQuery: return {"app_id:q", "client_id:s cursor:s limit:u"};
    case MgmtOp::clientAccessSet: return {"app_id:q client_id:s role_ids:Q", ""};
    case MgmtOp::clientAccessState: return {"app_id:q client_id:s state:s", ""};
    case MgmtOp::adminAccessQuery: return {"app_id:q", "cursor:s limit:u"};
    case MgmtOp::adminAccessSet: return {"app_id:q actor_kind:s actor_id:s operation_ids:U role_ids:Q", ""};
    case MgmtOp::adminAccessState: return {"app_id:q actor_kind:s actor_id:s state:s", ""};
    case MgmtOp::providerQuery: return {"", "id:q name:s cursor:s limit:u"};
    case MgmtOp::providerAdd: return {"name:s issuer:s client_id:s scopes:S role_claim:s claim_source:s", "client_secret:s"};
    case MgmtOp::providerUpdate: return {"provider_id:q", "issuer:s client_id:s client_secret:s scopes:S role_claim:s claim_source:s"};
    case MgmtOp::providerState: return {"provider_id:q state:s", ""};
    case MgmtOp::authPolicyQuery: return {"", "app_id:q cursor:s limit:u"};
    case MgmtOp::authPolicySet: return {"app_id:q local_first:b eligibility_mode:s assignment_max_age:u session_idle:u session_absolute:u token_lifetime:u consent_policy:s", "provider_id:q eligibility_claim:s eligibility_values:S state:s"};
    case MgmtOp::roleMapQuery: return {"app_id:q", "cursor:s limit:u"};
    case MgmtOp::roleMapSet: return {"app_id:q provider_id:q value_key:s role_id:q", ""};
    case MgmtOp::roleMapDelete: return {"app_id:q provider_id:q value_key:s", ""};
    case MgmtOp::identityQuery: return {"", "cursor:s limit:u"};
    case MgmtOp::evidenceQuery: return {"", "cursor:s limit:u"};
    case MgmtOp::sessionQuery: return {"", "cursor:s limit:u"};
    case MgmtOp::sessionRevoke: return {"user_id:q limit:u", ""};
    case MgmtOp::consentQuery: return {"", "cursor:s limit:u"};
    case MgmtOp::consentRevoke: return {"user_id:q limit:u", "client_id:s app_id:q"};
    case MgmtOp::grantQuery: return {"", "cursor:s limit:u"};
    case MgmtOp::grantRevoke: return {"limit:u", "id:s user_id:q app_id:q"};
    case MgmtOp::grantCleanup: return {"limit:u", "before:i"};
    case MgmtOp::signKeyQuery: return {"", "cursor:s limit:u"};
    case MgmtOp::signKeyAdd: return {"app_id:q id:s algorithm:s public_jwk:s not_before:i", "provider_ref:s private_material:s"};
    case MgmtOp::signKeyRetire: return {"key_id:s retire_after:i", ""};
    case MgmtOp::ssfRegister: return {"app_id:q receiver_id:s delivery_url:s callback_auth:s expires_in:u", ""};
    case MgmtOp::catalogPublish: return {"app_id:q catalog:j revision:u digest:s", ""};
    default: return {};
  }
}

inline ZuCSpan command(int op)
{
  using namespace Zum;
  switch (op) {
    case MgmtOp::issuerQuery: return "issuer list";
    case MgmtOp::operationQuery: return "operation list";
    case MgmtOp::appQuery: return "app list";
    case MgmtOp::appEnroll: return "app add";
    case MgmtOp::appUpdate: return "app update";
    case MgmtOp::appState: return "app state";
    case MgmtOp::userQuery: return "user list";
    case MgmtOp::userInvite: return "user add";
    case MgmtOp::userUpdate: return "user update";
    case MgmtOp::userState: return "user state";
    case MgmtOp::userRecover: return "user recover";
    case MgmtOp::credentialQuery: return "credential list";
    case MgmtOp::credentialUpdate: return "credential update";
    case MgmtOp::credentialState: return "credential state";
    case MgmtOp::assignmentQuery: return "assign list";
    case MgmtOp::assignmentAdd: return "assign add";
    case MgmtOp::assignmentRoles: return "assign roles";
    case MgmtOp::assignmentState: return "assign state";
    case MgmtOp::actionQuery: return "action list";
    case MgmtOp::actionAdd: return "action add";
    case MgmtOp::actionState: return "action state";
    case MgmtOp::roleQuery: return "role list";
    case MgmtOp::roleAdd: return "role add";
    case MgmtOp::roleUpdate: return "role update";
    case MgmtOp::roleActions: return "role actions";
    case MgmtOp::roleState: return "role state";
    case MgmtOp::roleDelete: return "role delete";
    case MgmtOp::clientQuery: return "client list";
    case MgmtOp::clientAdd: return "client add";
    case MgmtOp::clientUpdate: return "client update";
    case MgmtOp::clientState: return "client state";
    case MgmtOp::clientSecretRotate: return "client secret rotate";
    case MgmtOp::clientAccessQuery: return "client access list";
    case MgmtOp::clientAccessSet: return "client access set";
    case MgmtOp::clientAccessState: return "client access state";
    case MgmtOp::adminAccessQuery: return "admin access list";
    case MgmtOp::adminAccessSet: return "admin access set";
    case MgmtOp::adminAccessState: return "admin access state";
    case MgmtOp::providerQuery: return "provider list";
    case MgmtOp::providerAdd: return "provider add";
    case MgmtOp::providerUpdate: return "provider update";
    case MgmtOp::providerState: return "provider state";
    case MgmtOp::authPolicyQuery: return "auth policy list";
    case MgmtOp::authPolicySet: return "auth policy set";
    case MgmtOp::roleMapQuery: return "role map list";
    case MgmtOp::roleMapSet: return "role map set";
    case MgmtOp::roleMapDelete: return "role map delete";
    case MgmtOp::identityQuery: return "identity list";
    case MgmtOp::evidenceQuery: return "evidence list";
    case MgmtOp::sessionQuery: return "session list";
    case MgmtOp::sessionRevoke: return "session revoke";
    case MgmtOp::consentQuery: return "consent list";
    case MgmtOp::consentRevoke: return "consent revoke";
    case MgmtOp::grantQuery: return "grant list";
    case MgmtOp::grantRevoke: return "grant revoke";
    case MgmtOp::grantCleanup: return "grant cleanup";
    case MgmtOp::signKeyQuery: return "sign key list";
    case MgmtOp::signKeyAdd: return "sign key add";
    case MgmtOp::signKeyRetire: return "sign key retire";
    case MgmtOp::ssfRegister: return "ssf register";
    case MgmtOp::catalogPublish: return "catalog publish";
    default: return {};
  }
}

inline ZuCSpan word(ZuCSpan &text)
{
  auto end = text.find<" ">();
  if (end < 0) { auto value = text; text = {}; return value; }
  auto value = text;
  value.trunc(unsigned(end));
  text.offset(unsigned(end) + 1);
  return value;
}

inline ZuCSpan name(ZuCSpan field)
{
  field.trunc(field.length() - 2);
  return field;
}

inline String option(ZuCSpan name)
{
  String key;
  for (unsigned i = 0, n = name.length(); i < n; ++i) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z') {
      if (i && name[i - 1] >= 'a' && name[i - 1] <= 'z') key << '-';
      c += 'a' - 'A';
    }
    key << (c == '_' ? '-' : c);
  }
  return key;
}

inline const ZfCLI::AnyNode *field(
    const ZfCLI::AnyNode::Object &object, ZuCSpan key)
{
  auto item = object.find(key);
  return item ? item->val().ptr() : nullptr;
}

inline unsigned commandArgs(int op)
{
  unsigned count = 0;
  for (auto fields = command(op); fields; word(fields)) ++count;
  return count;
}

inline bool matches(const ZfCLI::Parser<Options> &parser,
    ZuCSpan words, unsigned count)
{
  const auto &object = parser.root->data<ZfCLI::AnyNode::Object>();
  unsigned pos = 0;
  while (words && count--) {
    String key;
    if (pos) key << "arg" << pos;
    else key = "command";
    ++pos;
    auto node = field(object, key);
    if (!node || !node->has<ZfCLI::AnyNode::String>() ||
	node->data<ZfCLI::AnyNode::String>() != word(words)) return false;
  }
  return true;
}

inline int operation(const ZfCLI::Parser<Options> &parser)
{
  for (int op = 0; op < Zum::MgmtOp::N; ++op)
    if (matches(parser, command(op), commandArgs(op))) return op;
  return -1;
}

inline bool value(String &json, ZuCSpan text, char type)
{
  switch (type) {
    case 's': ZfJSON::quote(json, text); return true;
    case 'b':
      if (text != "true" && text != "false") return false;
      json << text; return true;
    case 'g': {
      auto flags = Zum::ClientGrant::Map::Scan::eov(text, ",");
      if (flags.p<0>() < 0 || unsigned(flags.p<0>()) != text.length())
	return false;
      json << '"' << Zum::ClientGrant::Map::Print{flags.p<1>().val(), ","} << '"';
      return true;
    }
    case 'q':
    case 'u': {
      ZuBox<uint64_t> number;
      if (!text || text[0] == '-' ||
	  number.scan(text) != int(text.length())) return false;
      // ZuBox scanning does not report overflow; check the decimal round trip.
      if (text[0] == '+') text.offset(1);
      while (text.length() > 1 && text[0] == '0') text.offset(1);
      auto start = json.length();
      if (type == 'q') json << '"';
      json << number;
      auto length = json.length() - start;
      if (type == 'q') {
	json << '"';
	++start;
	--length;
      }
      return ZuCSpan{json.data() + start, length} == text;
    }
    case 'i': {
      ZuBox<int64_t> number;
      if (!text || number.scan(text) != int(text.length())) return false;
      json << number; return true;
    }
    case 'Q':
    case 'U':
    case 'S': {
      json << '[';
      bool first = true;
      while (text) {
	if (!first) json << ',';
	first = false;
	auto end = text.find<",">();
	auto item = text;
	if (end >= 0) {
	  item.trunc(unsigned(end));
	  text.offset(unsigned(end) + 1);
	  if (!text) return false;
	} else text = {};
	if (!value(json, item, type == 'Q' ? 'q' : type == 'U' ? 'u' : 's')) return false;
      }
      json << ']'; return true;
    }
    case 'j': {
      // Structured catalog data remains an individual JSON argument.
      String source{text};
      auto parsed = ZfJSON::scan(source.span());
      if (parsed.p<0>() != int(source.length()) || !parsed.p<1>())
	return false;
      auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
      if (roots.length() != 1 ||
	  !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
      ZfJSON::save(json, ZfJSON::Union<>{
	static_cast<const ZfJSON::AnyNode *>(roots[0].ptr())});
      return true;
    }
    default: return false;
  }
}

inline bool request(const ZfCLI::Parser<Options> &parser, int op,
    String &json, String &error)
{
  auto args = spec(op);
  const auto &object = parser.root->data<ZfCLI::AnyNode::Object>();
  unsigned count = 0;
  for (auto fields = args.required; fields; word(fields)) ++count;
  unsigned optional = 0;
  for (auto fields = args.positional; fields; word(fields)) ++optional;
  auto cmdArgs = commandArgs(op);
  if (parser.argc < count + cmdArgs + 1 ||
      parser.argc > count + optional + cmdArgs + 1) {
    error = "expected ";
    error << count;
    if (optional) error << " to " << count + optional;
    error << " positional argument(s): " << args.required;
    if (optional) error << " [" << args.positional << ']';
    return false;
  }
  // Only global controls and this command's optional fields may be named.
  auto i = object.citer();
  while (auto item = i()) {
    auto key = item->key();
    if (key == "config" || key == "command" || key == "if-match" ||
	key == "if-none-match" || key == "idempotence" ||
	key == "help" || key == "no-browser" || key == "arg10" ||
	(key.length() == 4 && key[0] == 'a' && key[1] == 'r' &&
	 key[2] == 'g' && key[3] >= '1' && key[3] <= '9')) continue;
    bool allowed = false;
    for (auto fields = args.optional; fields;) {
      if (option(name(word(fields))) == key) { allowed = true; break; }
    }
    if (!allowed) {
      error = "unexpected option for command: "; error << key;
      return false;
    }
  }
  json = "{";
  bool first = true;
  auto emit = [&json, &error, &first](ZuCSpan entry,
      const ZfCLI::AnyNode *node) {
    if (!node || !node->has<ZfCLI::AnyNode::String>()) return false;
    if (!first) json << ',';
    first = false;
    ZfJSON::quote(json, name(entry));
    json << ':';
    if (!value(json, node->data<ZfCLI::AnyNode::String>(),
	entry[entry.length() - 1])) {
      error = "invalid value for "; error << name(entry);
      return false;
    }
    return true;
  };
  unsigned pos = cmdArgs - 1;
  for (auto fields = args.required; fields;) {
    String key{"arg"}; key << ++pos;
    if (!emit(word(fields), field(object, key))) return false;
  }
  for (auto fields = args.positional; fields;) {
    auto entry = word(fields);
    String key{"arg"}; key << ++pos;
    if (auto node = field(object, key))
      if (!emit(entry, node)) return false;
  }
  for (auto fields = args.optional; fields;) {
    auto entry = word(fields);
    if (auto node = field(object, option(name(entry))))
      if (!emit(entry, node)) return false;
  }
  json << '}';
  return true;
}

} // namespace ZumCLI

#endif /* zum_cli_HH */
