// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Independent gRPC wire peer: Connect codecs/protocol over quic-go HTTP/3.
// This does not route Connect-protocol RPCs or fall back to HTTP/2.
package main

import (
	"bytes"
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"connectrpc.com/connect"
	"github.com/quic-go/quic-go/http3"
	"google.golang.org/protobuf/proto"
	"zgrpc-interop/pb"
)

const path = "/zgrpc.interop.Matrix/"
const limit = 16 << 20

func fields(in, header, trailer http.Header) {
	header.Set("x-peer", "go-h3")
	trailer.Set("x-trailer", "done")
	for _, value := range in.Values("x-test-bin") {
		header.Add("x-echo-bin", value)
		trailer.Add("x-echo-bin", value)
	}
}

func control(ctx context.Context, in *pb.Packet) error {
	if in.Code != 0 {
		code := connect.Code(in.Code)
		if code > connect.CodeUnauthenticated {
			code = connect.CodeInvalidArgument
		}
		return connect.NewError(code, errors.New("requested error"))
	}
	if in.WaitCancel {
		<-ctx.Done()
		return ctx.Err()
	}
	return nil
}

func handler() http.Handler {
	mux := http.NewServeMux()
	opts := []connect.HandlerOption{connect.WithReadMaxBytes(limit), connect.WithSendMaxBytes(limit)}
	mux.Handle(path+"Unary", connect.NewUnaryHandler(path+"Unary",
		func(ctx context.Context, in *connect.Request[pb.Packet]) (*connect.Response[pb.Packet], error) {
			if err := control(ctx, in.Msg); err != nil {
				return nil, err
			}
			out := connect.NewResponse(proto.Clone(in.Msg).(*pb.Packet))
			fields(in.Header(), out.Header(), out.Trailer())
			return out, nil
		}, opts...))
	mux.Handle(path+"ServerStream", connect.NewServerStreamHandler(path+"ServerStream",
		func(ctx context.Context, in *connect.Request[pb.Packet], out *connect.ServerStream[pb.Packet]) error {
			if err := control(ctx, in.Msg); err != nil {
				return err
			}
			if in.Msg.Count > 65536 {
				return connect.NewError(connect.CodeResourceExhausted, errors.New("fixture count limit"))
			}
			fields(in.Header(), out.ResponseHeader(), out.ResponseTrailer())
			for i := uint32(0); i < in.Msg.Count; i++ {
				value := proto.Clone(in.Msg).(*pb.Packet)
				value.Seq += uint64(i)
				if err := out.Send(value); err != nil {
					return err
				}
			}
			return nil
		}, opts...))
	mux.Handle(path+"ClientStream", connect.NewClientStreamHandler(path+"ClientStream",
		func(ctx context.Context, in *connect.ClientStream[pb.Packet]) (*connect.Response[pb.Packet], error) {
			fields(in.RequestHeader(), in.Conn().ResponseHeader(), in.Conn().ResponseTrailer())
			if err := in.Conn().Send(nil); err != nil {
				return nil, err
			}
			out := &pb.Packet{}
			for in.Receive() {
				value := in.Msg()
				if err := control(ctx, value); err != nil {
					return nil, err
				}
				out.Seq += value.Seq
				out.Count++
				out.Payload = value.Payload
			}
			return connect.NewResponse(out), in.Err()
		}, opts...))
	mux.Handle(path+"Duplex", connect.NewBidiStreamHandler(path+"Duplex",
		func(ctx context.Context, stream *connect.BidiStream[pb.Packet, pb.Packet]) error {
			fields(stream.RequestHeader(), stream.ResponseHeader(), stream.ResponseTrailer())
			if err := stream.Send(nil); err != nil {
				return err
			}
			for {
				value, err := stream.Receive()
				if errors.Is(err, io.EOF) {
					return nil
				}
				if err != nil {
					return err
				}
				if err := control(ctx, value); err != nil {
					return err
				}
				if err := stream.Send(value); err != nil {
					return err
				}
			}
		}, opts...))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		contentType := r.Header.Get("Content-Type")
		if r.ProtoMajor != 3 || (contentType != "application/grpc" && contentType != "application/grpc+proto") {
			http.Error(w, "requires gRPC over HTTP/3", http.StatusBadRequest)
			return
		}
		mux.ServeHTTP(w, r)
	})
}

type test struct {
	name string
	run  func(context.Context) error
}

