//  -*- mode:c++; indent-tabs-mode:t; tab-width:4; c-basic-offset:4; -*-
//  vi: noet ts=4 sw=4 cino=+0,(s,l1,m1,j1,U1,W4

package main

import (
	"container/heap"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"html/template"
	"io"
	"log"
	"mime"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/golang-jwt/jwt/v5"
)

const (
	applicationID = "ping"
	pingScope     = "ping"
	reqBodyMax    = 1 << 20
	respBodyMax   = 1 << 20
	redirectMax   = 2048
	stateMax      = 256
	sessionMax    = 1024
	codeMax       = 4096
	tokenMax      = 4096
)

type durationValue struct{ d time.Duration }

func (v *durationValue) String() string { return v.d.String() }
func (v *durationValue) Set(s string) error {
	if len(s) < 2 || strings.ContainsAny(s[:len(s)-1], ".+-") {
		return errors.New("expected unsigned integer and s, m, or h")
	}
	n := int64(0)
	for _, c := range s[:len(s)-1] {
		if c < '0' || c > '9' || n > (1<<63-1-int64(c-'0'))/10 {
			return errors.New("invalid or overflowing duration")
		}
		n = n*10 + int64(c-'0')
	}
	unit := time.Second
	switch s[len(s)-1] {
	case 's':
	case 'm':
		unit = time.Minute
	case 'h':
		unit = time.Hour
	default:
		return errors.New("duration suffix must be s, m, or h")
	}
	if n == 0 || n > int64((1<<63-1)/unit) {
		return errors.New("invalid or overflowing duration")
	}
	v.d = time.Duration(n) * unit
	return nil
}

type oauthError struct {
	Error       string `json:"error"`
	Description string `json:"error_description,omitempty"`
}

type tokenResponse struct {
	AccessToken  string `json:"access_token"`
	TokenType    string `json:"token_type"`
	ExpiresIn    int64  `json:"expires_in"`
	RefreshToken string `json:"refresh_token,omitempty"`
	Scope        string `json:"scope"`
}

type accessClaims struct {
	ClientID string `json:"client_id"`
	Scope    string `json:"scope"`
	jwt.RegisteredClaims
}

type authorization struct {
	issuer, clientID, redirectURI, scope, state, challenge string
}
type browserSession struct {
	authorization
	expires int64
	expiry  *expiryEntry
}
type codeGrant struct {
	authorization
	subject string
	expires int64
	expiry  *expiryEntry
}
type refreshGrant struct {
	family, clientID, subject, scope string
	expires                          int64
	active                           bool
	expiry                           *expiryEntry
}
type familyState struct {
	expires int64
	revoked bool
	expiry  *expiryEntry
}
type accessState struct {
	family  string
	expires int64
	revoked bool
	expiry  *expiryEntry
}

type expiryEntry struct {
	key     [32]byte
	text    string
	expires int64
	kind    uint8
	index   int
}

type expiryQueue []*expiryEntry

func (q expiryQueue) Len() int           { return len(q) }
func (q expiryQueue) Less(i, j int) bool { return q[i].expires < q[j].expires }
func (q expiryQueue) Swap(i, j int) {
	q[i], q[j] = q[j], q[i]
	q[i].index, q[j].index = i, j
}
func (q *expiryQueue) Push(value any) {
	entry := value.(*expiryEntry)
	entry.index = len(*q)
	*q = append(*q, entry)
}
func (q *expiryQueue) Pop() any {
	old := *q
	entry := old[len(old)-1]
	old[len(old)-1] = nil
	entry.index = -1
	*q = old[:len(old)-1]
	return entry
}

const (
	expirySession = iota
	expiryCode
	expiryRefresh
	expiryFamily
	expiryAccess
)

type app struct {
	key                             *ecdsa.PrivateKey
	keyID                           string
	user, password                  string
	accessLifetime, refreshLifetime time.Duration
	requestLimit                    uint64
	verbose                         bool
	mu                              sync.Mutex
	sessions                        map[[32]byte]browserSession
	codes                           map[[32]byte]codeGrant
	refreshTokens                   map[[32]byte]refreshGrant
	families                        map[string]familyState
	accessTokens                    map[string]accessState
	expiries                        expiryQueue
	authorize, token, refresh       atomic.Uint64
	revoke, pong                    atomic.Uint64
	stopOnce                        sync.Once
	stop                            chan struct{}
	fatal                           atomic.Bool
}

