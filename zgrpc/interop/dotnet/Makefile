# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

DOTNET ?= dotnet

.PHONY: all test clean
all:
	mkdir -p obj
	python3 ../../../scripts/toml-to-json packages.lock.toml > obj/packages.lock.json
	DOTNET_CLI_TELEMETRY_OPTOUT=1 $(DOTNET) restore --locked-mode Peer.csproj
	DOTNET_CLI_TELEMETRY_OPTOUT=1 $(DOTNET) build --no-restore -c Release Peer.csproj

test: all
	python3 ../run-dotnet.py --peer=./bin/Release/net10.0/Peer --self-test

clean:
	$(DOTNET) clean -c Release Peer.csproj
