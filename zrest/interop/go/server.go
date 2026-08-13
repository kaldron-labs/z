//  -*- mode:c++; indent-tabs-mode:t; tab-width:4; c-basic-offset:4; -*-
//  vi: noet ts=4 sw=4 cino=+0,(s,l1,m1,j1,U1,W4

package main

import (
	"context"
	"crypto/rand"
	"crypto/tls"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
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
	reqBodyMax  = 1 << 20
	respBodyMax = 1 << 20
	jwtPartMax  = 4096
	jwtMax      = 3*jwtPartMax + 2
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
type Claims struct {
	TokenType string `json:"token_type"`
	jwt.RegisteredClaims
}

type app struct {
	secret                          []byte
	user, password                  string
	accessLifetime, refreshLifetime time.Duration
	requestLimit                    uint64
	verbose                         bool
	auth, refresh, pong             atomic.Uint64
	stopOnce                        sync.Once
	stop                            chan struct{}
	fatal                           atomic.Bool
}

func decodeBody(w http.ResponseWriter, r *http.Request, out any) bool {
	r.Body = http.MaxBytesReader(w, r.Body, reqBodyMax)
	decoder := json.NewDecoder(r.Body)
	if decoder.Decode(out) != nil {
		return false
	}
	var extra any
	return decoder.Decode(&extra) == io.EOF
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

func (a *app) fail() { a.fatal.Store(true); a.stopOnce.Do(func() { close(a.stop) }) }

func (a *app) issueToken(subject, tokenType string, lifetime time.Duration, now time.Time) (string, error) {
	var id [16]byte
	for id == [16]byte{} {
		if _, err := io.ReadFull(rand.Reader, id[:]); err != nil {
			return "", err
		}
	}
	sec := now.Unix()
	exp := now.Add(lifetime).Unix()
	if sec <= 0 || exp <= sec {
		return "", errors.New("unrepresentable token lifetime")
	}
	claims := Claims{TokenType: tokenType, RegisteredClaims: jwt.RegisteredClaims{
		Subject: subject, ID: hex.EncodeToString(id[:]),
		IssuedAt: jwt.NewNumericDate(time.Unix(sec, 0)), NotBefore: jwt.NewNumericDate(time.Unix(sec, 0)),
		ExpiresAt: jwt.NewNumericDate(time.Unix(exp, 0)),
	}}
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	token.Header["typ"] = "JWT"
	raw, err := token.SignedString(a.secret)
	if err == nil {
		parts := strings.Split(raw, ".")
		if len(raw) > jwtMax || len(parts) != 3 ||
			len(parts[0]) > jwtPartMax || len(parts[1]) > jwtPartMax || len(parts[2]) > jwtPartMax {
			err = errors.New("generated JWT too large")
		}
	}
	return raw, err
}

func (a *app) issueTokenPair(subject string) (TokenResponse, error) {
	now := time.Now().UTC().Truncate(time.Second)
	access, err := a.issueToken(subject, "access", a.accessLifetime, now)
	if err != nil {
		return TokenResponse{}, err
	}
	refresh, err := a.issueToken(subject, "refresh", a.refreshLifetime, now)
	return TokenResponse{access, refresh, int64(a.accessLifetime / time.Second)}, err
}

func validJTI(s string) bool {
	if len(s) != 32 {
		return false
	}
	for _, c := range s {
		if c < '0' || c > '9' && (c < 'a' || c > 'f') {
			return false
		}
	}
	return true
}

func (a *app) parseToken(raw, requiredType string) (*Claims, error) {
	if len(raw) > jwtMax {
		return nil, errors.New("JWT too large")
	}
	parts := strings.Split(raw, ".")
	if len(parts) != 3 {
		return nil, errors.New("invalid JWT segments")
	}
	for _, part := range parts {
		if part == "" || len(part) > jwtPartMax || strings.Contains(part, "=") {
			return nil, errors.New("invalid JWT segment")
		}
	}
	claims := new(Claims)
	token, err := jwt.ParseWithClaims(raw, claims, func(token *jwt.Token) (any, error) {
		if token.Method != jwt.SigningMethodHS256 || token.Header["alg"] != "HS256" || token.Header["typ"] != "JWT" {
			return nil, errors.New("invalid JWT header")
		}
		return a.secret, nil
	}, jwt.WithValidMethods([]string{"HS256"}), jwt.WithExpirationRequired(), jwt.WithStrictDecoding())
	if err != nil || !token.Valid {
		return nil, errors.New("invalid JWT")
	}
	if claims.Subject == "" || claims.TokenType != requiredType || !validJTI(claims.ID) ||
		claims.IssuedAt == nil || claims.NotBefore == nil || claims.ExpiresAt == nil {
		return nil, errors.New("invalid JWT claims")
	}
	now, iat, nbf, exp := time.Now().Unix(), claims.IssuedAt.Unix(), claims.NotBefore.Unix(), claims.ExpiresAt.Unix()
	if iat <= 0 || nbf <= 0 || exp <= 0 || iat >= exp || nbf >= exp || iat > now || nbf > now || now >= exp {
		return nil, errors.New("invalid JWT times")
	}
	return claims, nil
}

func (a *app) wrote(event string, counter *atomic.Uint64) {
	n := counter.Add(1)
	if a.verbose {
		log.Printf("event=%s", event)
	}
	if event == "pong" && a.requestLimit != 0 && n >= a.requestLimit {
		a.stopOnce.Do(func() { close(a.stop) })
	}
}

func (a *app) authHandler(w http.ResponseWriter, r *http.Request) {
	var credentials Credentials
	if !decodeBody(w, r, &credentials) || credentials.Username == "" || credentials.Password == "" || credentials.Username != a.user || credentials.Password != a.password {
		w.WriteHeader(http.StatusUnauthorized)
		return
	}
	tokens, err := a.issueTokenPair(credentials.Username)
	if err != nil || writeJSON(w, http.StatusOK, tokens) != nil {
		if err != nil {
			w.WriteHeader(http.StatusInternalServerError)
		}
		a.fail()
		return
	}
	a.wrote("auth", &a.auth)
}

func (a *app) refreshHandler(w http.ResponseWriter, r *http.Request) {
	var request RefreshRequest
	if !decodeBody(w, r, &request) || request.RefreshToken == "" {
		w.WriteHeader(http.StatusUnauthorized)
		return
	}
	claims, err := a.parseToken(request.RefreshToken, "refresh")
	if err != nil {
		w.WriteHeader(http.StatusUnauthorized)
		return
	}
	tokens, err := a.issueTokenPair(claims.Subject)
	if err != nil || writeJSON(w, http.StatusOK, tokens) != nil {
		if err != nil {
			w.WriteHeader(http.StatusInternalServerError)
		}
		a.fail()
		return
	}
	a.wrote("refresh", &a.refresh)
}

func (a *app) pingHandler(w http.ResponseWriter, r *http.Request) {
	query, err := url.ParseQuery(r.URL.RawQuery)
	ping := Ping{}
	if values := query["ping"]; len(values) == 1 {
		ping.Ping = values[0] == "true"
	}
	values := r.Header.Values("Authorization")
	if err != nil || !ping.Ping || len(values) != 1 || !strings.HasPrefix(values[0], "Bearer ") || strings.Count(values[0], " ") != 1 || len(values[0]) == 7 {
		w.WriteHeader(http.StatusUnauthorized)
		return
	}
	if _, err = a.parseToken(values[0][7:], "access"); err != nil {
		w.WriteHeader(http.StatusUnauthorized)
		return
	}
	if writeJSON(w, http.StatusOK, Pong{true}) != nil {
		a.fail()
		return
	}
	a.wrote("pong", &a.pong)
}

func main() {
	addr := flag.String("addr", ":8443", "listen address")
	cert := flag.String("cert", "server.crt", "TLS certificate")
	key := flag.String("key", "server.key", "TLS private key")
	secret := flag.String("jwt-secret", "your_secret_key", "JWT HMAC secret")
	user := flag.String("user", "test", "accepted username")
	password := flag.String("pass", "test123", "accepted password")
	requests := flag.Uint64("requests", 0, "successful pongs before shutdown")
	verbose := flag.Bool("verbose", false, "emit stable event records")
	access := durationValue{5 * time.Minute}
	refresh := durationValue{24 * time.Hour}
	flag.Var(&access, "access-token-lifetime", "access token lifetime")
	flag.Var(&refresh, "refresh-token-lifetime", "refresh token lifetime")
	flag.Parse()
	if *secret == "" || *user == "" || *password == "" || refresh.d <= access.d {
		flag.Usage()
		os.Exit(2)
	}
	a := &app{secret: []byte(*secret), user: *user, password: *password, accessLifetime: access.d, refreshLifetime: refresh.d, requestLimit: *requests, verbose: *verbose, stop: make(chan struct{})}
	mux := http.NewServeMux()
	mux.HandleFunc("POST /api/auth", a.authHandler)
	mux.HandleFunc("POST /api/refresh", a.refreshHandler)
	mux.HandleFunc("GET /{$}", a.pingHandler)
	server := &http.Server{Addr: *addr, Handler: mux, TLSConfig: &tls.Config{
		MinVersion: tls.VersionTLS12,
		NextProtos: []string{"http/1.1"},
	}}
	pair, err := tls.LoadX509KeyPair(*cert, *key)
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
	log.Printf("summary auth=%d refresh=%d pong=%d", a.auth.Load(), a.refresh.Load(), a.pong.Load())
	if a.fatal.Load() {
		os.Exit(1)
	}
}
