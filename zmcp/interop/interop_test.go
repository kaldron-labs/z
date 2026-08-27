package interop

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http/httptest"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/mark3labs/mcp-go/client"
	"github.com/mark3labs/mcp-go/client/transport"
	"github.com/mark3labs/mcp-go/mcp"
	"github.com/mark3labs/mcp-go/server"
)

const (
	modern = mcp.ProtocolVersion20260728
	legacy = mcp.ProtocolVersion20251125
)

type addArgs struct {
	LHS int64 `json:"lhs"`
	RHS int64 `json:"rhs"`
}

type addData struct {
	Value int64 `json:"value"`
}

type addResult struct {
	Code int     `json:"code"`
	Data addData `json:"data"`
}

func addHandler(
	context.Context, mcp.CallToolRequest, addArgs,
) (*mcp.CallToolResult, error) {
	return &mcp.CallToolResult{
		Content: []mcp.Content{},
		StructuredContent: addResult{
			Code: 200,
			Data: addData{Value: 42},
		},
	}, nil
}

func goServer() *server.MCPServer {
	s := server.NewMCPServer(
		"zmcp-interop", "1",
		server.WithToolCapabilities(false))
	tool := mcp.NewTool(
		"add",
		mcp.WithInputSchema[addArgs](),
		mcp.WithOutputSchema[addResult]())
	s.AddTool(tool, mcp.NewTypedToolHandler(addHandler))
	return s
}

func program(t *testing.T, name string) string {
	t.Helper()
	path, err := filepath.Abs(filepath.Join("..", "example", name))
	if err != nil {
		t.Fatal(err)
	}
	if _, err = os.Stat(path); err != nil {
		t.Fatalf("%s is not built: %v", path, err)
	}
	return path
}

func clientFor(
	t *testing.T, trans transport.Interface, version string,
) *client.Client {
	t.Helper()
	c := client.NewClient(trans, client.WithProtocolVersion(version))
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	t.Cleanup(cancel)
	if err := c.Start(ctx); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = c.Close() })
	initialized, err := c.Initialize(ctx, mcp.InitializeRequest{
		Params: mcp.InitializeParams{
			ProtocolVersion: version,
			ClientInfo: mcp.Implementation{
				Name: "zmcp-interop", Version: "1",
			},
		},
	})
	if err != nil {
		t.Fatal(err)
	}
	if initialized.ProtocolVersion != version {
		t.Fatalf("negotiated %q, want %q", initialized.ProtocolVersion, version)
	}
	if initialized.ServerInfo.Name != "zmcp" {
		t.Fatalf("server name %q, want zmcp", initialized.ServerInfo.Name)
	}
	return c
}

func exerciseGoClient(t *testing.T, c *client.Client, version string) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	result, err := c.CallTool(ctx, mcp.CallToolRequest{
		Params: mcp.CallToolParams{
			Name:      "add",
			Arguments: map[string]any{"lhs": 19, "rhs": 23},
		},
	})
	if err != nil {
		t.Fatal(err)
	}
	if result.IsError || len(result.Content) != 0 {
		t.Fatalf("unexpected tool result: %#v", result)
	}
	if version == modern && result.ResultType != mcp.ResultTypeComplete {
		t.Fatalf("result type %q, want complete", result.ResultType)
	}
	if version == legacy && result.ResultType != "" {
		t.Fatalf("legacy result type %q, want omitted", result.ResultType)
	}
	var structured struct {
		Code int     `json:"code"`
		Data addData `json:"data"`
	}
	if err := json.Unmarshal(result.RawStructuredContent, &structured); err != nil {
		t.Fatal(err)
	}
	if structured.Code != 200 || structured.Data.Value != 42 {
		t.Fatalf("unexpected structured result: %#v", structured)
	}
}

