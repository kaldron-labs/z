GO ?= go
EXEEXT ?=

.PHONY: all clean
all: zpbpeer$(EXEEXT)

zpbpeer$(EXEEXT): main.go go.mod go.sum
	$(GO) build -o $@ .

clean:
	rm -f zpbpeer zpbpeer.exe