func (a *app) scheduleLocked(kind uint8, key [32]byte, text string, expires int64) *expiryEntry {
	entry := &expiryEntry{key: key, text: text, expires: expires, kind: kind, index: -1}
	heap.Push(&a.expiries, entry)
	return entry
}

func (a *app) unscheduleLocked(entry *expiryEntry) {
	if entry != nil && entry.index >= 0 {
		heap.Remove(&a.expiries, entry.index)
	}
}

func (a *app) reapLocked(now int64) {
	for len(a.expiries) != 0 && a.expiries[0].expires <= now {
		entry := heap.Pop(&a.expiries).(*expiryEntry)
		switch entry.kind {
		case expirySession:
			delete(a.sessions, entry.key)
		case expiryCode:
			delete(a.codes, entry.key)
		case expiryRefresh:
			delete(a.refreshTokens, entry.key)
		case expiryFamily:
			delete(a.families, entry.text)
		case expiryAccess:
			delete(a.accessTokens, entry.text)
		}
	}
}

var loginPage = template.Must(template.New("login").Parse(`<!doctype html>
<html><head><meta charset="utf-8"><title>Authorize ping</title></head>
<body><main><h1>Authorize ping</h1><form method="post">
<label>Username <input name="username" autocomplete="username" required></label>
<label>Password <input name="password" type="password" autocomplete="current-password" required></label>
<button name="decision" value="approve" type="submit">Approve</button>
<button name="decision" value="deny" type="submit">Deny</button>
</form></main></body></html>`))

func digest(value string) [32]byte { return sha256.Sum256([]byte(value)) }

func opaque() (string, error) {
	var value [32]byte
	if _, err := io.ReadFull(rand.Reader, value[:]); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(value[:]), nil
}

func issuer(r *http.Request) string {
	return "https://" + r.Host + "/oauth2/" + applicationID
}

