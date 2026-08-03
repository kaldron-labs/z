//  -*- mode:c++; indent-tabs-mode:t; tab-width:4; c-basic-offset:4; -*-
//  vi: noet ts=4 sw=4 cino=+0,(s,l1,m1,j1,U1,W4

package main

import (
	"crypto/tls"
	"encoding/json"
	"log"
	"net/http"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"github.com/gorilla/mux"
)

var jwtKey = []byte("your_secret_key")

type Credentials struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type Claims struct {
	Username string `json:"username"`
	jwt.RegisteredClaims
}

type Response struct {
	Message string `json:"message"`
	Data    string `json:"data"`
}

type TLSConfig struct {
	CertFile string
	KeyFile  string
}

func main() {
	r := mux.NewRouter()

	// Route handlers
	r.HandleFunc("/api/auth", handleAuth).Methods("POST")
	r.HandleFunc("/api/protected", authMiddleware(handleProtected)).Methods("GET")

	tlsConfig := &TLSConfig{
		CertFile: "server.crt",
		KeyFile:  "server.key",
	}

	server := &http.Server{
		Addr:    ":8443",
		Handler: r,
		TLSConfig: &tls.Config{
			MinVersion: tls.VersionTLS12,
			CurvePreferences: []tls.CurveID{
				tls.CurveP256,
				tls.X25519,
			},
			PreferServerCipherSuites: true,
			CipherSuites: []uint16{
				tls.TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
				tls.TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
				tls.TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305,
				tls.TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305,
				tls.TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
				tls.TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
			},
		},
	}

	log.Printf("Starting HTTPS server on port 8443")
	log.Fatal(server.ListenAndServeTLS(tlsConfig.CertFile, tlsConfig.KeyFile))
}

func handleAuth(w http.ResponseWriter, r *http.Request) {
	var creds Credentials
	if err := json.NewDecoder(r.Body).Decode(&creds); err != nil {
		w.WriteHeader(http.StatusBadRequest)
		return
	}

	// In a real application, validate credentials against a database
	if creds.Username != "test" || creds.Password != "test123" {
		w.WriteHeader(http.StatusUnauthorized)
		return
	}

	expirationTime := time.Now().Add(24 * time.Hour)
	claims := &Claims{
		Username: creds.Username,
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(expirationTime),
			IssuedAt:  jwt.NewNumericDate(time.Now()),
			NotBefore: jwt.NewNumericDate(time.Now()),
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenString, err := token.SignedString(jwtKey)
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"token": tokenString,
	})
}

func authMiddleware(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		tokenString := r.Header.Get("Authorization")
		if tokenString == "" {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}

		claims := &Claims{}
		token, err := jwt.ParseWithClaims(tokenString, claims, func(token *jwt.Token) (interface{}, error) {
			return jwtKey, nil
		})

		if err != nil || !token.Valid {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}

		next.ServeHTTP(w, r)
	}
}

func handleProtected(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	response := Response{
		Message: "Success",
		Data:    "This is protected data",
	}
	json.NewEncoder(w).Encode(response)
}
