"""Focused SQLite federation fixture using an independent local TLS IdP."""

import json
import hashlib
import http.client
import os
import re
import secrets
import shutil
import ssl
import tempfile
import time
from http.cookies import SimpleCookie
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlsplit

from zumhttp import Authenticator, Fixture, b64
from zumidp import Provider, TLSProxy


class TLSFixture(Fixture):
    def __init__(self, directory, provider):
        super().__init__(directory)
        self.proxy = TLSProxy(self.port, provider)
        self.origin = self.proxy.origin
        self.authenticator = Authenticator(self.origin)
        self.tls = ssl.create_default_context(cafile=provider.ca_path)
        self.ca_path = provider.ca_path

    def connection(self):
        return http.client.HTTPSConnection("localhost", self.proxy.port,
                                           timeout=30, context=self.tls)


def exercise(fixture, provider):
    fixture.start()
    fixture.enroll()
    fixture.admin_cli()
    admin = fixture.login()

    def create(path, value):
        return fixture.request("POST", path, value, token=admin, status=201,
            headers={"Idempotency-Key": secrets.token_hex(16)})[0]["item"]

    def query(path):
        return fixture.request("GET", path, token=admin)[0]["items"]

    audience_uri = "https://federation.example/api"
    app = create("/admin/apps", {"name": "federation",
                                  "audience": audience_uri})
    app_id = app["appID"]
    provider.redirects.add(fixture.issuer(app_id) + "/v1/oidc/callback")
    prefix = "/admin/apps/" + app_id
    action = create(prefix + "/actions", {"name": "ping", "label": "Ping"})
    role = create(prefix + "/roles", {"name": "ping", "label": "Ping"})
    fixture.request("PUT", prefix + "/roles/" + role["id"] + "/actions",
                    {"actionIDs": [action["id"]]}, token=admin,
                    headers={"If-Match": role["etag"]})
    client = create("/admin/clients", {"appID": app_id, "label": "Independent federation client",
        "type": "native", "redirectURIs": ["http://127.0.0.1:49152/callback"],
        "grants": 5, "refreshAllowed": True, "identityScopes": ["openid"]})
    fixture.request("PUT", prefix + "/client-access/" + client["id"], {
        "roleIDs": [role["id"]]},
        token=admin, headers={"If-None-Match": "*"}, status=201)
    upstream = create("/admin/providers", {"name": "independent-fixture",
        "issuer": provider.issuer, "clientID": provider.client_id,
        "clientSecret": provider.client_secret, "scopes": ["openid", "roles"],
        "roleClaim": "roles", "claimSource": "IDToken"})
    assert "clientSecret" not in upstream
    def delegate(target_app, target_role, provider_id=None):
        if provider_id is None:
            provider_id = upstream["id"]
        target = "/admin/apps/" + target_app
        for value in ("operators", "readers"):
            fixture.request("PUT", target + "/role-mappings/" + provider_id + "/" + b64(value.encode()),
                {"roleID": target_role}, token=admin, headers={"If-None-Match": "*"}, status=201)
        set_policy(target_app, provider_id=provider_id)

    def set_policy(target_app, assignment_max_age=300,
                   eligibility_mode="MappedRole", eligibility_values=(),
                   provider_id=None):
        if provider_id is None:
            provider_id = upstream["id"]
        target = "/admin/apps/" + target_app
        policy = query("/admin/auth-policies?appID=" + target_app)[0]
        replacement = {
            "providerID": provider_id, "localFirst": True, "eligibilityMode": "MappedRole",
            "assignmentMaxAge": assignment_max_age, "sessionIdle": 1800, "sessionAbsolute": 43200,
            "tokenLifetime": 300, "consentPolicy": "Explicit", "state": "Active"}
        replacement["eligibilityMode"] = eligibility_mode
        if eligibility_mode == "ClaimValues":
            replacement["eligibilityClaim"] = "roles"
            replacement["eligibilityValues"] = list(eligibility_values)
        fixture.request("PUT", target + "/auth-policy", replacement,
            token=admin, headers={"If-Match": policy["etag"]})

    delegate(app_id, role["id"])
    assert not any(user["source"] == "External" for user in query("/admin/users?limit=1000"))
    fixture.cookies = SimpleCookie()
    tokens = fixture.login(client["id"], "openid ping", app_id,
                           return_tokens=True, login="external-user")
    fixture.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
    users = [user for user in query("/admin/users?limit=1000") if user["source"] == "External"]
    assert len(users) == 1
    members = query(prefix + "/memberships?userID=" + users[0]["id"])
    assert not members, "upstream authority must not create local role assignments"
    grants = [grant for grant in query("/admin/grants?limit=1000")
              if grant["userID"] == users[0]["id"] and grant["appID"] == app_id]
    # Completed authorization does not retain a Grant: refresh-family state
    # lives in the dedicated refresh table and mapped authority is recorded in
    # evidence below.
    assert not grants
    def evidence():
        all_rows = query("/admin/evidence?limit=1000")
        rows = [row for row in all_rows
                if row["appID"] == app_id and row["userID"] == users[0]["id"]]
        assert len(rows) == 1, f"external evidence mismatch: rows={rows!r} all={all_rows!r}"
        return rows[0]

    observed = evidence()
    assert observed["eligible"] and observed["roleValues"] == provider.roles
    assert fixture.verify_jwt(tokens["access_token"])["exp"] <= observed["deadline"]
    assert all(provider.calls.get(path, 0) for path in
               ("/.well-known/openid-configuration", "/authorize", "/token", "/jwks"))
    # No local account/password fallback is needed for the external user; the
    # bootstrap administrator remains local even when the upstream is unavailable.
    before = dict(provider.calls)
    provider.outage = True
    refreshed, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": tokens["refresh_token"]}, form=True)
    fixture.verify_access(refreshed["access_token"], client["id"], app_id, audience_uri, ["ping"])
    assert fixture.verify_jwt(refreshed["access_token"])["exp"] <= observed["deadline"]
    assert evidence() == observed, "refresh must not extend upstream assignment freshness"
    fixture.cookies = SimpleCookie()
    fixture.login()
    assert provider.calls == before
    provider.outage = False
    fixture.cookies = SimpleCookie()
    tokens = fixture.login(client["id"], "openid ping", app_id,
                           return_tokens=True, login="external-user")
    fixture.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
    assert int(evidence()["version"]) == int(observed["version"]) + 1
    assert len([user for user in query("/admin/users?limit=1000")
                if user["source"] == "External"]) == 1

    def ping_sso(app, catalog, service_port, user):
        provider.redirects.add(fixture.issuer(app["appID"]) +
                               "/v1/oidc/callback")
        policy = query("/admin/auth-policies?appID=" + app["appID"])[0]
        if policy["providerID"] != upstream["id"]:
            delegate(app["appID"], catalog["roleQuery"][0]["id"])
        assert not query("/admin/apps/" + app["appID"] + "/memberships?userID=" + users[0]["id"])
        fixture.ping_client(service_port, *user, login="external-user")

    fixture.ping_service(after_login=ping_sso)
    fixture.stop()
    fixture.start()
    tokens = fixture.login(client["id"], "openid ping", app_id,
                           return_tokens=True, login="external-user")
    fixture.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
    persisted = [user for user in query("/admin/users?limit=1000") if user["source"] == "External"]
    assert len(persisted) == 1 and persisted[0]["id"] == users[0]["id"]

    def denied_login(target_client=client, target_app=app_id):
        fixture.cookies = SimpleCookie()
        verifier = secrets.token_urlsafe(32)
        page, _ = fixture.request("GET", fixture.oauth(target_app, "authorize") +
                                  "?" + urlencode({
            "response_type": "code", "client_id": target_client["id"],
            "redirect_uri": "http://127.0.0.1:49152/callback", "scope": "openid ping",
            "state": secrets.token_urlsafe(24),
            "nonce": secrets.token_urlsafe(24), "code_challenge_method": "S256",
            "code_challenge": b64(hashlib.sha256(verifier.encode()).digest())}))
        ceremony = re.search(r"const id='([^']+)'", page)
        assert ceremony
        _, headers = fixture.request("POST", fixture.oauth(target_app, "login"), {
            "id": ceremony[1], "login": "external-user"}, form=True, status=302)
        callback = urlsplit(provider.authorize(headers["location"]))
        return fixture.request("GET", callback.path + "?" + callback.query, status=400)[0]

    # Provider identity and assignment state is qualified by both application
    # and provider. A second enrolled provider may use the same upstream issuer
    # and subject without inheriting the first provider's mappings or evidence.
    isolated_uri = "https://provider-isolation.example/api"
    isolated_app = create("/admin/apps", {
        "name": "provider-isolation",
        "audience": isolated_uri})
    isolated_id = isolated_app["appID"]
    provider.redirects.add(fixture.issuer(isolated_id) + "/v1/oidc/callback")
    isolated_prefix = "/admin/apps/" + isolated_id
    isolated_action = create(isolated_prefix + "/actions", {"name": "ping"})
    isolated_role = create(isolated_prefix + "/roles", {"name": "ping"})
    fixture.request("PUT", isolated_prefix + "/roles/" + isolated_role["id"] + "/actions",
                    {"actionIDs": [isolated_action["id"]]}, token=admin,
                    headers={"If-Match": isolated_role["etag"]})
    isolated_client = create("/admin/clients", {
        "appID": isolated_id, "label": "Provider isolation client", "type": "native",
        "redirectURIs": ["http://127.0.0.1:49152/callback"], "grants": 5,
        "refreshAllowed": True, "identityScopes": ["openid"]})
    fixture.request("PUT", isolated_prefix + "/client-access/" + isolated_client["id"], {
        "roleIDs": [isolated_role["id"]]},
        token=admin, headers={"If-None-Match": "*"}, status=201)
    isolated_provider = create("/admin/providers", {
        "name": "second-fixture", "issuer": provider.issuer,
        "clientID": provider.client_id, "clientSecret": provider.client_secret,
        "scopes": ["openid", "roles"], "roleClaim": "roles",
        "claimSource": "IDToken"})
    fixture.request("PUT", isolated_prefix + "/role-mappings/" +
                    isolated_provider["id"] + "/" + b64(b"outsiders"),
                    {"roleID": isolated_role["id"]}, token=admin,
                    headers={"If-None-Match": "*"}, status=201)
    set_policy(isolated_id, provider_id=isolated_provider["id"])
    assert denied_login(isolated_client, isolated_id)["error"] == "access_denied"
    isolated_maps = query(isolated_prefix + "/role-mappings?limit=1000")
    assert len(isolated_maps) == 1
    assert isolated_maps[0]["providerID"] == isolated_provider["id"]
    delegate(isolated_id, isolated_role["id"], isolated_provider["id"])
    fixture.cookies = SimpleCookie()
    isolated_tokens = fixture.login(isolated_client["id"], "openid ping", isolated_id,
                                    return_tokens=True, login="external-user")
    fixture.verify_access(isolated_tokens["access_token"], isolated_client["id"],
                          isolated_id, isolated_uri, ["ping"])
    for wrong_app, wrong_client, refresh in (
            (isolated_id, client["id"], tokens["refresh_token"]),
            (app_id, isolated_client["id"], isolated_tokens["refresh_token"])):
        denied, _ = fixture.request("POST", fixture.oauth(wrong_app, "token"), {
            "grant_type": "refresh_token", "client_id": wrong_client,
            "refresh_token": refresh}, form=True, status=401)
        assert denied["error"] == "invalid_client"
    primary_keys = fixture.request("GET", fixture.oauth(app_id, "keys"))[0]["keys"]
    isolated_keys = fixture.request("GET", fixture.oauth(isolated_id, "keys"))[0]["keys"]
    assert {key["kid"] for key in primary_keys}.isdisjoint(
        key["kid"] for key in isolated_keys)
    isolated_evidence = [row for row in query("/admin/evidence?limit=1000")
                         if row["appID"] == isolated_id]
    assert len(isolated_evidence) == 1
    assert isolated_evidence[0]["providerID"] == isolated_provider["id"]
    assert isolated_evidence[0]["userID"] != users[0]["id"]
    assert not query(isolated_prefix + "/memberships?userID=" +
                     isolated_evidence[0]["userID"])
    assert not [row for row in query("/admin/evidence?limit=1000")
                if row["appID"] == isolated_id and row["providerID"] == upstream["id"]]
    assert not [row for row in query(isolated_prefix + "/role-mappings?limit=1000")
                if row["providerID"] == upstream["id"]]

    # ClaimValues eligibility is independent from role mapping. Accept either a
    # scalar or array claim, but reject unknown values and non-string claim types.
    set_policy(app_id, eligibility_mode="ClaimValues", eligibility_values=["readers"])
    fixture.cookies = SimpleCookie()
    claim_tokens = fixture.login(client["id"], "openid ping", app_id,
                                 return_tokens=True, login="external-user")
    fixture.verify_access(claim_tokens["access_token"], client["id"], app_id,
                          audience_uri, ["ping"])
    set_policy(app_id, eligibility_mode="ClaimValues", eligibility_values=["administrators"])
    assert denied_login()["error"] == "access_denied"
    set_policy(app_id, eligibility_mode="ClaimValues", eligibility_values=["readers"])
    provider.id_overrides = {"roles": 7}
    assert denied_login()["error"] == "access_denied"
    provider.id_overrides = {"roles": "readers"}
    fixture.cookies = SimpleCookie()
    claim_tokens = fixture.login(client["id"], "openid ping", app_id,
                                 return_tokens=True, login="external-user")
    fixture.verify_access(claim_tokens["access_token"], client["id"], app_id,
                          audience_uri, ["ping"])
    provider.id_overrides = {}
    set_policy(app_id)

    # The same identity and app mapping must work when the administrator selects
    # UserInfo as the authoritative role-claim source instead of the ID token.
    current_provider = next(row for row in query("/admin/providers?limit=1000")
                            if row["id"] == upstream["id"])
    fixture.request("PATCH", "/admin/providers/" + upstream["id"],
                    {"claimSource": "UserInfo"}, token=admin,
                    headers={"If-Match": current_provider["etag"]})
    before_userinfo = provider.calls.get("/userinfo", 0)
    before_evidence = evidence()
    provider.userinfo_roles = ["readers"]
    fixture.cookies = SimpleCookie()
    tokens = fixture.login(client["id"], "openid ping", app_id,
                           return_tokens=True, login="external-user")
    fixture.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
    assert provider.calls.get("/userinfo", 0) == before_userinfo + 1
    assert int(evidence()["version"]) == int(before_evidence["version"]) + 1
    assert evidence()["roleValues"] == ["readers"], "UserInfo roles must replace ID-token roles"
    assert not query(prefix + "/memberships?userID=" + users[0]["id"])

    # UserInfo cannot substitute a different subject for the signed ID token.
    before_evidence = evidence()
    before_users = query("/admin/users?limit=1000")
    provider.userinfo_subject = "different-user"
    assert denied_login()["error"]
    assert evidence() == before_evidence
    assert query("/admin/users?limit=1000") == before_users
    provider.userinfo_subject = None
    # A valid upstream signature does not replace issuer/client/transaction
    # binding. Reject these claims before fetching UserInfo or changing evidence.
    for claim, value in (("nonce", "wrong-transaction"),
                         ("aud", "different-client"),
                         ("iss", "https://different-issuer.invalid")):
        before_evidence = evidence()
        before_userinfo = provider.calls.get("/userinfo", 0)
        provider.id_overrides = {claim: value}
        assert denied_login()["error"], "invalid upstream " + claim + " was accepted"
        assert evidence() == before_evidence
        assert provider.calls.get("/userinfo", 0) == before_userinfo
    provider.id_overrides = {}
    # Expire evidence using the real configured deadline, not session ageing or
    # direct DB edits. Waiting here exercises protocol time, not async completion.
    set_policy(app_id, assignment_max_age=5)
    fixture.cookies = SimpleCookie()
    expiring = fixture.login(client["id"], "openid ping", app_id,
                             return_tokens=True, login="external-user")
    short_evidence = evidence()
    deadline = int(short_evidence["deadline"])
    assert fixture.verify_jwt(expiring["access_token"])["exp"] <= deadline
    sessions = [row for row in query("/admin/sessions?limit=1000")
                if row["userID"] == users[0]["id"]]
    assert any(int(row["idleDeadline"]) > deadline and
               int(row["absoluteDeadline"]) > deadline for row in sessions)
    remaining = deadline + 1 - time.time()
    assert remaining < 10
    if remaining > 0:
        time.sleep(remaining)
    upstream_calls = dict(provider.calls)
    denied, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": expiring["refresh_token"]}, form=True, status=400)
    assert denied["error"] == "invalid_grant"
    assert provider.calls == upstream_calls, "refresh must not silently renew upstream authority"
    assert evidence() == short_evidence
    set_policy(app_id)
    fixture.cookies = SimpleCookie()
    tokens = fixture.login(client["id"], "openid ping", app_id,
                           return_tokens=True, login="external-user")
    fixture.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
    assert int(evidence()["deadline"]) > deadline

    # Fresh evidence is not a frozen role assignment: current app mappings and
    # role state still constrain refresh without another upstream login.
    mapped_evidence = evidence()
    mappings = query(prefix + "/role-mappings?limit=1000")
    assert len(mappings) == 2
    for mapping in mappings:
        fixture.request("DELETE", prefix + "/role-mappings/" + mapping["providerID"] +
                        "/" + b64(mapping["value"].encode()), token=admin,
                        headers={"If-Match": mapping["etag"]})
    before_calls = dict(provider.calls)
    narrowed, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": tokens["refresh_token"]}, form=True)
    fixture.verify_access(narrowed["access_token"], client["id"], app_id, audience_uri, [])
    assert evidence() == mapped_evidence and provider.calls == before_calls
    for mapping in mappings:
        fixture.request("PUT", prefix + "/role-mappings/" + mapping["providerID"] +
                        "/" + b64(mapping["value"].encode()),
                        {"roleID": mapping["roleID"]}, token=admin,
                        headers={"If-None-Match": "*"}, status=201)
    still_narrowed, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": narrowed["refresh_token"]}, form=True)
    fixture.verify_access(still_narrowed["access_token"], client["id"], app_id, audience_uri, [])
    fixture.cookies = SimpleCookie()
    tokens = fixture.login(client["id"], "openid ping", app_id,
                           return_tokens=True, login="external-user")
    fixture.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
    active_role = next(row for row in query(prefix + "/roles?limit=1000") if row["id"] == role["id"])
    fixture.request("PUT", prefix + "/roles/" + role["id"] + "/state",
                    {"state": "Disabled"}, token=admin, headers={"If-Match": active_role["etag"]})
    mapped_evidence = evidence()
    before_calls = dict(provider.calls)
    denied, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": tokens["refresh_token"]}, form=True, status=400)
    assert denied["error"] == "invalid_scope"
    assert evidence() == mapped_evidence and provider.calls == before_calls
    disabled_role = next(row for row in query(prefix + "/roles?limit=1000") if row["id"] == role["id"])
    fixture.request("PUT", prefix + "/roles/" + role["id"] + "/state",
                    {"state": "Active"}, token=admin, headers={"If-Match": disabled_role["etag"]})
    restored, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": tokens["refresh_token"]}, form=True)
    fixture.verify_access(restored["access_token"], client["id"], app_id, audience_uri, ["ping"])
    fixture.cookies = SimpleCookie()
    tokens = fixture.login(client["id"], "openid ping", app_id,
                           return_tokens=True, login="external-user")
    fixture.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
    # Fresh authoritative claims with no mapped role deny admission and invalidate
    # the earlier refresh authority, even though the global projected user exists.
    provider.roles = ["unmapped"]
    provider.userinfo_roles = None
    denied = denied_login()
    assert denied["error"] == "access_denied" and not evidence()["eligible"]
    denied, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": tokens["refresh_token"]}, form=True, status=400)
    assert denied["error"] == "invalid_grant"
    # A newly invited local identity with the same login takes routing priority;
    # it must not inherit the external identity or its application assignments.
    provider.roles = ["readers"]
    fixture.cookies = SimpleCookie()
    tokens = fixture.login(client["id"], "openid ping", app_id,
                           return_tokens=True, login="external-user")
    projected_name = users[0]["name"]
    session_cookies = SimpleCookie(fixture.cookies.output(header="", sep=";"))
    # An untrusted browser hint is not a projected identity name.
    assert projected_name != "external-user"
    create("/admin/users", {"name": "external-user"})
    tokens, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": tokens["refresh_token"]}, form=True)
    external_query = "/admin/users?id=" + users[0]["id"]
    before_external = query(external_query)[0]
    local = create("/admin/users", {"name": projected_name})
    after_external = query(external_query)[0]
    assert int(after_external["authVersion"]) == int(before_external["authVersion"]) + 1
    assert int(after_external["version"]) == int(before_external["version"]) + 1
    assert after_external["name"] == before_external["name"]
    assert after_external["source"] == "External"
    local_query = "/admin/users?" + urlencode({"name": projected_name, "source": "Local"})
    local_rows = query(local_query)
    assert len(local_rows) == 1 and local_rows[0]["id"] != users[0]["id"]
    assert not query(prefix + "/memberships?userID=" + local_rows[0]["id"])
    before_calls = dict(provider.calls)
    fixture.cookies = SimpleCookie()
    page, _ = fixture.request("GET", fixture.oauth(app_id, "authorize") +
                              "?" + urlencode({
        "response_type": "code", "client_id": client["id"],
        "redirect_uri": "http://127.0.0.1:49152/callback", "scope": "openid ping",
        "state": secrets.token_urlsafe(24),
        "nonce": secrets.token_urlsafe(24), "code_challenge_method": "S256",
        "code_challenge": b64(hashlib.sha256(secrets.token_bytes(32)).digest())}))
    ceremony = re.search(r"const id='([^']+)'", page)
    assert ceremony
    page, headers = fixture.request("POST", fixture.oauth(app_id, "login"), {
        "id": ceremony[1], "login": projected_name}, form=True)
    assert "navigator.credentials" in page and "location" not in headers
    assert provider.calls == before_calls, "local account must suppress upstream fallback"
    denied, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "refresh_token", "client_id": client["id"],
        "refresh_token": tokens["refresh_token"]}, form=True, status=400)
    assert denied["error"] == "invalid_grant"
    assert provider.calls == before_calls
    fixture.cookies = session_cookies
    _, headers = fixture.request("GET", fixture.oauth(app_id, "authorize") +
                                 "?" + urlencode({
        "response_type": "code", "client_id": client["id"],
        "redirect_uri": "http://127.0.0.1:49152/callback", "scope": "openid ping",
        "state": "conflict-check", "prompt": "none",
        "nonce": secrets.token_urlsafe(24), "code_challenge_method": "S256",
        "code_challenge": b64(hashlib.sha256(secrets.token_bytes(32)).digest())}), status=302)
    result = parse_qs(urlsplit(headers["location"]).query)
    assert result.get("error") == ["login_required"] and "code" not in result
    assert result["state"] == ["conflict-check"] and provider.calls == before_calls
    assert query(local_query) == local_rows
    assert local["enrollmentURL"]
    fixture.proxy.close()
    fixture.stop()
    assert provider.client_secret not in (fixture.directory / "server.log").read_text()


def main():
    for key in ("ZDB_MODULE", "ZDB_CONNECT"):
        if not os.environ.get(key):
            raise AssertionError("set " + key + " for a fresh SQLite federation fixture")
    directory = tempfile.mkdtemp(prefix="zum-federation-")
    provider = None
    fixture = None
    try:
        provider = Provider(directory)
        fixture = TLSFixture(directory, provider)
        fixture.idp = provider
        fixture.node_config = Path(directory) / "node.cf"
        fixture.node_config.write_text(Path(__file__).with_name("zumd.cf").read_text() +
            ",\noidc: {caPath: " + json.dumps(str(provider.ca_path)) + "}\n")
        exercise(fixture, provider)
    except BaseException:
        print("# failed federation diagnostics retained in " + directory, flush=True)
        raise
    else:
        shutil.rmtree(directory)
    finally:
        if fixture:
            fixture.proxy.close()
        if fixture and fixture.process:
            fixture.stop()
        if provider:
            provider.close()


if __name__ == "__main__":
    main()
