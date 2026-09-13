// zrestua is the deterministic external user agent used by integration tests.
// It owns the demonstration credentials; the OAuth client never sees them.
package main

import (
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"net/http"
	"net/http/cookiejar"
	"net/url"
	"os"
	"strings"
	"time"
)

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: zrestua AUTHORIZATION_URL")
		os.Exit(2)
	}
	ca, err := os.ReadFile(os.Getenv("ZREST_CA"))
	if err != nil {
		panic(err)
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(ca) {
		panic(errors.New("invalid authorization-server CA"))
	}
	jar, _ := cookiejar.New(nil)
	client := &http.Client{Jar: jar, Timeout: 15 * time.Second,
		Transport: &http.Transport{DisableCompression: true, ForceAttemptHTTP2: true,
			TLSClientConfig: &tls.Config{RootCAs: roots,
				MinVersion: tls.VersionTLS12}}}
	response, err := client.Get(os.Args[1])
	if err != nil {
		panic(err)
	}
	response.Body.Close()
	if response.StatusCode != http.StatusOK {
		panic(fmt.Errorf("authorization page returned %d", response.StatusCode))
	}
	authorize, err := url.Parse(os.Args[1])
	if err != nil {
		panic(err)
	}
	authorize.RawQuery = ""
	form := url.Values{"username": {"test"}, "password": {"test123"},
		"decision": {"approve"}}
	request, err := http.NewRequest(http.MethodPost, authorize.String(),
		strings.NewReader(form.Encode()))
	if err != nil {
		panic(err)
	}
	request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	response, err = client.Do(request)
	if err != nil {
		panic(err)
	}
	response.Body.Close()
	if response.StatusCode != http.StatusOK {
		panic(fmt.Errorf("authorization completion returned %d", response.StatusCode))
	}
}
