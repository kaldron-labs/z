// zrestprobe exercises OAuth and bearer failures against either example server.
package main

import (
	"bufio"
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"strings"
	"time"
)

const bodyMax = 1 << 20

type metadata struct {
	Issuer                string   `json:"issuer"`
	AuthorizationEndpoint string   `json:"authorization_endpoint"`
	TokenEndpoint         string   `json:"token_endpoint"`
	RevocationEndpoint    string   `json:"revocation_endpoint"`
	JWKSURI               string   `json:"jwks_uri"`
	Responses             []string `json:"response_types_supported"`
	Grants                []string `json:"grant_types_supported"`
	Challenges            []string `json:"code_challenge_methods_supported"`
	Scopes                []string `json:"scopes_supported"`
	TokenAuth             []string `json:"token_endpoint_auth_methods_supported"`
	RevokeAuth            []string `json:"revocation_endpoint_auth_methods_supported"`
}

type oauthError struct {
	Error       string `json:"error"`
	Description string `json:"error_description,omitempty"`
}

type tokenResponse struct {
	AccessToken  string `json:"access_token"`
	TokenType    string `json:"token_type"`
	ExpiresIn    int64  `json:"expires_in"`
	RefreshToken string `json:"refresh_token"`
	Scope        string `json:"scope"`
}

type response struct {
	status int
	header http.Header
	body   []byte
}

type grant struct {
	code, verifier, redirect string
}

func fail(format string, args ...any) {
	panic(fmt.Sprintf(format, args...))
}

func randomText() string {
	var raw [32]byte
	if _, err := io.ReadFull(rand.Reader, raw[:]); err != nil {
		panic(err)
	}
	return base64.RawURLEncoding.EncodeToString(raw[:])
}

func challenge(verifier string) string {
	digest := sha256.Sum256([]byte(verifier))
	return base64.RawURLEncoding.EncodeToString(digest[:])
}

func httpClient(cert string, redirect bool) (*http.Client, error) {
	pem, err := os.ReadFile(cert)
	if err != nil {
		return nil, err
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(pem) {
		return nil, errors.New("invalid CA")
	}
	client := &http.Client{Timeout: 10 * time.Second, Transport: &http.Transport{
		TLSClientConfig: &tls.Config{RootCAs: roots, MinVersion: tls.VersionTLS12,
			NextProtos: []string{"http/1.1"}}, DisableCompression: true}}
	if !redirect {
		client.CheckRedirect = func(*http.Request, []*http.Request) error {
			return http.ErrUseLastResponse
		}
	}
	return client, nil
}

func request(ctx context.Context, client *http.Client, method, endpoint string,
	body io.Reader, headers [][2]string) response {
	req, err := http.NewRequestWithContext(ctx, method, endpoint, body)
	if err != nil {
		panic(err)
	}
	for _, header := range headers {
		req.Header.Add(header[0], header[1])
	}
	res, err := client.Do(req)
	if err != nil {
		panic(err)
	}
	defer res.Body.Close()
	data, err := io.ReadAll(io.LimitReader(res.Body, bodyMax+1))
	if err != nil || len(data) > bodyMax {
		panic("response body failure")
	}
	return response{res.StatusCode, res.Header, data}
}

func get(ctx context.Context, client *http.Client, endpoint string,
	headers [][2]string) response {
	return request(ctx, client, http.MethodGet, endpoint, nil, headers)
}

func postForm(ctx context.Context, client *http.Client, endpoint string,
	values url.Values) response {
	return request(ctx, client, http.MethodPost, endpoint,
		strings.NewReader(values.Encode()), [][2]string{{"Content-Type",
			"application/x-www-form-urlencoded"}})
}

func decode(data []byte, value any) {
	decoder := json.NewDecoder(strings.NewReader(string(data)))
	decoder.DisallowUnknownFields()
	if decoder.Decode(value) != nil || decoder.Decode(new(any)) != io.EOF {
		panic("invalid JSON response")
	}
}

func expectOAuth(res response, status int, code string) {
	if res.status != status || !strings.HasPrefix(res.header.Get("Content-Type"),
		"application/json") || res.header.Get("Cache-Control") != "no-store" {
		fail("OAuth failure: status=%d", res.status)
	}
	var value oauthError
	decode(res.body, &value)
	if value.Error != code {
		fail("OAuth failure code: got %q", value.Error)
	}
}

func authValues(clientID, redirect, scope, state, verifier string) url.Values {
	return url.Values{"response_type": {"code"}, "client_id": {clientID},
		"redirect_uri": {redirect}, "scope": {scope}, "state": {state},
		"code_challenge":        {challenge(verifier)},
		"code_challenge_method": {"S256"}}
}

func expectRedirectError(res response, code, state, issuer string) {
	if res.status != http.StatusFound {
		fail("authorization redirect: status=%d", res.status)
	}
	location, err := url.Parse(res.header.Get("Location"))
	if err != nil || location.Query().Get("error") != code ||
		location.Query().Get("state") != state || location.Query().Get("iss") != issuer ||
		location.Query().Get("code") != "" {
		panic("invalid authorization error redirect")
	}
}

func authorize(ctx context.Context, client *http.Client, browser, cert,
	issuer, endpoint, clientID, scope string) grant {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		panic(err)
	}
	defer listener.Close()
	redirect := "http://" + listener.Addr().String() + "/callback"
	verifier, state := randomText(), randomText()
	values := authValues(clientID, redirect, scope, state, verifier)
	authorizeURL := endpoint + "?" + values.Encode()

	result := make(chan url.Values, 1)
	server := &http.Server{ReadHeaderTimeout: 5 * time.Second, Handler: http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			if r.Method != http.MethodGet || r.URL.Path != "/callback" {
				w.WriteHeader(http.StatusNotFound)
				return
			}
			select {
			case result <- r.URL.Query():
			default:
			}
			w.WriteHeader(http.StatusOK)
		})}
	done := make(chan error, 1)
	go func() { done <- server.Serve(listener) }()

	command := exec.CommandContext(ctx, browser, authorizeURL)
	command.Env = append(os.Environ(), "ZREST_CA="+cert)
	if err := command.Run(); err != nil {
		panic("headless user agent failed")
	}
	var query url.Values
	select {
	case query = <-result:
	case <-time.After(10 * time.Second):
		panic("authorization callback timeout")
	}
	shutdown, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	_ = server.Shutdown(shutdown)
	<-done
	if len(query) != 3 || len(query["code"]) != 1 ||
		query.Get("state") != state || query.Get("iss") != issuer ||
		query.Get("error") != "" || query.Get("code") == "" {
		panic("invalid authorization callback")
	}
	return grant{query.Get("code"), verifier, redirect}
}

