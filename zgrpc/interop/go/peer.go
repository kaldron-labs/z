// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"sync"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/encoding/gzip"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	"zgrpc-interop/pb"
)

// Match Zgrpc's default payload budget, independently of transport windows.
const messageLimit = 16 << 20

// matrix is an independent canonical gRPC implementation of zgrpc.proto.
// No Z framing or codec implementation is used by this peer.
type matrix struct{ pb.UnimplementedMatrixServer }

func responseFields(ctx context.Context) (metadata.MD, metadata.MD) {
	in, _ := metadata.FromIncomingContext(ctx)
	header := metadata.Pairs("x-peer", "go")
	trailer := metadata.Pairs("x-trailer", "done")
	for _, v := range in.Get("x-test-bin") {
		header.Append("x-echo-bin", v)
		trailer.Append("x-echo-bin", v)
	}
	return header, trailer
}

func control(ctx context.Context, in *pb.Packet) error {
	if in.Code != 0 {
		if in.Code > uint32(codes.Unauthenticated) {
			return status.Error(codes.InvalidArgument, "invalid requested status")
		}
		return status.Error(codes.Code(in.Code), "requested error")
	}
	if in.WaitCancel {
		<-ctx.Done()
		return status.FromContextError(ctx.Err()).Err()
	}
	return nil
}

func (*matrix) Unary(ctx context.Context, in *pb.Packet) (*pb.Packet, error) {
	// An error before any application headers or DATA exercises trailers-only.
	if in.Code != 0 {
		return nil, control(ctx, in)
	}
	header, trailer := responseFields(ctx)
	if err := grpc.SendHeader(ctx, header); err != nil {
		return nil, err
	}
	grpc.SetTrailer(ctx, trailer)
	if err := control(ctx, in); err != nil {
		return nil, err
	}
	return proto.Clone(in).(*pb.Packet), nil
}

func (*matrix) ServerStream(in *pb.Packet, stream grpc.ServerStreamingServer[pb.Packet]) error {
	if err := control(stream.Context(), in); err != nil {
		return err
	}
	if in.Count > 65536 {
		return status.Error(codes.ResourceExhausted, "fixture count limit")
	}
	header, trailer := responseFields(stream.Context())
	if err := stream.SendHeader(header); err != nil {
		return err
	}
	stream.SetTrailer(trailer)
	for i := uint32(0); i < in.Count; i++ {
		out := proto.Clone(in).(*pb.Packet)
		out.Seq += uint64(i)
		if err := stream.Send(out); err != nil {
			return err
		}
	}
	return nil
}

func (*matrix) ClientStream(stream grpc.ClientStreamingServer[pb.Packet, pb.Packet]) error {
	header, trailer := responseFields(stream.Context())
	if err := stream.SendHeader(header); err != nil {
		return err
	}
	stream.SetTrailer(trailer)
	out := &pb.Packet{}
	for {
		in, err := stream.Recv()
		if err == io.EOF {
			return stream.SendAndClose(out)
		}
		if err != nil {
			return err
		}
		if err := control(stream.Context(), in); err != nil {
			return err
		}
		out.Count++
		out.Seq += in.Seq
		out.Payload = in.Payload
	}
}

func (*matrix) Duplex(stream grpc.BidiStreamingServer[pb.Packet, pb.Packet]) error {
	header, trailer := responseFields(stream.Context())
	if err := stream.SendHeader(header); err != nil {
		return err
	}
	stream.SetTrailer(trailer)
	for {
		in, err := stream.Recv()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		if err := control(stream.Context(), in); err != nil {
			return err
		}
		if err := stream.Send(in); err != nil {
			return err
		}
	}
}

type peerCase struct {
	name string
	run  func(context.Context, pb.MatrixClient) error
}

func echo(ctx context.Context, client pb.MatrixClient, in *pb.Packet) error {
	out, err := client.Unary(ctx, in)
	if err != nil {
		return err
	}
	if !proto.Equal(in, out) {
		return fmt.Errorf("unary protobuf mismatch")
	}
	return nil
}

func serverStream(ctx context.Context, client pb.MatrixClient, count uint32) error {
	// Each message exceeds the ordinary initial HTTP/2 stream receive window.
	payload := bytes.Repeat([]byte{0x6d}, 256<<10)
	stream, err := client.ServerStream(ctx, &pb.Packet{Seq: 10, Count: count, Payload: payload})
	if err != nil {
		return err
	}
	for i := uint32(0); i < count; i++ {
		out, err := stream.Recv()
		if err != nil {
			return err
		}
		if out.Seq != 10+uint64(i) || !bytes.Equal(out.Payload, payload) {
			return fmt.Errorf("server-stream message %d mismatch", i)
		}
	}
	if _, err := stream.Recv(); err != io.EOF {
		return fmt.Errorf("server-stream terminal: %v", err)
	}
	return nil
}

func clientStream(ctx context.Context, client pb.MatrixClient, count int) error {
	stream, err := client.ClientStream(ctx)
	if err != nil {
		return err
	}
	payload := bytes.Repeat([]byte{0x7a}, 256<<10)
	for i := 1; i <= count; i++ {
		if err := stream.Send(&pb.Packet{Seq: uint64(i), Payload: payload}); err != nil {
			return err
		}
	}
	out, err := stream.CloseAndRecv()
	if err != nil {
		return err
	}
	if out.Count != uint32(count) || out.Seq != uint64(count*(count+1)/2) {
		return fmt.Errorf("client-stream aggregate mismatch")
	}
	if count > 0 && !bytes.Equal(out.Payload, payload) {
		return fmt.Errorf("client-stream payload mismatch")
	}
	return nil
}

