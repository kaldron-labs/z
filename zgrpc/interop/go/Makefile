GO ?= go
PROTOC ?= protoc
EXEEXT ?=

.PHONY: all test race generate clean
all: zgrpcpeer$(EXEEXT) zgrpch3peer$(EXEEXT)

zgrpch3peer$(EXEEXT): h3/main.go go.mod go.sum pb/zgrpc.pb.go
	$(GO) build -o $@ ./h3

zgrpcpeer$(EXEEXT): main.go peer.go peer_test.go go.mod go.sum pb/zgrpc.pb.go pb/zgrpc_grpc.pb.go
	$(GO) build -o $@ .

test:
	$(GO) test -count=1 ./...

race:
	$(GO) test -race -count=1 ./...

generate:
	$(PROTOC) -I.. --go_out=. --go_opt=module=zgrpc-interop \
	  --go-grpc_out=. --go-grpc_opt=module=zgrpc-interop ../zgrpc.proto

clean:
	rm -f zgrpcpeer zgrpcpeer.exe zgrpch3peer zgrpch3peer.exe
