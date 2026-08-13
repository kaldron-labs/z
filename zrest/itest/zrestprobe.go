// zrestprobe exercises authentication failures against either example server.
package main

import (
	"bytes"
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"
)

const bodyMax = 1 << 20

type credentials struct {
	Username string `json:"username"`
	Password string `json:"password"`
}
type refreshRequest struct {
	RefreshToken string `json:"refresh_token"`
}
type tokenResponse struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresIn    int64  `json:"expires_in"`
}

func client(cert string) (*http.Client, error) {
	pem, err := os.ReadFile(cert)
	if err != nil {
		return nil, err
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(pem) {
		return nil, errors.New("invalid CA")
	}
	return &http.Client{Timeout: 5 * time.Second, Transport: &http.Transport{
		TLSClientConfig: &tls.Config{RootCAs: roots, MinVersion: tls.VersionTLS12,
			NextProtos: []string{"http/1.1"}},
		DisableCompression: true,
	}}, nil
}

func request(ctx context.Context, c *http.Client, method, url string,
	body any, headers [][2]string) (int, []byte, error) {
	var data []byte
	var err error
	if body != nil {
		data, err = json.Marshal(body)
		if err != nil {
			return 0, nil, err
		}
	}
	req, err := http.NewRequestWithContext(ctx, method, url, bytes.NewReader(data))
	if err != nil {
		return 0, nil, err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	for _, h := range headers {
		req.Header.Add(h[0], h[1])
	}
	res, err := c.Do(req)
	if err != nil {
		return 0, nil, err
	}
	defer res.Body.Close()
	b, err := io.ReadAll(io.LimitReader(res.Body, bodyMax+1))
	if err != nil || len(b) > bodyMax {
		return 0, nil, errors.New("response body failure")
	}
	return res.StatusCode, b, nil
}

func verify(raw, secret, tokenType string) error {
	parts := strings.Split(raw, ".")
	if len(parts) != 3 {
		return errors.New("JWT segment count")
	}
	header, err := base64.RawURLEncoding.DecodeString(parts[0])
	if err != nil {
		return err
	}
	var h struct {
		Alg string `json:"alg"`
		Typ string `json:"typ"`
	}
	if json.Unmarshal(header, &h) != nil || h.Alg != "HS256" || h.Typ != "JWT" {
		return errors.New("JWT header")
	}
	payload, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return err
	}
	var claims struct {
		Sub       string `json:"sub"`
		Iat       int64  `json:"iat"`
		Nbf       int64  `json:"nbf"`
		Exp       int64  `json:"exp"`
		JTI       string `json:"jti"`
		TokenType string `json:"token_type"`
	}
	if json.Unmarshal(payload, &claims) != nil || claims.Sub == "" ||
		claims.Iat <= 0 || claims.Nbf <= 0 || claims.Exp <= claims.Iat ||
		len(claims.JTI) != 32 || claims.TokenType != tokenType {
		return errors.New("JWT claims")
	}
	for _, c := range claims.JTI {
		if c < '0' || (c > '9' && c < 'a') || c > 'f' {
			return errors.New("JWT jti")
		}
	}
	sig, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil {
		return err
	}
	mac := hmac.New(sha256.New, []byte(secret))
	_, _ = mac.Write([]byte(parts[0] + "." + parts[1]))
	if !hmac.Equal(sig, mac.Sum(nil)) {
		return errors.New("JWT signature")
	}
	return nil
}

func main() {
	url := flag.String("url", "https://localhost:8443", "server origin")
	cert := flag.String("cert", "server.crt", "server CA")
	secret := flag.String("jwt-secret", "matrix-secret", "JWT secret")
	flag.Parse()
	c, err := client(*cert)
	if err != nil {
		panic(err)
	}
	ctx := context.Background()
	status, body, err := request(ctx, c, "POST", *url+"/api/auth",
		credentials{"test", "test123"}, nil)
	if err != nil || status != 200 {
		panic(fmt.Sprintf("auth: status=%d err=%v", status, err))
	}
	var tokens tokenResponse
	if json.Unmarshal(body, &tokens) != nil || tokens.ExpiresIn != 1 {
		panic("invalid token response")
	}
	if err = verify(tokens.AccessToken, *secret, "access"); err != nil {
		panic(err)
	}
	if err = verify(tokens.RefreshToken, *secret, "refresh"); err != nil {
		panic(err)
	}
	mutated := tokens.AccessToken
	last := mutated[len(mutated)-1]
	if last == 'A' {
		last = 'B'
	} else {
		last = 'A'
	}
	mutated = mutated[:len(mutated)-1] + string(last)
	tests := []struct {
		method, path string
		body         any
		headers      [][2]string
	}{
		{"GET", "/?ping=true", nil, nil},
		{"GET", "/?ping=true", nil, [][2]string{{"Authorization", "Bearer " + tokens.AccessToken}, {"Authorization", "Bearer " + tokens.AccessToken}}},
		{"GET", "/?ping=true", nil, [][2]string{{"Authorization", tokens.AccessToken}}},
		{"GET", "/?ping=true", nil, [][2]string{{"Authorization", "Bearer  " + tokens.AccessToken}}},
		{"GET", "/?ping=true", nil, [][2]string{{"Authorization", "Bearer " + mutated}}},
		{"GET", "/?ping=true", nil, [][2]string{{"Authorization", "Bearer " + tokens.RefreshToken}}},
		{"POST", "/api/refresh", refreshRequest{tokens.AccessToken}, nil},
		{"POST", "/api/auth", credentials{"bad", "bad"}, nil},
		{"POST", "/api/auth", credentials{"", ""}, nil},
		{"POST", "/api/refresh", refreshRequest{""}, nil},
	}
	for i, test := range tests {
		status, _, err = request(ctx, c, test.method, *url+test.path, test.body, test.headers)
		if err != nil || status != 401 {
			panic(fmt.Sprintf("case %d: status=%d err=%v", i, status, err))
		}
	}
	time.Sleep(2 * time.Second)
	status, _, err = request(ctx, c, "GET", *url+"/?ping=true", nil,
		[][2]string{{"Authorization", "Bearer " + tokens.AccessToken}})
	if err != nil || status != 401 {
		panic(fmt.Sprintf("expired: status=%d err=%v", status, err))
	}
}
