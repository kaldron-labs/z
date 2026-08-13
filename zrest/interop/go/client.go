//  -*- mode:c++; indent-tabs-mode:t; tab-width:4; c-basic-offset:4; -*-
//  vi: noet ts=4 sw=4 cino=+0,(s,l1,m1,j1,U1,W4

package main

import (
	"bytes"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"time"
)

const (
	respBodyMax = 1 << 20
	jwtMax      = 3*4096 + 2
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

type Credentials struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type RefreshRequest struct {
	RefreshToken string `json:"refresh_token"`
}

type TokenResponse struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresIn    int64  `json:"expires_in"`
}

type Ping struct {
	Ping bool `json:"ping"`
}
type Pong struct {
	Pong bool `json:"pong"`
}

type Client struct {
	BaseURL         string
	HTTPClient      *http.Client
	AccessToken     string
	RefreshToken    string
	AccessExpiresAt time.Time
}

func newClient(baseURL, certFile string, timeout time.Duration) (*Client, error) {
	u, err := url.Parse(baseURL)
	if err != nil || u.Scheme != "https" && u.Scheme != "http" || u.Host == "" ||
		u.User != nil || u.RawQuery != "" || u.Fragment != "" || u.Path != "" && u.Path != "/" {
		return nil, errors.New("URL must be an http(s) origin")
	}
	u.Path = ""
	transport := &http.Transport{
		DisableCompression: true, MaxIdleConns: 10, MaxIdleConnsPerHost: 10,
		IdleConnTimeout: 30 * time.Second,
	}
	if u.Scheme == "https" {
		caCert, err := os.ReadFile(certFile)
		if err != nil {
			return nil, fmt.Errorf("reading server certificate: %w", err)
		}
		caCertPool := x509.NewCertPool()
		if !caCertPool.AppendCertsFromPEM(caCert) {
			return nil, errors.New("server certificate contains no certificates")
		}
		transport.TLSClientConfig = &tls.Config{
			RootCAs: caCertPool, MinVersion: tls.VersionTLS12,
			NextProtos: []string{"http/1.1"},
		}
	}
	return &Client{BaseURL: u.String(), HTTPClient: &http.Client{
		Transport: transport, Timeout: timeout,
	}}, nil
}

func (c *Client) doJSON(method, path string, in, out any, bearer string) (int, error) {
	var body io.Reader
	if in != nil {
		data, err := json.Marshal(in)
		if err != nil {
			return 0, err
		}
		body = bytes.NewReader(data)
	}
	req, err := http.NewRequest(method, c.BaseURL+path, body)
	if err != nil {
		return 0, err
	}
	if in != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	if bearer != "" {
		req.Header.Set("Authorization", "Bearer "+bearer)
	}
	resp, err := c.HTTPClient.Do(req)
	if err != nil {
		return 0, err
	}
	defer resp.Body.Close()
	limited := &io.LimitedReader{R: resp.Body, N: respBodyMax + 1}
	if resp.StatusCode != http.StatusOK {
		data, err := io.ReadAll(limited)
		if err != nil {
			return resp.StatusCode, err
		}
		if len(data) > respBodyMax {
			return resp.StatusCode, errors.New("response body too large")
		}
		return resp.StatusCode, nil
	}
	if out == nil {
		return resp.StatusCode, errors.New("unexpected response body")
	}
	decoder := json.NewDecoder(limited)
	if err := decoder.Decode(out); err != nil {
		return resp.StatusCode, err
	}
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		return resp.StatusCode, errors.New("multiple JSON values")
	}
	if limited.N <= 0 {
		return resp.StatusCode, errors.New("response body too large")
	}
	return resp.StatusCode, nil
}

func (c *Client) installTokens(tokens TokenResponse, now time.Time) error {
	if tokens.AccessToken == "" || tokens.RefreshToken == "" ||
		len(tokens.AccessToken) > jwtMax || len(tokens.RefreshToken) > jwtMax || tokens.ExpiresIn <= 0 {
		return errors.New("invalid token response")
	}
	if tokens.ExpiresIn > int64((1<<63-1)/time.Second) {
		return errors.New("token expiry overflows")
	}
	d := time.Duration(tokens.ExpiresIn) * time.Second
	expires := now.Add(d)
	if !expires.After(now) {
		return errors.New("token expiry overflows")
	}
	c.AccessToken, c.RefreshToken, c.AccessExpiresAt = tokens.AccessToken, tokens.RefreshToken, expires
	return nil
}

func (c *Client) Authenticate(username, password string) error {
	var tokens TokenResponse
	status, err := c.doJSON(http.MethodPost, "/api/auth", Credentials{username, password}, &tokens, "")
	if err != nil {
		return fmt.Errorf("authentication: %w", err)
	}
	if status != http.StatusOK {
		return fmt.Errorf("authentication returned %d", status)
	}
	return c.installTokens(tokens, time.Now())
}

func (c *Client) Refresh() error {
	var tokens TokenResponse
	status, err := c.doJSON(http.MethodPost, "/api/refresh", RefreshRequest{c.RefreshToken}, &tokens, "")
	if err != nil {
		return fmt.Errorf("refresh: %w", err)
	}
	if status != http.StatusOK {
		return fmt.Errorf("refresh returned %d", status)
	}
	return c.installTokens(tokens, time.Now())
}

func (c *Client) Ping() error {
	if !time.Now().Before(c.AccessExpiresAt) {
		if err := c.Refresh(); err != nil {
			return err
		}
	}
	ping := Ping{Ping: true}
	query := url.Values{}
	query.Set("ping", fmt.Sprint(ping.Ping))
	for replayed := false; ; replayed = true {
		var pong Pong
		status, err := c.doJSON(http.MethodGet, "/?"+query.Encode(), nil, &pong, c.AccessToken)
		if err != nil {
			return fmt.Errorf("ping: %w", err)
		}
		if status == http.StatusUnauthorized && !replayed {
			if err := c.Refresh(); err != nil {
				return err
			}
			continue
		}
		if status != http.StatusOK {
			return fmt.Errorf("ping returned %d", status)
		}
		if !pong.Pong {
			return errors.New("ping returned false pong")
		}
		return nil
	}
}

func main() {
	baseURL := flag.String("url", "https://localhost:8443", "server origin")
	cert := flag.String("cert", "server.crt", "server CA certificate")
	user := flag.String("user", "test", "username")
	password := flag.String("pass", "test123", "password")
	requests := flag.Uint("requests", 1, "number of pings")
	interval := durationValue{allowZero: true}
	timeout := durationValue{d: 10 * time.Second}
	flag.Var(&interval, "interval", "interval between pings")
	flag.Var(&timeout, "timeout", "HTTP timeout")
	flag.Parse()
	if *requests == 0 || timeout.d <= 0 {
		flag.Usage()
		os.Exit(2)
	}
	client, err := newClient(*baseURL, *cert, timeout.d)
	if err == nil {
		err = client.Authenticate(*user, *password)
	}
	for i := uint(0); err == nil && i < *requests; i++ {
		err = client.Ping()
		if err == nil && i+1 < *requests && interval.d != 0 {
			time.Sleep(interval.d)
		}
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "zrest Go client:", err)
		os.Exit(1)
	}
}