func duplex(ctx context.Context, client pb.MatrixClient) error {
	stream, err := client.Duplex(ctx)
	if err != nil {
		return err
	}
	for i := uint64(1); i <= 3; i++ {
		in := &pb.Packet{Seq: i, Payload: bytes.Repeat([]byte{byte(i)}, 256<<10)}
		if err := stream.Send(in); err != nil {
			return err
		}
		// Demand a response before request half-close, proving duplex behavior.
		out, err := stream.Recv()
		if err != nil {
			return err
		}
		if !proto.Equal(in, out) {
			return fmt.Errorf("duplex message %d mismatch", i)
		}
	}
	if err := stream.CloseSend(); err != nil {
		return err
	}
	if _, err := stream.Recv(); err != io.EOF {
		return fmt.Errorf("duplex terminal: %v", err)
	}
	return nil
}

func binaryMetadata(ctx context.Context, client pb.MatrixClient) error {
	values := []string{string([]byte{0, ',', 255}), "second"}
	ctx = metadata.NewOutgoingContext(ctx, metadata.MD{"x-test-bin": values})
	var header, trailer metadata.MD
	_, err := client.Unary(ctx, &pb.Packet{}, grpc.Header(&header), grpc.Trailer(&trailer))
	if err != nil {
		return err
	}
	for _, fields := range []metadata.MD{header, trailer} {
		got := fields.Get("x-echo-bin")
		if len(got) != len(values) || got[0] != values[0] || got[1] != values[1] {
			return fmt.Errorf("binary metadata mismatch")
		}
	}
	return nil
}

func cancellation(ctx context.Context, client pb.MatrixClient) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	stream, err := client.Duplex(ctx)
	if err != nil {
		return err
	}
	if _, err := stream.Header(); err != nil {
		return err
	}
	if err := stream.Send(&pb.Packet{WaitCancel: true}); err != nil {
		return err
	}
	cancel()
	if _, err := stream.Recv(); status.Code(err) != codes.Canceled {
		return fmt.Errorf("cancellation: %v", err)
	}
	return nil
}

func deadline(ctx context.Context, client pb.MatrixClient) error {
	// The handler waits for context termination; no test sleep/polling races.
	ctx, cancel := context.WithTimeout(ctx, 30*time.Millisecond)
	defer cancel()
	_, err := client.Unary(ctx, &pb.Packet{WaitCancel: true})
	if status.Code(err) != codes.DeadlineExceeded {
		return fmt.Errorf("deadline: %v", err)
	}
	return nil
}

func concurrent(ctx context.Context, client pb.MatrixClient) error {
	const calls = 16
	var wg sync.WaitGroup
	errors := make(chan error, calls)
	for i := 0; i < calls; i++ {
		wg.Go(func() { errors <- echo(ctx, client, &pb.Packet{Seq: uint64(i)}) })
	}
	wg.Wait()
	close(errors)
	for err := range errors {
		if err != nil {
			return err
		}
	}
	return nil
}

func compression(ctx context.Context, client pb.MatrixClient, reject bool) error {
	in := &pb.Packet{Seq: 42, Payload: bytes.Repeat([]byte{0x71}, 8192)}
	out, err := client.Unary(ctx, in, grpc.UseCompressor(gzip.Name))
	if reject {
		if err == nil || ctx.Err() != nil {
			return fmt.Errorf("compressed request was not rejected promptly: %v", err)
		}
	} else if err != nil || !proto.Equal(in, out) {
		return fmt.Errorf("canonical peer compressed echo: %v", err)
	}
	// An ordinary call must remain usable after rejection.
	return echo(ctx, client, &pb.Packet{Seq: 43})
}

func cases(conn grpc.ClientConnInterface, native bool) []peerCase {
	return []peerCase{
		{"unknown-method", func(ctx context.Context, c pb.MatrixClient) error {
			err := conn.Invoke(ctx, "/zgrpc.interop.Matrix/Unknown", &pb.Packet{}, &pb.Packet{})
			if status.Code(err) != codes.Unimplemented {
				return fmt.Errorf("unknown method: %v", err)
			}
			return nil
		}},
		{"unary", func(ctx context.Context, c pb.MatrixClient) error {
			return echo(ctx, c, &pb.Packet{Seq: 42, Payload: []byte{0, 1, 255}})
		}},
		{"empty-message", func(ctx context.Context, c pb.MatrixClient) error {
			return echo(ctx, c, &pb.Packet{})
		}},
		{"server-stream", func(ctx context.Context, c pb.MatrixClient) error {
			return serverStream(ctx, c, 3)
		}},
		{"empty-server-stream", func(ctx context.Context, c pb.MatrixClient) error {
			return serverStream(ctx, c, 0)
		}},
		{"client-stream", func(ctx context.Context, c pb.MatrixClient) error {
			return clientStream(ctx, c, 3)
		}},
		{"empty-client-stream", func(ctx context.Context, c pb.MatrixClient) error {
			return clientStream(ctx, c, 0)
		}},
		{"duplex", duplex},
		{"trailers-only-error", func(ctx context.Context, c pb.MatrixClient) error {
			_, err := c.Unary(ctx, &pb.Packet{Code: uint32(codes.NotFound)})
			if status.Code(err) != codes.NotFound {
				return fmt.Errorf("trailers-only status: %v", err)
			}
			return nil
		}},
		{"binary-metadata", binaryMetadata},
		{"cancellation", cancellation},
		{"deadline", deadline},
		{"concurrent", concurrent},
		{"compression", func(ctx context.Context, c pb.MatrixClient) error {
			return compression(ctx, c, native)
		}},
	}
}
