//  -*- mode:c++; indent-tabs-mode:t; tab-width:4; c-basic-offset:4; -*-
//  vi: noet ts=4 sw=4 cino=+0,(s,l1,m1,j1,U1,W4

package main

import (
	"bytes"
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

const (
	responseBodyMax = 1 << 20
	urlMax          = 2048
	tokenValueMax   = 16 << 10
)

type durationValue struct {
	d         time.Duration
	allowZero bool
}

func (v *durationValue) String() string { return v.d.String() }
func (v *durationValue) Set(s string) error {
	if len(s) < 2 || strings.ContainsAny(s[:len(s)-1], ".+-") {
		return errors.New("expected an unsigned integer followed by s, m, or h")
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
	if n == 0 && !v.allowZero || n > int64((1<<63-1)/unit) {
		return errors.New("invalid or overflowing duration")
	}
	v.d = time.Duration(n) * unit
	return nil
}

type metadata struct {
	Issuer            string   `json:"issuer"`
	Authorization     string   `json:"authorization_endpoint"`
	Token             string   `json:"token_endpoint"`
	Revoke            string   `json:"revocation_endpoint"`
	Keys              string   `json:"jwks_uri"`
	Responses         []string `json:"response_types_supported"`
	Grants            []string `json:"grant_types_supported"`
	Challenges        []string `json:"code_challenge_methods_supported"`
	Scopes            []string `json:"scopes_supported"`
	TokenAuthMethods  []string `json:"token_endpoint_auth_methods_supported"`
	RevokeAuthMethods []string `json:"revocation_endpoint_auth_methods_supported"`
}

type tokenResponse struct {
	AccessToken  string `json:"access_token"`
	TokenType    string `json:"token_type"`
	ExpiresIn    int64  `json:"expires_in"`
	RefreshToken string `json:"refresh_token,omitempty"`
	Scope        string `json:"scope"`
}

type oauthError struct {
	Error       string `json:"error"`
	Description string `json:"error_description,omitempty"`
}

type callbackResult struct {
	code, state, issuer, oauthError string
}

type client struct {
	issuer, clientID, scope, resource string
	browser, certFile                 string
	http                              *http.Client
	metadata                          metadata
	accessToken, refreshToken         string
	accessExpires                     time.Time
}

func absoluteEndpoint(raw string, allowHTTP bool) (*url.URL, bool) {
	if len(raw) == 0 || len(raw) > urlMax {
		return nil, false
	}
	u, err := url.Parse(raw)
	if err != nil || u.Host == "" || u.User != nil || u.RawQuery != "" || u.Fragment != "" ||
		(u.Scheme != "https" && (!allowHTTP || u.Scheme != "http")) {
		return nil, false
	}
	if u.Scheme == "http" && u.Hostname() != "127.0.0.1" && u.Hostname() != "::1" {
		return nil, false
	}
	return u, true
}

func contains(values []string, required string) bool {
	for _, value := range values {
		if value == required {
			return true
		}
	}
	return false
}

func newClient(issuer, clientID, scope, resource, certFile, browser string,
	timeout time.Duration, allowHTTP bool) (*client, error) {
	issuerURL, ok := absoluteEndpoint(issuer, allowHTTP)
	if !ok || issuerURL.Path == "" || strings.HasSuffix(issuerURL.Path, "/") {
		return nil, errors.New("issuer must be an exact absolute issuer URL")
	}
	resourceURL, ok := absoluteEndpoint(resource, allowHTTP)
	if !ok || resourceURL.Path == "" {
		return nil, errors.New("resource must be an exact absolute URL")
	}
	transport := &http.Transport{DisableCompression: true, MaxIdleConns: 10,
		MaxIdleConnsPerHost: 10, IdleConnTimeout: 30 * time.Second}
	if issuerURL.Scheme == "https" || resourceURL.Scheme == "https" {
		pem, err := os.ReadFile(certFile)
		if err != nil {
			return nil, fmt.Errorf("reading server certificate: %w", err)
		}
		roots := x509.NewCertPool()
		if !roots.AppendCertsFromPEM(pem) {
			return nil, errors.New("server certificate contains no certificates")
		}
		transport.TLSClientConfig = &tls.Config{RootCAs: roots,
			MinVersion: tls.VersionTLS12, NextProtos: []string{"http/1.1"}}
	}
	return &client{issuer: issuerURL.String(), clientID: clientID, scope: scope,
		resource: resourceURL.String(), browser: browser, certFile: certFile,
		http: &http.Client{Transport: transport, Timeout: timeout}}, nil
}

func readJSON(response *http.Response, value any) error {
	defer response.Body.Close()
	limited := &io.LimitedReader{R: response.Body, N: responseBodyMax + 1}
	data, err := io.ReadAll(limited)
	if err != nil || limited.N <= 0 {
		return errors.New("invalid or oversized JSON response")
	}
	if err := uniqueJSON(data); err != nil {
		return err
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		return err
	}
	var extra any
	if decoder.Decode(&extra) != io.EOF {
		return errors.New("invalid JSON response")
	}
	return nil
}

func uniqueJSON(data []byte) error {
	decoder := json.NewDecoder(bytes.NewReader(data))
	var value func() error
	value = func() error {
		token, err := decoder.Token()
		if err != nil {
			return err
		}
		delim, ok := token.(json.Delim)
		if !ok {
			return nil
		}
		switch delim {
		case '{':
			seen := make(map[string]bool)
			for decoder.More() {
				keyToken, err := decoder.Token()
				if err != nil {
					return err
				}
				key, ok := keyToken.(string)
				if !ok || seen[key] {
					return errors.New("duplicate or invalid JSON object key")
				}
				seen[key] = true
				if err := value(); err != nil {
					return err
				}
			}
			_, err = decoder.Token()
			return err
		case '[':
			for decoder.More() {
				if err := value(); err != nil {
					return err
				}
			}
			_, err = decoder.Token()
			return err
		default:
			return errors.New("invalid JSON delimiter")
		}
	}
	if err := value(); err != nil {
		return err
	}
	if _, err := decoder.Token(); err != io.EOF {
		return errors.New("invalid trailing JSON data")
	}
	return nil
}

func (c *client) discover() error {
	issuerURL, _ := url.Parse(c.issuer)
	metadataURL := issuerURL.Scheme + "://" + issuerURL.Host +
		"/.well-known/oauth-authorization-server" + issuerURL.EscapedPath()
	response, err := c.http.Get(metadataURL)
	if err != nil {
		return err
	}
	if response.StatusCode != http.StatusOK {
		response.Body.Close()
		return fmt.Errorf("metadata returned %d", response.StatusCode)
	}
	var value metadata
	if err := readJSON(response, &value); err != nil {
		return err
	}
	if value.Issuer != c.issuer || !contains(value.Responses, "code") ||
		!contains(value.Grants, "authorization_code") || !contains(value.Grants, "refresh_token") ||
		!contains(value.Challenges, "S256") || !contains(value.Scopes, c.scope) ||
		!contains(value.TokenAuthMethods, "none") || !contains(value.RevokeAuthMethods, "none") {
		return errors.New("unsupported authorization-server metadata")
	}
	for _, endpoint := range []string{value.Authorization, value.Token, value.Revoke, value.Keys} {
		if _, ok := absoluteEndpoint(endpoint, issuerURL.Scheme == "http"); !ok {
			return errors.New("invalid metadata endpoint")
		}
	}
	c.metadata = value
	return nil
}

func randomValue() (string, error) {
	var value [32]byte
	if _, err := io.ReadFull(rand.Reader, value[:]); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(value[:]), nil
}

func exactCallback(values url.Values) bool {
	allowed := map[string]bool{"code": true, "state": true, "iss": true, "error": true,
		"error_description": true}
	for key, value := range values {
		if !allowed[key] || len(value) != 1 {
			return false
		}
	}
	return len(values["state"]) == 1 && len(values["iss"]) == 1 &&
		(len(values["code"]) == 1) != (len(values["error"]) == 1)
}

func (c *client) launch(authorizeURL string) error {
	command := exec.Command(c.browser, authorizeURL)
	command.Env = append(os.Environ(), "ZREST_CA="+c.certFile)
	return command.Run()
}

func (c *client) authorize(ctx context.Context) error {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return err
	}
	defer listener.Close()
	verifier, err := randomValue()
	if err != nil {
		return err
	}
	state, err := randomValue()
	if err != nil {
		return err
	}
	sum := sha256.Sum256([]byte(verifier))
	redirect := "http://" + listener.Addr().String() + "/callback"
	authorizeURL, _ := url.Parse(c.metadata.Authorization)
	query := authorizeURL.Query()
	query.Set("response_type", "code")
	query.Set("client_id", c.clientID)
	query.Set("redirect_uri", redirect)
	query.Set("scope", c.scope)
	query.Set("state", state)
	query.Set("code_challenge", base64.RawURLEncoding.EncodeToString(sum[:]))
	query.Set("code_challenge_method", "S256")
	authorizeURL.RawQuery = query.Encode()
	result := make(chan callbackResult, 1)
	mux := http.NewServeMux()
	mux.HandleFunc("GET /callback", func(w http.ResponseWriter, r *http.Request) {
		values, parseErr := url.ParseQuery(r.URL.RawQuery)
		if parseErr != nil || !exactCallback(values) {
			w.WriteHeader(http.StatusBadRequest)
			return
		}
		select {
		case result <- callbackResult{values.Get("code"), values.Get("state"),
			values.Get("iss"), values.Get("error")}:
			w.Header().Set("Content-Type", "text/plain; charset=utf-8")
			_, _ = io.WriteString(w, "Authorization complete. You may close this window.\n")
		default:
			w.WriteHeader(http.StatusGone)
		}
	})
	server := &http.Server{Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	serveDone := make(chan error, 1)
	go func() { serveDone <- server.Serve(listener) }()
	if err := c.launch(authorizeURL.String()); err != nil {
		_ = server.Close()
		return err
	}
	var callback callbackResult
	select {
	case callback = <-result:
	case <-ctx.Done():
		_ = server.Close()
		return ctx.Err()
	}
	_ = server.Shutdown(context.Background())
	<-serveDone
	if callback.state != state || callback.issuer != c.issuer || callback.oauthError != "" || callback.code == "" {
		return errors.New("invalid authorization response")
	}
	form := url.Values{"grant_type": {"authorization_code"}, "code": {callback.code},
		"redirect_uri": {redirect}, "client_id": {c.clientID}, "code_verifier": {verifier}}
	verifier = ""
	callback.code = ""
	return c.exchange(form, false)
}

func (c *client) exchange(form url.Values, refresh bool) error {
	request, err := http.NewRequest(http.MethodPost, c.metadata.Token,
		strings.NewReader(form.Encode()))
	if err != nil {
		return err
	}
	request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	response, err := c.http.Do(request)
	if err != nil {
		return err
	}
	if response.StatusCode != http.StatusOK {
		var failure oauthError
		_ = readJSON(response, &failure)
		return fmt.Errorf("token endpoint returned %d (%s)", response.StatusCode, failure.Error)
	}
	var tokens tokenResponse
	if err := readJSON(response, &tokens); err != nil {
		return err
	}
	if tokens.AccessToken == "" || len(tokens.AccessToken) > tokenValueMax ||
		tokens.TokenType != "Bearer" || tokens.ExpiresIn <= 0 || tokens.Scope != c.scope ||
		(!refresh && tokens.RefreshToken == "") || len(tokens.RefreshToken) > tokenValueMax {
		return errors.New("invalid token response")
	}
	if tokens.RefreshToken == "" {
		tokens.RefreshToken = c.refreshToken
	}
	c.accessToken, c.refreshToken = tokens.AccessToken, tokens.RefreshToken
	c.accessExpires = time.Now().Add(time.Duration(tokens.ExpiresIn) * time.Second)
	return nil
}

func (c *client) refresh() error {
	if c.refreshToken == "" {
		return errors.New("refresh token unavailable")
	}
	form := url.Values{"grant_type": {"refresh_token"}, "refresh_token": {c.refreshToken},
		"client_id": {c.clientID}}
	return c.exchange(form, true)
}

func (c *client) ping() error {
	if !time.Now().Before(c.accessExpires) {
		if err := c.refresh(); err != nil {
			return err
		}
	}
	for replayed := false; ; replayed = true {
		resource, _ := url.Parse(c.resource)
		query := resource.Query()
		query.Set("ping", "true")
		resource.RawQuery = query.Encode()
		request, err := http.NewRequest(http.MethodGet, resource.String(), nil)
		if err != nil {
			return err
		}
		request.Header.Set("Authorization", "Bearer "+c.accessToken)
		response, err := c.http.Do(request)
		if err != nil {
			return err
		}
		if response.StatusCode == http.StatusUnauthorized && !replayed {
			response.Body.Close()
			if err := c.refresh(); err != nil {
				return err
			}
			continue
		}
		if response.StatusCode != http.StatusOK {
			response.Body.Close()
			return fmt.Errorf("resource returned %d", response.StatusCode)
		}
		var value struct {
			Pong bool `json:"pong"`
		}
		if err := readJSON(response, &value); err != nil || !value.Pong {
			return errors.New("invalid resource response")
		}
		return nil
	}
}

func (c *client) revoke() {
	if c.refreshToken == "" {
		return
	}
	form := url.Values{"token": {c.refreshToken}, "token_type_hint": {"refresh_token"},
		"client_id": {c.clientID}}
	request, err := http.NewRequest(http.MethodPost, c.metadata.Revoke,
		bytes.NewBufferString(form.Encode()))
	if err == nil {
		request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
		if response, requestErr := c.http.Do(request); requestErr == nil {
			response.Body.Close()
		}
	}
	c.accessToken, c.refreshToken = "", ""
}

func main() {
	issuer := flag.String("issuer", "https://localhost:8443/oauth2/ping", "exact OAuth issuer URL")
	clientID := flag.String("client-id", "zrest-go", "public OAuth client ID")
	scope := flag.String("scope", "ping", "requested OAuth scope")
	resource := flag.String("resource", "https://localhost:8443/api/ping", "exact resource URL")
	cert := flag.String("cert", "server.crt", "server CA certificate")
	browser := flag.String("browser", "xdg-open", "external user-agent launcher")
	devHTTP := flag.Bool("dev-http", false, "permit HTTP loopback development endpoints")
	requests := flag.Uint("requests", 1, "number of pings")
	interval := durationValue{allowZero: true}
	timeout := durationValue{d: 10 * time.Second}
	callbackTimeout := durationValue{d: 30 * time.Second}
	flag.Var(&interval, "interval", "interval between pings")
	flag.Var(&timeout, "timeout", "HTTP timeout")
	flag.Var(&callbackTimeout, "callback-timeout", "authorization callback timeout")
	flag.Parse()
	if *requests == 0 || timeout.d <= 0 || callbackTimeout.d <= 0 || *clientID == "" || *scope == "" {
		flag.Usage()
		os.Exit(2)
	}
	client, err := newClient(*issuer, *clientID, *scope, *resource, *cert,
		*browser, timeout.d, *devHTTP)
	if err == nil {
		err = client.discover()
	}
	if err == nil {
		ctx, cancel := context.WithTimeout(context.Background(), callbackTimeout.d)
		err = client.authorize(ctx)
		cancel()
	}
	for i := uint(0); err == nil && i < *requests; i++ {
		err = client.ping()
		if err == nil && i+1 < *requests && interval.d != 0 {
			time.Sleep(interval.d)
		}
	}
	if client != nil {
		client.revoke()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "zrest Go client:", err)
		os.Exit(1)
	}
}
