GO ?= go

.PHONY: all clean
all: client server

client: client.go go.mod go.sum
	$(GO) build -o $@ client.go

server: server.go go.mod go.sum
	$(GO) build -o $@ server.go

clean:
	rm -f client server
