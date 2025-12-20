all: client server

client: client.go
	go build -o client client.go

server: server.go
	go build -o server server.go

cert:
	openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout server.key -out server.crt -config cert.conf

init:
	go mod init github.com/djnz00/z-mx
	go get github.com/golang-jwt/jwt/v5
	go get github.com/gorilla/mux
	go mod tidy
