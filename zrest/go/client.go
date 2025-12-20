//  -*- mode:c++; indent-tabs-mode:t; tab-width:4; c-basic-offset:4; -*-
//  vi: noet ts=4 sw=4 cino=+0,(s,l1,m1,j1,U1,W4

package main

import (
	"bytes"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"time"
)

type Credentials struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type AuthResponse struct {
	Token string `json:"token"`
}

type ProtectedResponse struct {
	Message string `json:"message"`
	Data    string `json:"data"`
}

type Client struct {
	BaseURL    string
	HTTPClient *http.Client
	Token      string
}

func NewClient(certFile string) (*Client, error) {
	caCert, err := os.ReadFile(certFile)
	if err != nil {
		return nil, fmt.Errorf("reading server certificate: %w", err)
	}

	caCertPool := x509.NewCertPool()
	caCertPool.AppendCertsFromPEM(caCert)

	tlsConfig := &tls.Config{
		RootCAs:            caCertPool,
		MinVersion:         tls.VersionTLS12,
		CurvePreferences:   []tls.CurveID{tls.CurveP256, tls.X25519},
		InsecureSkipVerify: false,
	}

	transport := &http.Transport{
		TLSClientConfig:     tlsConfig,
		DisableCompression:  true,
		MaxIdleConns:        10,
		IdleConnTimeout:     30 * time.Second,
		DisableKeepAlives:   false,
		ForceAttemptHTTP2:   true,
		MaxIdleConnsPerHost: 10,
	}

	return &Client{
		BaseURL: "https://localhost:8443",
		HTTPClient: &http.Client{
			Transport: transport,
			Timeout:   10 * time.Second,
		},
	}, nil
}

func (c *Client) Authenticate(username, password string) error {
	creds := Credentials{
		Username: username,
		Password: password,
	}

	jsonData, err := json.Marshal(creds)
	if err != nil {
		return fmt.Errorf("marshaling credentials: %w", err)
	}

	resp, err := c.HTTPClient.Post(
		c.BaseURL+"/api/auth",
		"application/json",
		bytes.NewBuffer(jsonData),
	)
	if err != nil {
		return fmt.Errorf("authentication request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("authentication failed with status %d: %s", resp.StatusCode, string(body))
	}

	var authResp AuthResponse
	if err := json.NewDecoder(resp.Body).Decode(&authResp); err != nil {
		return fmt.Errorf("decoding auth response: %w", err)
	}

	c.Token = authResp.Token
	return nil
}

func (c *Client) GetProtectedData() (*ProtectedResponse, error) {
	if c.Token == "" {
		return nil, fmt.Errorf("no authentication token available")
	}

	req, err := http.NewRequest(http.MethodGet, c.BaseURL+"/api/protected", nil)
	if err != nil {
		return nil, fmt.Errorf("creating request: %w", err)
	}

	req.Header.Set("Authorization", c.Token)

	{
		var buf bytes.Buffer
		req.Write(&buf)
		fmt.Println(buf.String())
	}

	resp, err := c.HTTPClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("protected request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("protected request failed with status %d: %s", resp.StatusCode, string(body))
	}

	var protectedResp ProtectedResponse
	if err := json.NewDecoder(resp.Body).Decode(&protectedResp); err != nil {
		return nil, fmt.Errorf("decoding protected response: %w", err)
	}

	return &protectedResp, nil
}

func main() {
	certFile := flag.String("cert", "server.crt", "path to server certificate")
	username := flag.String("user", "test", "username for authentication")
	password := flag.String("pass", "test123", "password for authentication")
	flag.Parse()

	client, err := NewClient(*certFile)
	if err != nil {
		log.Fatalf("Creating client: %v", err)
	}

	log.Printf("Authenticating user: %s", *username)
	if err := client.Authenticate(*username, *password); err != nil {
		log.Fatalf("Authentication failed: %v", err)
	}
	log.Printf("Authentication successful")

	log.Printf("Accessing protected endpoint")
	resp, err := client.GetProtectedData()
	if err != nil {
		log.Fatalf("Accessing protected data: %v", err)
	}

	log.Printf("Protected Response: %+v", resp)
}