func client(httpClient *http.Client, base string, native bool) bool {
	rpc := func(method string) *connect.Client[pb.Packet, pb.Packet] {
		return connect.NewClient[pb.Packet, pb.Packet](httpClient, base+path+method,
			connect.WithGRPC(), connect.WithReadMaxBytes(limit), connect.WithSendMaxBytes(limit))
	}
	unary := rpc("Unary")
	payload := bytes.Repeat([]byte{0x71}, 256<<10)
	var tests []test
	for _, empty := range []bool{false, true} {
		tests = append(tests, test{fmt.Sprintf("unary-empty-%t", empty), func(ctx context.Context) error {
			in := &pb.Packet{}
			if !empty {
				in.Seq, in.Payload = 42, payload
			}
			out, err := unary.CallUnary(ctx, connect.NewRequest(in))
			if err == nil && !proto.Equal(in, out.Msg) {
				return errors.New("unary mismatch")
			}
			return err
		}})
	}
	for _, count := range []uint32{3, 0} {
		tests = append(tests, test{fmt.Sprintf("server-stream-%d", count), func(ctx context.Context) error {
			stream, err := rpc("ServerStream").CallServerStream(ctx, connect.NewRequest(&pb.Packet{Seq: 42, Count: count, Payload: payload}))
			if err != nil {
				return err
			}
			defer stream.Close()
			var n uint32
			for stream.Receive() {
				out := stream.Msg()
				if out.Seq != 42+uint64(n) || !bytes.Equal(out.Payload, payload) {
					return errors.New("server-stream mismatch")
				}
				n++
			}
			if n != count {
				return fmt.Errorf("received %d messages, expected %d", n, count)
			}
			return stream.Err()
		}})
		tests = append(tests, test{fmt.Sprintf("client-stream-%d", count), func(ctx context.Context) error {
			stream := rpc("ClientStream").CallClientStream(ctx)
			for i := uint32(1); i <= count; i++ {
				if err := stream.Send(&pb.Packet{Seq: uint64(i), Payload: payload}); err != nil {
					return err
				}
			}
			out, err := stream.CloseAndReceive()
			if err == nil && (out.Msg.Count != count || out.Msg.Seq != uint64(count*(count+1)/2) || (count > 0 && !bytes.Equal(out.Msg.Payload, payload))) {
				return errors.New("client-stream mismatch")
			}
			return err
		}})
	}
	tests = append(tests, test{"duplex-before-half-close", func(ctx context.Context) error {
		stream := rpc("Duplex").CallBidiStream(ctx)
		defer stream.CloseResponse()
		for i := uint64(1); i <= 3; i++ {
			in := &pb.Packet{Seq: i, Payload: payload}
			if err := stream.Send(in); err != nil {
				return err
			}
			out, err := stream.Receive()
			if err != nil {
				return err
			}
			if !proto.Equal(in, out) {
				return errors.New("duplex mismatch")
			}
		}
		if err := stream.CloseRequest(); err != nil {
			return err
		}
		if _, err := stream.Receive(); !errors.Is(err, io.EOF) {
			return fmt.Errorf("duplex terminal: %v", err)
		}
		return nil
	}})
	tests = append(tests, test{"trailers-only-error", func(ctx context.Context) error {
		_, err := unary.CallUnary(ctx, connect.NewRequest(&pb.Packet{Code: uint32(connect.CodeNotFound)}))
		if connect.CodeOf(err) != connect.CodeNotFound {
			return fmt.Errorf("status: %v", err)
		}
		return nil
	}})
	tests = append(tests, test{"binary-metadata", func(ctx context.Context) error {
		in := connect.NewRequest(&pb.Packet{})
		values := []string{connect.EncodeBinaryHeader([]byte{0, ',', 255}), connect.EncodeBinaryHeader([]byte("second"))}
		for _, value := range values {
			in.Header().Add("x-test-bin", value)
		}
		out, err := unary.CallUnary(ctx, in)
		if err != nil {
			return err
		}
		for _, meta := range []http.Header{out.Header(), out.Trailer()} {
			got := meta.Values("x-echo-bin")
			if len(got) != 2 || got[0] != values[0] || got[1] != values[1] {
				return fmt.Errorf("binary metadata mismatch: %v", got)
			}
		}
		return nil
	}})
	tests = append(tests, test{"deadline", func(ctx context.Context) error {
		ctx, cancel := context.WithTimeout(ctx, 30*time.Millisecond)
		defer cancel()
		_, err := unary.CallUnary(ctx, connect.NewRequest(&pb.Packet{WaitCancel: true}))
		if connect.CodeOf(err) != connect.CodeDeadlineExceeded {
			return fmt.Errorf("deadline: %v", err)
		}
		return nil
	}})
	tests = append(tests, test{"cancellation", func(ctx context.Context) error {
		ctx, cancel := context.WithCancel(ctx)
		defer cancel()
		stream := rpc("Duplex").CallBidiStream(ctx)
		defer stream.CloseResponse()
		if err := stream.Send(&pb.Packet{Seq: 1}); err != nil {
			return err
		}
		if _, err := stream.Receive(); err != nil {
			return err
		}
		cancel()
		if _, err := stream.Receive(); connect.CodeOf(err) != connect.CodeCanceled {
			return fmt.Errorf("cancellation: %v", err)
		}
		return nil
	}})
	tests = append(tests, test{"concurrent", func(ctx context.Context) error {
		var wg sync.WaitGroup
		errs := make(chan error, 16)
		for i := uint64(0); i < 16; i++ {
			wg.Go(func() {
				in := &pb.Packet{Seq: i}
				out, err := unary.CallUnary(ctx, connect.NewRequest(in))
				if err == nil && !proto.Equal(in, out.Msg) {
					err = errors.New("concurrent mismatch")
				}
				errs <- err
			})
		}
		wg.Wait()
		close(errs)
		for err := range errs {
			if err != nil {
				return err
			}
		}
		return nil
	}})
	tests = append(tests, test{"compression", func(ctx context.Context) error {
		compressed := connect.NewClient[pb.Packet, pb.Packet](httpClient, base+path+"Unary",
			connect.WithGRPC(), connect.WithSendCompression("gzip"))
		in := &pb.Packet{Seq: 42, Payload: bytes.Repeat([]byte{0x71}, 8192)}
		out, err := compressed.CallUnary(ctx, connect.NewRequest(in))
		if native {
			if err == nil || ctx.Err() != nil {
				return fmt.Errorf("compressed request was not rejected promptly: %v", err)
			}
		} else if err != nil || !proto.Equal(in, out.Msg) {
			return fmt.Errorf("canonical peer compressed echo: %v", err)
		}
		plain := &pb.Packet{Seq: 43}
		echo, err := unary.CallUnary(ctx, connect.NewRequest(plain))
		if err == nil && !proto.Equal(plain, echo.Msg) {
			return errors.New("plain echo after compression rejection")
		}
		return err
	}})
	fmt.Printf("TAP version 13\n1..%d\n", len(tests))
	ok := true
	for i, test := range tests {
		ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
		err := test.run(ctx)
		cancel()
		if err != nil {
			ok = false
			fmt.Printf("not ok %d - %s\n# %v\n", i+1, test.name, err)
		} else {
			fmt.Printf("ok %d - %s\n", i+1, test.name)
		}
	}
	return ok
}