func redeem(ctx context.Context, client *http.Client, endpoint, clientID string,
	grant grant, verifier string) response {
	return postForm(ctx, client, endpoint, url.Values{
		"grant_type": {"authorization_code"}, "code": {grant.code},
		"redirect_uri": {grant.redirect}, "client_id": {clientID},
		"code_verifier": {verifier}})
}

func token(res response, scope string) tokenResponse {
	if res.status != http.StatusOK || res.header.Get("Cache-Control") != "no-store" ||
		res.header.Get("Pragma") != "no-cache" {
		fail("token response: status=%d", res.status)
	}
	var value tokenResponse
	decode(res.body, &value)
	if value.AccessToken == "" || value.RefreshToken == "" ||
		value.TokenType != "Bearer" || value.ExpiresIn <= 0 || value.Scope != scope {
		panic("invalid token response")
	}
	return value
}

func ping(ctx context.Context, client *http.Client, resource, access string) response {
	headers := [][2]string(nil)
	if access != "" {
		headers = append(headers, [2]string{"Authorization", "Bearer " + access})
	}
	return get(ctx, client, resource+"?ping=true", headers)
}

func expectBearer(res response, status int, code string) {
	if res.status != status || !strings.HasPrefix(res.header.Get("WWW-Authenticate"),
		"Bearer") || !strings.Contains(res.header.Get("WWW-Authenticate"), code) {
		fail("bearer failure: status=%d", res.status)
	}
}

func revoke(ctx context.Context, client *http.Client, endpoint, clientID,
	raw, hint string) response {
	values := url.Values{"token": {raw}, "client_id": {clientID}}
	if hint != "" {
		values.Set("token_type_hint", hint)
	}
	return postForm(ctx, client, endpoint, values)
}

func replaceHost(raw, host string) string {
	value, err := url.Parse(raw)
	if err != nil {
		panic(err)
	}
	value.Host = net.JoinHostPort(host, value.Port())
	return value.String()
}

