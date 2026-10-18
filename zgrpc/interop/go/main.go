// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

package main

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"zgrpc-interop/pb"
)

func main() {
	mode := flag.String("mode", "server", "server or client")
	listen := flag.String("listen", "127.0.0.1:0", "server TCP address")
	target := flag.String("target", "", "client host:port")
	cert := flag.String("cert", "", "server certificate PEM")
	key := flag.String("key", "", "server key PEM")
	ca := flag.String("ca", "", "client trust PEM")
	name := flag.String("server-name", "localhost", "TLS server name")
	selected := flag.String("case", "all", "client matrix case")
	native := flag.Bool("native", false, "test an identity-only native peer")
	timeout := flag.Duration("timeout", 30*time.Second, "per-case deadline")
	flag.Parse()
	switch *mode {
	case "server":
		creds, err := credentials.NewServerTLSFromFile(*cert, *key)
		if err != nil {
			log.Fatal(err)
		}
		listener, err := net.Listen("tcp", *listen)
		if err != nil {
			log.Fatal(err)
		}
		server := grpc.NewServer(grpc.Creds(creds), grpc.MaxRecvMsgSize(messageLimit),
			grpc.MaxSendMsgSize(messageLimit))
		pb.RegisterMatrixServer(server, &matrix{})
		ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		defer cancel()
		go func() { <-ctx.Done(); server.Stop() }()
		fmt.Printf("READY %s\n", listener.Addr())
		if err := server.Serve(listener); err != nil && err != grpc.ErrServerStopped {
			log.Fatal(err)
		}
	case "client":
		pem, err := os.ReadFile(*ca)
		if err != nil {
			log.Fatal(err)
		}
		roots := x509.NewCertPool()
		if !roots.AppendCertsFromPEM(pem) {
			log.Fatal("empty CA file")
		}
		creds := credentials.NewTLS(&tls.Config{RootCAs: roots, ServerName: *name,
			MinVersion: tls.VersionTLS12})
		conn, err := grpc.NewClient(*target, grpc.WithTransportCredentials(creds),
			grpc.WithDisableRetry(), grpc.WithDefaultCallOptions(
				grpc.MaxCallRecvMsgSize(messageLimit), grpc.MaxCallSendMsgSize(messageLimit)))
		if err != nil {
			log.Fatal(err)
		}
		defer conn.Close()
		var run []peerCase
		for _, test := range cases(conn, *native) {
			if *selected == "all" || *selected == test.name {
				run = append(run, test)
			}
		}
		if len(run) == 0 {
			log.Fatal("unknown case")
		}
		fmt.Printf("TAP version 13\n1..%d\n", len(run))
		failed := false
		for i, test := range run {
			ctx, cancel := context.WithTimeout(context.Background(), *timeout)
			err := test.run(ctx, pb.NewMatrixClient(conn))
			cancel()
			if err != nil {
				failed = true
				fmt.Printf("not ok %d - %s\n# %s\n", i+1, test.name,
					strings.ReplaceAll(err.Error(), "\n", "\n# "))
			} else {
				fmt.Printf("ok %d - %s\n", i+1, test.name)
			}
		}
		if failed {
			os.Exit(1)
		}
	default:
		log.Fatal("mode must be server or client")
	}
}