func main() {
	mode := flag.String("mode", "server", "server or client")
	listen := flag.String("listen", "127.0.0.1:0", "server UDP address")
	target := flag.String("target", "", "client host:port")
	cert := flag.String("cert", "", "server certificate")
	key := flag.String("key", "", "server key")
	ca := flag.String("ca", "", "client trust PEM")
	native := flag.Bool("native", false, "test an identity-only native peer")
	flag.Parse()
	if *mode == "server" {
		pair, err := tls.LoadX509KeyPair(*cert, *key)
		if err != nil {
			log.Fatal(err)
		}
		conn, err := net.ListenPacket("udp", *listen)
		if err != nil {
			log.Fatal(err)
		}
		server := http3.Server{Handler: handler(), TLSConfig: &tls.Config{Certificates: []tls.Certificate{pair}}}
		ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		defer cancel()
		go func() { <-ctx.Done(); server.Close() }()
		fmt.Printf("READY %s\n", conn.LocalAddr())
		if err := server.Serve(conn); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Fatal(err)
		}
		return
	}
	pem, err := os.ReadFile(*ca)
	if err != nil {
		log.Fatal(err)
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(pem) {
		log.Fatal("empty CA")
	}
	transport := &http3.Transport{TLSClientConfig: &tls.Config{RootCAs: roots, ServerName: "localhost"}}
	defer transport.Close()
	if !client(&http.Client{Transport: transport}, "https://"+*target, *native) {
		os.Exit(1)
	}
}