func rawRequest(roots *x509.CertPool, address, target, host string) response {
	dialer := &net.Dialer{Timeout: 5 * time.Second}
	conn, err := tls.DialWithDialer(dialer, "tcp", address, &tls.Config{
		RootCAs: roots, ServerName: "localhost", MinVersion: tls.VersionTLS12,
		NextProtos: []string{"http/1.1"}})
	if err != nil {
		panic(err)
	}
	defer conn.Close()
	requestText := "GET " + target + " HTTP/1.1\r\n"
	if host != "" {
		requestText += "Host: " + host + "\r\n"
	}
	requestText += "Connection: close\r\n\r\n"
	if _, err := io.WriteString(conn, requestText); err != nil {
		panic(err)
	}
	res, err := http.ReadResponse(bufio.NewReader(conn),
		&http.Request{Method: http.MethodGet})
	if err != nil {
		panic(err)
	}
	defer res.Body.Close()
	data, err := io.ReadAll(io.LimitReader(res.Body, bodyMax+1))
	if err != nil || len(data) > bodyMax {
		panic("raw response body failure")
	}
	return response{res.StatusCode, res.Header, data}
}

func authorityChecks(cert string, issuerURL *url.URL, expectedIssuer string) {
	pem, err := os.ReadFile(cert)
	if err != nil {
		panic(err)
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(pem) {
		panic("invalid CA")
	}
	address := net.JoinHostPort("127.0.0.1", issuerURL.Port())
	path := "/.well-known/oauth-authorization-server/oauth2/ping"
	absolute := "https://" + issuerURL.Host + path
	checkMetadata := func(res response) {
		if res.status != http.StatusOK {
			fail("raw metadata: status=%d", res.status)
		}
		var value metadata
		decode(res.body, &value)
		if value.Issuer != expectedIssuer {
			fail("raw metadata issuer: got %q", value.Issuer)
		}
	}
	checkMetadata(rawRequest(roots, address, absolute, "inconsistent.invalid"))
	checkMetadata(rawRequest(roots, address, absolute, ""))
	checkMetadata(rawRequest(roots, address, path,
		"LOCALHOST:"+issuerURL.Port()))
	if rawRequest(roots, address, path, "bad host").status != http.StatusBadRequest {
		panic("malformed Host was accepted")
	}
	if rawRequest(roots, address,
		"http://"+issuerURL.Host+path, "").status != http.StatusBadRequest {
		panic("transport-inconsistent scheme was accepted")
	}
}

func main() {
	issuerFlag := flag.String("issuer", "https://localhost:8443/oauth2/ping", "exact issuer")
	resource := flag.String("resource", "https://localhost:8443/api/ping", "resource URL")
	cert := flag.String("cert", "server.crt", "server CA")
	browser := flag.String("browser", "./zrestua", "headless user agent")
	authority := flag.Bool("authority", false, "exercise raw authority handling")
	secondApp := flag.Bool("second-app", false, "exercise application isolation")
	expiry := flag.Bool("expiry", false, "exercise bounded state expiry")
	flag.Parse()
	client, err := httpClient(*cert, false)
	if err != nil {
		panic(err)
	}
	ctx := context.Background()

	issuerURL, err := url.Parse(*issuerFlag)
	if err != nil || issuerURL.Scheme != "https" || issuerURL.Host == "" {
		panic("invalid issuer")
	}
	metadataURL := *issuerFlag
	metadataURL = strings.TrimSuffix(metadataURL, issuerURL.EscapedPath()) +
		"/.well-known/oauth-authorization-server" + issuerURL.EscapedPath()
	res := get(ctx, client, metadataURL, nil)
	if res.status != http.StatusOK {
		fail("metadata: status=%d", res.status)
	}
	var meta metadata
	decode(res.body, &meta)
	if meta.Issuer != *issuerFlag || meta.AuthorizationEndpoint == "" ||
		meta.TokenEndpoint == "" || meta.RevocationEndpoint == "" || meta.JWKSURI == "" {
		panic("invalid metadata")
	}
	if *authority {
		authorityChecks(*cert, issuerURL, *issuerFlag)
	}
	if *expiry {
		first := authorize(ctx, client, *browser, *cert, *issuerFlag,
			meta.AuthorizationEndpoint, "zrest-native", "ping")
		firstTokens := token(redeem(ctx, client, meta.TokenEndpoint,
			"zrest-native", first, first.verifier), "ping")
		if revoke(ctx, client, meta.RevocationEndpoint, "zrest-native",
			firstTokens.RefreshToken, "refresh_token").status != http.StatusOK {
			panic("initial expiry revocation failed")
		}
		time.Sleep(3 * time.Second)
		second := authorize(ctx, client, *browser, *cert, *issuerFlag,
			meta.AuthorizationEndpoint, "zrest-native", "ping")
		secondTokens := token(redeem(ctx, client, meta.TokenEndpoint,
			"zrest-native", second, second.verifier), "ping")
		if revoke(ctx, client, meta.RevocationEndpoint, "zrest-native",
			secondTokens.RefreshToken, "refresh_token").status != http.StatusOK {
			panic("post-expiry revocation failed")
		}
		return
	}

	expectOAuth(get(ctx, client, meta.AuthorizationEndpoint, nil),
		http.StatusBadRequest, "invalid_request")
	state, verifier := "probe-state", randomText()
	redirect := "http://127.0.0.1:49152/callback"
	values := authValues("unknown", redirect, "ping", state, verifier)
	expectOAuth(get(ctx, client, meta.AuthorizationEndpoint+"?"+values.Encode(), nil),
		http.StatusBadRequest, "unauthorized_client")
	values = authValues("zrest-native", "https://example.invalid/callback", "ping", state, verifier)
	expectOAuth(get(ctx, client, meta.AuthorizationEndpoint+"?"+values.Encode(), nil),
		http.StatusBadRequest, "unauthorized_client")
	values = authValues("zrest-native", redirect, "ping", state, verifier)
	values.Del("code_challenge")
	expectRedirectError(get(ctx, client, meta.AuthorizationEndpoint+"?"+values.Encode(), nil),
		"invalid_request", state, *issuerFlag)
	values = authValues("zrest-native", redirect, "ping", state, verifier)
	values.Set("code_challenge_method", "plain")
	expectRedirectError(get(ctx, client, meta.AuthorizationEndpoint+"?"+values.Encode(), nil),
		"invalid_request", state, *issuerFlag)

	first := authorize(ctx, client, *browser, *cert, *issuerFlag,
		meta.AuthorizationEndpoint, "zrest-native", "ping other")
	expectOAuth(redeem(ctx, client, meta.TokenEndpoint, "zrest-native", first,
		first.verifier+"x"), http.StatusBadRequest, "invalid_grant")
	expectOAuth(redeem(ctx, client, meta.TokenEndpoint, "zrest-go", first,
		first.verifier), http.StatusBadRequest, "invalid_grant")
	alternateToken := replaceHost(meta.TokenEndpoint, "127.0.0.1")
	expectOAuth(redeem(ctx, client, alternateToken, "zrest-native", first,
		first.verifier), http.StatusBadRequest, "invalid_grant")
	firstTokens := token(redeem(ctx, client, meta.TokenEndpoint, "zrest-native", first,
		first.verifier), "ping other")
	expectOAuth(redeem(ctx, client, meta.TokenEndpoint, "zrest-native", first,
		first.verifier), http.StatusBadRequest, "invalid_grant")
	if ping(ctx, client, *resource, firstTokens.AccessToken).status != http.StatusOK {
		panic("valid access token rejected")
	}
	expectBearer(ping(ctx, client, *resource, ""), http.StatusUnauthorized, "invalid_token")
	last := "A"
	if firstTokens.AccessToken[len(firstTokens.AccessToken)-1] == 'A' {
		last = "B"
	}
	corrupt := firstTokens.AccessToken[:len(firstTokens.AccessToken)-1] + last
	expectBearer(ping(ctx, client, *resource, corrupt), http.StatusUnauthorized, "invalid_token")
	duplicate := get(ctx, client, *resource+"?ping=true", [][2]string{
		{"Authorization", "Bearer " + firstTokens.AccessToken},
		{"Authorization", "Bearer " + firstTokens.AccessToken}})
	expectBearer(duplicate, http.StatusUnauthorized, "invalid_token")

	expectOAuth(postForm(ctx, client, meta.TokenEndpoint, url.Values{
		"grant_type": {"refresh_token"}, "refresh_token": {firstTokens.RefreshToken},
		"client_id": {"zrest-go"}}), http.StatusBadRequest, "invalid_grant")
	reduced := token(postForm(ctx, client, meta.TokenEndpoint, url.Values{
		"grant_type": {"refresh_token"}, "refresh_token": {firstTokens.RefreshToken},
		"client_id": {"zrest-native"}, "scope": {"other"}}), "other")
	expectBearer(ping(ctx, client, *resource, reduced.AccessToken),
		http.StatusForbidden, "insufficient_scope")
	expectOAuth(postForm(ctx, client, meta.TokenEndpoint, url.Values{
		"grant_type": {"refresh_token"}, "refresh_token": {firstTokens.RefreshToken},
		"client_id": {"zrest-native"}}), http.StatusBadRequest, "invalid_grant")
	expectBearer(ping(ctx, client, *resource, reduced.AccessToken),
		http.StatusUnauthorized, "invalid_token")

	second := authorize(ctx, client, *browser, *cert, *issuerFlag,
		meta.AuthorizationEndpoint, "zrest-native", "ping")
	secondTokens := token(redeem(ctx, client, meta.TokenEndpoint, "zrest-native", second,
		second.verifier), "ping")
	if revoke(ctx, client, meta.RevocationEndpoint, "zrest-native",
		secondTokens.AccessToken, "access_token").status != http.StatusOK {
		panic("access-token revocation failed")
	}
	expectBearer(ping(ctx, client, *resource, secondTokens.AccessToken),
		http.StatusUnauthorized, "invalid_token")

	third := authorize(ctx, client, *browser, *cert, *issuerFlag,
		meta.AuthorizationEndpoint, "zrest-native", "ping")
	thirdTokens := token(redeem(ctx, client, meta.TokenEndpoint, "zrest-native", third,
		third.verifier), "ping")
	if revoke(ctx, client, meta.RevocationEndpoint, "zrest-native",
		thirdTokens.RefreshToken, "refresh_token").status != http.StatusOK {
		panic("refresh-token revocation failed")
	}
	expectBearer(ping(ctx, client, *resource, thirdTokens.AccessToken),
		http.StatusUnauthorized, "invalid_token")
	if revoke(ctx, client, meta.RevocationEndpoint, "zrest-native",
		"unknown-token", "").status != http.StatusOK {
		panic("unknown-token revocation was not idempotent")
	}

	alternateIssuer := replaceHost(*issuerFlag, "127.0.0.1")
	alternateAuthorize := replaceHost(meta.AuthorizationEndpoint, "127.0.0.1")
	alternateResource := replaceHost(*resource, "127.0.0.1")
	otherAuthority := authorize(ctx, client, *browser, *cert, alternateIssuer,
		alternateAuthorize, "zrest-native", "ping")
	otherTokens := token(redeem(ctx, client, alternateToken, "zrest-native",
		otherAuthority, otherAuthority.verifier), "ping")
	expectBearer(ping(ctx, client, *resource, otherTokens.AccessToken),
		http.StatusUnauthorized, "invalid_token")
	if ping(ctx, client, alternateResource, otherTokens.AccessToken).status != http.StatusOK {
		panic("authority-bound token rejected at its resource")
	}

	otherIssuer := strings.Replace(*issuerFlag, "/oauth2/ping", "/oauth2/other", 1)
	otherMetadataURL := strings.Replace(metadataURL, "/oauth2/ping", "/oauth2/other", 1)
	if *secondApp {
		otherMetadataRes := get(ctx, client, otherMetadataURL, nil)
		if otherMetadataRes.status != http.StatusOK {
			fail("second application metadata: status=%d", otherMetadataRes.status)
		}
		var otherMeta metadata
		decode(otherMetadataRes.body, &otherMeta)
		if otherMeta.Issuer != otherIssuer {
			panic("second application issuer mismatch")
		}

		pingGrant := authorize(ctx, client, *browser, *cert, *issuerFlag,
			meta.AuthorizationEndpoint, "zrest-native", "ping")
		expectOAuth(redeem(ctx, client, otherMeta.TokenEndpoint, "zrest-native",
			pingGrant, pingGrant.verifier), http.StatusBadRequest, "invalid_grant")
		pingTokens := token(redeem(ctx, client, meta.TokenEndpoint, "zrest-native",
			pingGrant, pingGrant.verifier), "ping")

		otherGrant := authorize(ctx, client, *browser, *cert, otherIssuer,
			otherMeta.AuthorizationEndpoint, "zrest-other", "ping")
		expectOAuth(redeem(ctx, client, meta.TokenEndpoint, "zrest-native",
			otherGrant, otherGrant.verifier), http.StatusBadRequest, "invalid_grant")
		otherTokens := token(redeem(ctx, client, otherMeta.TokenEndpoint,
			"zrest-other", otherGrant, otherGrant.verifier), "ping")
		expectBearer(ping(ctx, client, *resource, otherTokens.AccessToken),
			http.StatusUnauthorized, "invalid_token")
		if revoke(ctx, client, meta.RevocationEndpoint, "zrest-native",
			pingTokens.RefreshToken, "refresh_token").status != http.StatusOK ||
			revoke(ctx, client, otherMeta.RevocationEndpoint, "zrest-other",
				otherTokens.RefreshToken, "refresh_token").status != http.StatusOK {
			panic("cross-application cleanup failed")
		}
	} else if postForm(ctx, client,
		strings.Replace(meta.TokenEndpoint, "/oauth2/ping/", "/oauth2/other/", 1),
		url.Values{"grant_type": {"authorization_code"}, "code": {"unknown"},
			"redirect_uri": {redirect}, "client_id": {"zrest-native"},
			"code_verifier": {verifier}}).status != http.StatusNotFound {
		panic("unknown application was admitted")
	}
}