func TestGoClientToZServer(t *testing.T) {
	for _, version := range []string{modern, legacy} {
		t.Run("stdio/"+version, func(t *testing.T) {
			trans := transport.NewStdio(program(t, "zmcpd"), nil, "--stdio")
			c := clientFor(t, trans, version)
			if stderr := trans.Stderr(); stderr != nil {
				go func() { _, _ = io.Copy(io.Discard, stderr) }()
			}
			exerciseGoClient(t, c, version)
		})

		t.Run("http/"+version, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
			defer cancel()
			listener, err := net.Listen("tcp", "127.0.0.1:0")
			if err != nil {
				t.Fatal(err)
			}
			port := listener.Addr().(*net.TCPAddr).Port
			_ = listener.Close()

			cmd := exec.CommandContext(ctx, program(t, "zmcpd"),
				fmt.Sprintf("--port=%d", port))
			stderr, err := cmd.StderrPipe()
			if err != nil {
				t.Fatal(err)
			}
			if err = cmd.Start(); err != nil {
				t.Fatal(err)
			}
			t.Cleanup(func() {
				if cmd.ProcessState == nil {
					_ = cmd.Process.Kill()
					_ = cmd.Wait()
				}
			})
			ready := make(chan error, 1)
			go func() {
				scanner := bufio.NewScanner(stderr)
				for scanner.Scan() {
					if strings.Contains(scanner.Text(), "listening port=") {
						ready <- nil
						_, _ = io.Copy(io.Discard, stderr)
						return
					}
				}
				ready <- scanner.Err()
			}()
			select {
			case err = <-ready:
				if err != nil {
					t.Fatal(err)
				}
			case <-ctx.Done():
				t.Fatal(ctx.Err())
			}

			trans, err := transport.NewStreamableHTTP(
				fmt.Sprintf("http://127.0.0.1:%d/mcp", port))
			if err != nil {
				t.Fatal(err)
			}
			c := clientFor(t, trans, version)
			exerciseGoClient(t, c, version)
			_ = c.Close()
			if err = cmd.Process.Signal(os.Interrupt); err != nil {
				t.Fatal(err)
			}
			if err = cmd.Wait(); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func runZClient(t *testing.T, args ...string) (*exec.Cmd, *bytes.Buffer) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	t.Cleanup(cancel)
	cmd := exec.CommandContext(ctx, program(t, "zmcp"), args...)
	stderr := new(bytes.Buffer)
	cmd.Stderr = stderr
	cmd.Env = os.Environ()
	t.Cleanup(func() {
		if cmd.Process != nil && cmd.ProcessState == nil {
			_ = cmd.Process.Kill()
			_ = cmd.Wait()
		}
	})
	return cmd, stderr
}

func TestZClientToGoServer(t *testing.T) {
	for _, version := range []string{modern, legacy} {
		t.Run("stdio/"+version, func(t *testing.T) {
			clientIn, serverOut, err := os.Pipe()
			if err != nil {
				t.Fatal(err)
			}
			serverIn, clientOut, err := os.Pipe()
			if err != nil {
				t.Fatal(err)
			}
			cmd, stderr := runZClient(t,
				"--stdio", "--lhs=19", "--rhs=23")
			cmd.Stdin = clientIn
			cmd.Stdout = clientOut
			if err = cmd.Start(); err != nil {
				t.Fatal(err)
			}
			_ = clientIn.Close()
			_ = clientOut.Close()

			stdio := server.NewStdioServer(goServer())
			if version == legacy {
				stdio.SetContextFunc(func(ctx context.Context) context.Context {
					return server.WithSupportedProtocolVersions(
						ctx, []string{legacy})
				})
			}
			ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
			defer cancel()
			done := make(chan error, 1)
			go func() { done <- stdio.Listen(ctx, serverIn, serverOut) }()
			if err = cmd.Wait(); err != nil {
				t.Fatalf("%v: %s", err, stderr.String())
			}
			_ = serverIn.Close()
			_ = serverOut.Close()
			select {
			case err = <-done:
				if err != nil {
					t.Fatal(err)
				}
			case <-ctx.Done():
				t.Fatal(ctx.Err())
			}
		})

		t.Run("http/"+version, func(t *testing.T) {
			opts := []server.StreamableHTTPOption{}
			if version == legacy {
				opts = append(opts,
					server.WithStreamableHTTPProtocolVersions(legacy))
			}
			handler := server.NewStreamableHTTPServer(goServer(), opts...)
			httpServer := httptest.NewServer(handler)
			defer httpServer.Close()
			endpoint, err := url.Parse(httpServer.URL)
			if err != nil {
				t.Fatal(err)
			}
			host, port, err := net.SplitHostPort(endpoint.Host)
			if err != nil {
				t.Fatal(err)
			}
			cmd, stderr := runZClient(t,
				"--host="+host, "--port="+port, "--lhs=19", "--rhs=23")
			if err = cmd.Run(); err != nil {
				t.Fatalf("%v: %s", err, stderr.String())
			}
		})
	}
}