func writeJSON(w http.ResponseWriter, status int, value any) error {
	data, err := json.Marshal(value)
	if err != nil {
		return err
	}
	if len(data) > respBodyMax {
		return errors.New("response too large")
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Length", fmt.Sprint(len(data)))
	w.WriteHeader(status)
	n, err := w.Write(data)
	if err == nil && n != len(data) {
		err = io.ErrShortWrite
	}
	return err
}

func oauthFailure(w http.ResponseWriter, status int, code, description string) {
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Pragma", "no-cache")
	_ = writeJSON(w, status, oauthError{code, description})
}

func exactValues(values url.Values, required, optional []string) bool {
	allowed := make(map[string]bool, len(required)+len(optional))
	for _, key := range required {
		allowed[key] = true
		if len(values[key]) != 1 || values.Get(key) == "" {
			return false
		}
	}
	for _, key := range optional {
		allowed[key] = true
		if len(values[key]) > 1 {
			return false
		}
	}
	for key := range values {
		if !allowed[key] {
			return false
		}
	}
	return true
}

func loopbackRedirect(raw string) bool {
	if len(raw) > redirectMax {
		return false
	}
	u, err := url.Parse(raw)
	if err != nil || u.Scheme != "http" || u.User != nil || u.RawQuery != "" ||
		u.Fragment != "" || u.Path != "/callback" || u.Port() == "" {
		return false
	}
	host := u.Hostname()
	return host == "127.0.0.1" || host == "::1"
}

func validClient(clientID, redirect string) bool {
	return (clientID == "zrest-native" || clientID == "zrest-go") &&
		loopbackRedirect(redirect)
}

func validScope(raw string) (string, bool) {
	fields := strings.Fields(raw)
	if len(fields) == 0 || strings.Join(fields, " ") != raw {
		return "", false
	}
	seen := make(map[string]bool, len(fields))
	for _, scope := range fields {
		if (scope != pingScope && scope != "other") || seen[scope] {
			return "", false
		}
		seen[scope] = true
	}
	return raw, true
}

func scopeSubset(requested, granted string) bool {
	allowed := make(map[string]bool)
	for _, scope := range strings.Fields(granted) {
		allowed[scope] = true
	}
	for _, scope := range strings.Fields(requested) {
		if !allowed[scope] {
			return false
		}
	}
	return requested != ""
}

func hasScope(granted, required string) bool {
	for _, scope := range strings.Fields(granted) {
		if scope == required {
			return true
		}
	}
	return false
}

func (a *app) fail() {
	a.fatal.Store(true)
	a.stopOnce.Do(func() { close(a.stop) })
}

func (a *app) wrote(event string, counter *atomic.Uint64) {
	counter.Add(1)
	if a.verbose {
		log.Printf("event=%s", event)
	}
	if event == "revoke" && a.requestLimit != 0 && a.pong.Load() >= a.requestLimit {
		a.stopOnce.Do(func() { close(a.stop) })
	}
}

func (a *app) metadataHandler(w http.ResponseWriter, r *http.Request) {
	base := issuer(r)
	value := struct {
		Issuer, Authorization, Token, Revoke, Keys string
		Responses, Grants, Challenges, Scopes      []string
		TokenAuth, RevokeAuth                      []string
	}{base, base + "/v1/authorize", base + "/v1/token",
		base + "/v1/revoke", base + "/v1/keys",
		[]string{"code"}, []string{"authorization_code", "refresh_token"},
		[]string{"S256"}, []string{pingScope, "other"}, []string{"none"}, []string{"none"}}
	wire := map[string]any{
		"issuer": value.Issuer, "authorization_endpoint": value.Authorization,
		"token_endpoint": value.Token, "revocation_endpoint": value.Revoke,
		"jwks_uri": value.Keys, "response_types_supported": value.Responses,
		"grant_types_supported":                      value.Grants,
		"code_challenge_methods_supported":           value.Challenges,
		"scopes_supported":                           value.Scopes,
		"token_endpoint_auth_methods_supported":      value.TokenAuth,
		"revocation_endpoint_auth_methods_supported": value.RevokeAuth,
	}
	w.Header().Set("Cache-Control", "no-store")
	if writeJSON(w, http.StatusOK, wire) != nil {
		a.fail()
	}
}

func authorizationFailure(w http.ResponseWriter, redirect, state, iss, code string) {
	if redirect == "" {
		oauthFailure(w, http.StatusBadRequest, code, "invalid authorization request")
		return
	}
	u, _ := url.Parse(redirect)
	query := u.Query()
	query.Set("error", code)
	if state != "" {
		query.Set("state", state)
	}
	query.Set("iss", iss)
	u.RawQuery = query.Encode()
	http.Redirect(w, &http.Request{}, u.String(), http.StatusFound)
}

func (a *app) authorizeGET(w http.ResponseWriter, r *http.Request) {
	values, err := url.ParseQuery(r.URL.RawQuery)
	shape := err == nil && exactValues(values, []string{"response_type", "client_id",
		"redirect_uri", "scope", "state", "code_challenge", "code_challenge_method"}, nil)
	clientID, redirect, state := values.Get("client_id"), values.Get("redirect_uri"), values.Get("state")
	redirectOK := validClient(clientID, redirect)
	scope, scopeOK := validScope(values.Get("scope"))
	if !shape || values.Get("response_type") != "code" || !scopeOK ||
		values.Get("code_challenge_method") != "S256" || len(values.Get("code_challenge")) != 43 ||
		len(state) > stateMax {
		if !redirectOK {
			redirect = ""
		}
		authorizationFailure(w, redirect, state, issuer(r), "invalid_request")
		return
	}
	if !redirectOK {
		authorizationFailure(w, "", "", issuer(r), "unauthorized_client")
		return
	}
	session, err := opaque()
	if err != nil {
		a.fail()
		oauthFailure(w, http.StatusInternalServerError, "server_error", "authorization unavailable")
		return
	}
	now := time.Now().Unix()
	expires := now + int64((5*time.Minute)/time.Second)
	key := digest(session)
	a.mu.Lock()
	a.reapLocked(now)
	if len(a.sessions) >= sessionMax {
		a.mu.Unlock()
		oauthFailure(w, http.StatusServiceUnavailable, "temporarily_unavailable", "authorization busy")
		return
	}
	expiry := a.scheduleLocked(expirySession, key, "", expires)
	a.sessions[key] = browserSession{authorization{issuer(r), clientID,
		redirect, scope, state, values.Get("code_challenge")},
		expires, expiry}
	a.mu.Unlock()
	http.SetCookie(w, &http.Cookie{Name: "__Host-zrest_auth", Value: session, Path: "/",
		Secure: true, HttpOnly: true, SameSite: http.SameSiteLaxMode, MaxAge: 300})
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if loginPage.Execute(w, nil) != nil {
		a.fail()
	}
}

func (a *app) authorizePOST(w http.ResponseWriter, r *http.Request) {
	values, ok := parseForm(w, r)
	if !ok || !exactValues(values, []string{"username", "password", "decision"}, nil) {
		oauthFailure(w, http.StatusBadRequest, "invalid_request", "invalid authorization form")
		return
	}
	cookie, err := r.Cookie("__Host-zrest_auth")
	if err != nil {
		oauthFailure(w, http.StatusBadRequest, "invalid_request", "authorization session missing")
		return
	}
	key := digest(cookie.Value)
	now := time.Now().Unix()
	a.mu.Lock()
	a.reapLocked(now)
	session, found := a.sessions[key]
	if found {
		delete(a.sessions, key)
		a.unscheduleLocked(session.expiry)
	}
	a.mu.Unlock()
	http.SetCookie(w, &http.Cookie{Name: "__Host-zrest_auth", Path: "/", Secure: true,
		HttpOnly: true, SameSite: http.SameSiteLaxMode, MaxAge: -1})
	if !found || session.expires <= now {
		oauthFailure(w, http.StatusBadRequest, "invalid_request", "authorization session expired")
		return
	}
	if values.Get("decision") != "approve" || values.Get("username") != a.user ||
		values.Get("password") != a.password {
		authorizationFailure(w, session.redirectURI, session.state, issuer(r), "access_denied")
		return
	}
	code, err := opaque()
	if err != nil {
		a.fail()
		authorizationFailure(w, session.redirectURI, session.state, issuer(r), "server_error")
		return
	}
	expires := now + int64(time.Minute/time.Second)
	key = digest(code)
	a.mu.Lock()
	a.reapLocked(now)
	if len(a.codes) >= codeMax {
		a.mu.Unlock()
		authorizationFailure(w, session.redirectURI, session.state, issuer(r), "temporarily_unavailable")
		return
	}
	expiry := a.scheduleLocked(expiryCode, key, "", expires)
	a.codes[key] = codeGrant{session.authorization, a.user, expires, expiry}
	a.mu.Unlock()
	u, _ := url.Parse(session.redirectURI)
	query := u.Query()
	query.Set("code", code)
	query.Set("state", session.state)
	query.Set("iss", issuer(r))
	u.RawQuery = query.Encode()
	a.wrote("authorize", &a.authorize)
	http.Redirect(w, r, u.String(), http.StatusFound)
}

func (a *app) authorizeHandler(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		a.authorizeGET(w, r)
	case http.MethodPost:
		a.authorizePOST(w, r)
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

func parseForm(w http.ResponseWriter, r *http.Request) (url.Values, bool) {
	media, _, _ := mime.ParseMediaType(r.Header.Get("Content-Type"))
	if media != "application/x-www-form-urlencoded" {
		return nil, false
	}
	r.Body = http.MaxBytesReader(w, r.Body, reqBodyMax)
	if r.ParseForm() != nil {
		return nil, false
	}
	return r.PostForm, true
}

func verifierChallenge(verifier string) string {
	sum := sha256.Sum256([]byte(verifier))
	return base64.RawURLEncoding.EncodeToString(sum[:])
}

func (a *app) signAccess(r *http.Request, subject, clientID, scope string) (string, string, int64, error) {
	now := time.Now().UTC().Truncate(time.Second)
	id, err := opaque()
	if err != nil {
		return "", "", 0, err
	}
	idDigest := digest(id)
	jti := hex.EncodeToString(idDigest[:16])
	expires := now.Add(a.accessLifetime).Unix()
	claims := accessClaims{ClientID: clientID, Scope: scope, RegisteredClaims: jwt.RegisteredClaims{
		Issuer: issuer(r), Subject: subject, Audience: jwt.ClaimStrings{"https://" + r.Host + "/api/ping"},
		ExpiresAt: jwt.NewNumericDate(time.Unix(expires, 0)), IssuedAt: jwt.NewNumericDate(now),
		NotBefore: jwt.NewNumericDate(now), ID: jti}}
	token := jwt.NewWithClaims(jwt.SigningMethodES256, claims)
	token.Header["typ"] = "at+jwt"
	token.Header["kid"] = a.keyID
	raw, err := token.SignedString(a.key)
	return raw, jti, expires, err
}

func (a *app) issuePair(r *http.Request, subject, clientID, scope, family string) (tokenResponse, error) {
	access, jti, accessExpiry, err := a.signAccess(r, subject, clientID, scope)
	if err != nil {
		return tokenResponse{}, err
	}
	refresh, err := opaque()
	if err != nil {
		return tokenResponse{}, err
	}
	newFamily := family == ""
	if newFamily {
		family, err = opaque()
		if err != nil {
			return tokenResponse{}, err
		}
	}
	now := time.Now().Unix()
	refreshExpiry := now + int64(a.refreshLifetime/time.Second)
	refreshKey := digest(refresh)
	a.mu.Lock()
	defer a.mu.Unlock()
	a.reapLocked(now)
	currentFamily, familyFound := a.families[family]
	if !newFamily {
		if !familyFound || currentFamily.revoked || currentFamily.expires <= now {
			return tokenResponse{}, errors.New("token family unavailable")
		}
		refreshExpiry = currentFamily.expires
	} else if familyFound {
		return tokenResponse{}, errors.New("token family collision")
	}
	if len(a.refreshTokens) >= tokenMax || len(a.accessTokens) >= tokenMax ||
		(newFamily && len(a.families) >= tokenMax) {
		return tokenResponse{}, errors.New("token capacity exhausted")
	}
	refreshExpiryEntry := a.scheduleLocked(expiryRefresh, refreshKey, "", refreshExpiry)
	a.refreshTokens[refreshKey] = refreshGrant{family, clientID, subject, scope,
		refreshExpiry, true, refreshExpiryEntry}
	if newFamily {
		familyExpiry := a.scheduleLocked(expiryFamily, [32]byte{}, family, refreshExpiry)
		a.families[family] = familyState{refreshExpiry, false, familyExpiry}
	}
	accessExpiryEntry := a.scheduleLocked(expiryAccess, [32]byte{}, jti, accessExpiry)
	a.accessTokens[jti] = accessState{family, accessExpiry, false, accessExpiryEntry}
	return tokenResponse{access, "Bearer", int64(a.accessLifetime / time.Second), refresh, scope}, nil
}

func (a *app) parseAccess(r *http.Request, raw string) (*accessClaims, error) {
	claims := new(accessClaims)
	token, err := jwt.ParseWithClaims(raw, claims, func(token *jwt.Token) (any, error) {
		if token.Method != jwt.SigningMethodES256 || token.Header["typ"] != "at+jwt" ||
			token.Header["kid"] != a.keyID {
			return nil, errors.New("invalid JWT header")
		}
		return &a.key.PublicKey, nil
	}, jwt.WithValidMethods([]string{"ES256"}), jwt.WithExpirationRequired(),
		jwt.WithIssuer(issuer(r)), jwt.WithAudience("https://"+r.Host+"/api/ping"), jwt.WithStrictDecoding())
	if err != nil || !token.Valid || claims.Subject == "" || claims.ClientID == "" ||
		claims.ID == "" || claims.IssuedAt == nil || claims.NotBefore == nil || claims.ExpiresAt == nil {
		return nil, errors.New("invalid access token")
	}
	a.mu.Lock()
	a.reapLocked(time.Now().Unix())
	state, found := a.accessTokens[claims.ID]
	family, familyFound := a.families[state.family]
	a.mu.Unlock()
	if !found || !familyFound || state.revoked || family.revoked {
		return nil, errors.New("revoked access token")
	}
	return claims, nil
}

func (a *app) tokenHandler(w http.ResponseWriter, r *http.Request) {
	values, ok := parseForm(w, r)
	if !ok {
		oauthFailure(w, http.StatusBadRequest, "invalid_request", "form body required")
		return
	}
	var response tokenResponse
	var err error
	switch values.Get("grant_type") {
	case "authorization_code":
		if !exactValues(values, []string{"grant_type", "code", "redirect_uri", "client_id", "code_verifier"}, nil) {
			oauthFailure(w, http.StatusBadRequest, "invalid_request", "invalid code request")
			return
		}
		key := digest(values.Get("code"))
		a.mu.Lock()
		a.reapLocked(time.Now().Unix())
		grant, found := a.codes[key]
		valid := found && grant.expires > time.Now().Unix() && grant.issuer == issuer(r) &&
			grant.clientID == values.Get("client_id") && grant.redirectURI == values.Get("redirect_uri") &&
			len(values.Get("code_verifier")) >= 43 && len(values.Get("code_verifier")) <= 128 &&
			verifierChallenge(values.Get("code_verifier")) == grant.challenge
		if valid {
			delete(a.codes, key)
			a.unscheduleLocked(grant.expiry)
		}
		a.mu.Unlock()
		if !valid {
			oauthFailure(w, http.StatusBadRequest, "invalid_grant", "authorization code is invalid")
			return
		}
		response, err = a.issuePair(r, grant.subject, grant.clientID, grant.scope, "")
		if err == nil {
			a.wrote("token", &a.token)
		}
	case "refresh_token":
		if !exactValues(values, []string{"grant_type", "refresh_token", "client_id"}, []string{"scope"}) {
			oauthFailure(w, http.StatusBadRequest, "invalid_request", "invalid refresh request")
			return
		}
		key := digest(values.Get("refresh_token"))
		a.mu.Lock()
		a.reapLocked(time.Now().Unix())
		grant, found := a.refreshTokens[key]
		family, familyFound := a.families[grant.family]
		if found && familyFound && !grant.active {
			family.revoked = true
			a.families[grant.family] = family
		}
		valid := found && familyFound && grant.active && grant.expires > time.Now().Unix() &&
			grant.clientID == values.Get("client_id") && !family.revoked
		scope := values.Get("scope")
		if scope == "" {
			scope = grant.scope
		}
		valid = valid && scopeSubset(scope, grant.scope)
		if valid {
			grant.active = false
			a.refreshTokens[key] = grant
		}
		a.mu.Unlock()
		if !valid {
			oauthFailure(w, http.StatusBadRequest, "invalid_grant", "refresh token is invalid")
			return
		}
		response, err = a.issuePair(r, grant.subject, grant.clientID, scope, grant.family)
		if err == nil {
			a.wrote("refresh", &a.refresh)
		}
	default:
		oauthFailure(w, http.StatusBadRequest, "unsupported_grant_type", "grant type is unsupported")
		return
	}
	if err != nil {
		a.fail()
		oauthFailure(w, http.StatusInternalServerError, "server_error", "token issuance failed")
		return
	}
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Pragma", "no-cache")
	if writeJSON(w, http.StatusOK, response) != nil {
		a.fail()
	}
}

func (a *app) revokeHandler(w http.ResponseWriter, r *http.Request) {
	values, ok := parseForm(w, r)
	if !ok || !exactValues(values, []string{"token", "client_id"}, []string{"token_type_hint"}) {
		oauthFailure(w, http.StatusBadRequest, "invalid_request", "invalid revocation request")
		return
	}
	clientID, raw := values.Get("client_id"), values.Get("token")
	if clientID != "zrest-native" && clientID != "zrest-go" {
		oauthFailure(w, http.StatusUnauthorized, "invalid_client", "unknown public client")
		return
	}
	a.mu.Lock()
	a.reapLocked(time.Now().Unix())
	if grant, found := a.refreshTokens[digest(raw)]; found && grant.clientID == clientID {
		family := a.families[grant.family]
		family.revoked = true
		a.families[grant.family] = family
	}
	a.mu.Unlock()
	if claims, parseErr := a.parseAccess(r, raw); parseErr == nil && claims.ClientID == clientID {
		a.mu.Lock()
		state := a.accessTokens[claims.ID]
		state.revoked = true
		a.accessTokens[claims.ID] = state
		a.mu.Unlock()
	}
	a.wrote("revoke", &a.revoke)
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(http.StatusOK)
}

func (a *app) keysHandler(w http.ResponseWriter, _ *http.Request) {
	size := (a.key.Curve.Params().BitSize + 7) / 8
	value := struct {
		Keys []map[string]string `json:"keys"`
	}{[]map[string]string{{"kid": a.keyID, "kty": "EC", "crv": "P-256", "use": "sig",
		"alg": "ES256", "x": base64.RawURLEncoding.EncodeToString(a.key.X.FillBytes(make([]byte, size))),
		"y": base64.RawURLEncoding.EncodeToString(a.key.Y.FillBytes(make([]byte, size)))}}}
	if writeJSON(w, http.StatusOK, value) != nil {
		a.fail()
	}
}

func bearerFailure(w http.ResponseWriter, status int, code string) {
	w.Header().Set("WWW-Authenticate", `Bearer error="`+code+`"`)
	w.WriteHeader(status)
}

func (a *app) pingHandler(w http.ResponseWriter, r *http.Request) {
	values, err := url.ParseQuery(r.URL.RawQuery)
	if err != nil || !exactValues(values, []string{"ping"}, nil) || values.Get("ping") != "true" {
		w.WriteHeader(http.StatusBadRequest)
		return
	}
	authorization := r.Header.Values("Authorization")
	if len(authorization) != 1 || !strings.HasPrefix(authorization[0], "Bearer ") ||
		strings.Count(authorization[0], " ") != 1 || len(authorization[0]) == 7 {
		bearerFailure(w, http.StatusUnauthorized, "invalid_token")
		return
	}
	claims, err := a.parseAccess(r, authorization[0][7:])
	if err != nil {
		bearerFailure(w, http.StatusUnauthorized, "invalid_token")
		return
	}
	if !hasScope(claims.Scope, pingScope) {
		bearerFailure(w, http.StatusForbidden, "insufficient_scope")
		return
	}
	if writeJSON(w, http.StatusOK, struct {
		Pong bool `json:"pong"`
	}{true}) != nil {
		a.fail()
		return
	}
	a.wrote("pong", &a.pong)
}

func main() {
	addr := flag.String("addr", ":8443", "listen address")
	cert := flag.String("cert", "server.crt", "TLS certificate")
	keyFile := flag.String("key", "server.key", "TLS private key")
	user := flag.String("user", "test", "demonstration username")
	password := flag.String("pass", "test123", "demonstration password")
	requests := flag.Uint64("requests", 0, "successful pongs before shutdown")
	verbose := flag.Bool("verbose", false, "emit stable event records")
	access := durationValue{5 * time.Minute}
	refresh := durationValue{24 * time.Hour}
	flag.Var(&access, "access-token-lifetime", "access token lifetime")
	flag.Var(&refresh, "refresh-token-lifetime", "refresh token lifetime")
	flag.Parse()
	if *user == "" || *password == "" || refresh.d <= access.d {
		flag.Usage()
		os.Exit(2)
	}
	signingKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		log.Fatal(err)
	}
	keyID, err := opaque()
	if err != nil {
		log.Fatal(err)
	}
	a := &app{key: signingKey, keyID: keyID[:16], user: *user, password: *password,
		accessLifetime: access.d, refreshLifetime: refresh.d, requestLimit: *requests,
		verbose: *verbose, sessions: make(map[[32]byte]browserSession),
		codes: make(map[[32]byte]codeGrant), refreshTokens: make(map[[32]byte]refreshGrant),
		families: make(map[string]familyState), accessTokens: make(map[string]accessState),
		stop: make(chan struct{})}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /.well-known/oauth-authorization-server/oauth2/ping", a.metadataHandler)
	mux.HandleFunc("/oauth2/ping/v1/authorize", a.authorizeHandler)
	mux.HandleFunc("POST /oauth2/ping/v1/token", a.tokenHandler)
	mux.HandleFunc("POST /oauth2/ping/v1/revoke", a.revokeHandler)
	mux.HandleFunc("GET /oauth2/ping/v1/keys", a.keysHandler)
	mux.HandleFunc("GET /api/ping", a.pingHandler)
	server := &http.Server{Addr: *addr, Handler: mux, TLSConfig: &tls.Config{
		MinVersion: tls.VersionTLS12, NextProtos: []string{"http/1.1"}}}
	pair, err := tls.LoadX509KeyPair(*cert, *keyFile)
	if err != nil {
		log.Fatal(err)
	}
	listener, err := net.Listen("tcp", *addr)
	if err != nil {
		log.Fatal(err)
	}
	server.TLSConfig.Certificates = []tls.Certificate{pair}
	log.Printf("event=listening addr=%s", listener.Addr())
	errCh := make(chan error, 1)
	go func() { errCh <- server.Serve(tls.NewListener(listener, server.TLSConfig)) }()
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	select {
	case <-a.stop:
	case <-signals:
	case err = <-errCh:
		if !errors.Is(err, http.ErrServerClosed) {
			a.fatal.Store(true)
			log.Print(err)
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := server.Shutdown(ctx); err != nil {
		a.fatal.Store(true)
		log.Print(err)
	}
	log.Printf("summary authorize=%d token=%d refresh=%d revoke=%d pong=%d",
		a.authorize.Load(), a.token.Load(), a.refresh.Load(), a.revoke.Load(), a.pong.Load())
	if a.fatal.Load() {
		os.Exit(1)
	}
}
