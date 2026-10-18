// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

using System.Net;
using System.Net.Http;
using System.Net.Security;
using System.Net.Quic;
using System.Security.Cryptography.X509Certificates;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Net.Client;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Zgrpc.Interop;

sealed class MatrixService : Matrix.MatrixBase
{
    static async Task Control(Packet input, ServerCallContext context)
    {
        if (input.Code != 0)
            throw new RpcException(new Status(input.Code <= 16 ?
                (StatusCode)input.Code : StatusCode.InvalidArgument, "requested error"));
        if (input.WaitCancel)
            await Task.Delay(Timeout.Infinite, context.CancellationToken);
    }

    static Task Headers(ServerCallContext context)
    {
        var headers = new Metadata { { "x-peer", "dotnet" } };
        context.ResponseTrailers.Add("x-trailer", "done");
        // grpc-dotnet flattens repeated binary request values before decoding.
        // Decode this fixture's echo field independently from the HTTP values.
        foreach (var combined in context.GetHttpContext().Request.Headers["x-test-bin"])
        {
            foreach (var encoded in combined!.Split(','))
            {
                var text = encoded.Trim();
                var value = Convert.FromBase64String(text.PadRight((text.Length + 3) & ~3, '='));
                headers.Add("x-echo-bin", value);
                context.ResponseTrailers.Add("x-echo-bin", value);
            }
        }
        return context.WriteResponseHeadersAsync(headers);
    }

    public override async Task<Packet> Unary(Packet input, ServerCallContext context)
    {
        if (input.Code != 0) await Control(input, context);
        await Headers(context);
        await Control(input, context);
        return input.Clone();
    }

    public override async Task ServerStream(Packet input,
        IServerStreamWriter<Packet> output, ServerCallContext context)
    {
        await Control(input, context);
        if (input.Count > 65536)
            throw new RpcException(new Status(StatusCode.ResourceExhausted, "fixture count limit"));
        await Headers(context);
        for (uint i = 0; i < input.Count; ++i)
        {
            var packet = input.Clone();
            packet.Seq += i;
            await output.WriteAsync(packet);
        }
    }

    public override async Task<Packet> ClientStream(IAsyncStreamReader<Packet> input,
        ServerCallContext context)
    {
        await Headers(context);
        var result = new Packet();
        while (await input.MoveNext(context.CancellationToken))
        {
            await Control(input.Current, context);
            ++result.Count;
            result.Seq += input.Current.Seq;
            result.Payload = input.Current.Payload;
        }
        return result;
    }

    public override async Task Duplex(IAsyncStreamReader<Packet> input,
        IServerStreamWriter<Packet> output, ServerCallContext context)
    {
        await Headers(context);
        while (await input.MoveNext(context.CancellationToken))
        {
            await Control(input.Current, context);
            await output.WriteAsync(input.Current);
        }
    }
}

sealed class VersionHandler : DelegatingHandler
{
    public VersionHandler(HttpMessageHandler inner) { InnerHandler = inner; }
    protected override async Task<HttpResponseMessage> SendAsync(
        HttpRequestMessage request, CancellationToken cancel)
    {
        var response = await base.SendAsync(request, cancel);
        if (response.Version != HttpVersion.Version30)
            throw new InvalidDataException("independent peer did not negotiate HTTP/3");
        return response;
    }
}

static class Program
{
    static DateTime Deadline => DateTime.UtcNow.AddSeconds(15);
    static void Check(bool value, string message)
    {
        if (!value) throw new InvalidDataException(message);
    }

    static ByteString Payload(byte value) => ByteString.CopyFrom(
        Enumerable.Repeat(value, 256 << 10).ToArray());

    static async Task Status(Func<Task> operation, StatusCode expected)
    {
        try { await operation(); }
        catch (RpcException error) when (error.StatusCode == expected) { return; }
        throw new InvalidDataException("expected status " + expected);
    }

    static async Task Unary(Matrix.MatrixClient client, bool empty)
    {
        var input = empty ? new Packet() : new Packet { Seq = 42, Payload = Payload(0x71) };
        using var call = client.UnaryAsync(input, deadline: Deadline);
        Check(input.Equals(await call.ResponseAsync), "unary protobuf mismatch");
    }

    static async Task ServerStream(Matrix.MatrixClient client, uint count)
    {
        var input = new Packet { Seq = 10, Count = count, Payload = Payload(0x6d) };
        using var call = client.ServerStream(input, deadline: Deadline);
        uint n = 0;
        while (await call.ResponseStream.MoveNext(CancellationToken.None))
        {
            var expected = input.Clone(); expected.Seq += n++;
            Check(call.ResponseStream.Current.Equals(expected), "server stream protobuf mismatch");
        }
        Check(n == count, "server stream count mismatch");
    }

    static async Task ClientStream(Matrix.MatrixClient client, uint count)
    {
        using var call = client.ClientStream(deadline: Deadline);
        var payload = Payload(0x7a);
        for (uint i = 1; i <= count; ++i)
            await call.RequestStream.WriteAsync(new Packet { Seq = i, Payload = payload });
        await call.RequestStream.CompleteAsync();
        var output = await call.ResponseAsync;
        Check(output.Seq == count * (count + 1) / 2 && output.Count == count &&
            output.Payload.Equals(count == 0 ? ByteString.Empty : payload), "client stream aggregate mismatch");
    }

    static async Task Duplex(Matrix.MatrixClient client)
    {
        using var call = client.Duplex(deadline: Deadline);
        for (ulong i = 1; i <= 3; ++i)
        {
            var packet = new Packet { Seq = i, Payload = Payload((byte)i) };
            await call.RequestStream.WriteAsync(packet);
            Check(await call.ResponseStream.MoveNext(CancellationToken.None) &&
                call.ResponseStream.Current.Equals(packet), "duplex response before request end");
        }
        await call.RequestStream.CompleteAsync();
        Check(!await call.ResponseStream.MoveNext(CancellationToken.None), "duplex terminal mismatch");
    }

    static async Task Binary(Matrix.MatrixClient client)
    {
        byte[][] values = { new byte[] { 0, (byte)',', 255 }, "second"u8.ToArray() };
        var headers = new Metadata();
        foreach (var value in values) headers.Add("x-test-bin", value);
        using var call = client.UnaryAsync(new Packet(), headers, Deadline);
        await call.ResponseAsync;
        foreach (var fields in new[] { await call.ResponseHeadersAsync, call.GetTrailers() })
        {
            var entries = fields.Where(field => field.Key == "x-echo-bin").ToArray();
            Check(entries.Length == values.Length && entries.Select((entry, i) =>
                entry.ValueBytes.SequenceEqual(values[i])).All(value => value), "binary metadata mismatch");
        }
    }

    static async Task Cancel(Matrix.MatrixClient client)
    {
        using var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        using var call = client.Duplex(cancellationToken: cancel.Token);
        await call.ResponseHeadersAsync;
        await call.RequestStream.WriteAsync(new Packet { WaitCancel = true });
        cancel.Cancel();
        await Status(async () => { await call.ResponseStream.MoveNext(CancellationToken.None); }, StatusCode.Cancelled);
    }

    static async Task Unknown(GrpcChannel channel)
    {
        var codec = Marshallers.Create<Packet>(packet => packet.ToByteArray(), Packet.Parser.ParseFrom);
        var method = new Method<Packet, Packet>(MethodType.Unary, "zgrpc.interop.Matrix", "Unknown", codec, codec);
        await Status(async () => {
            using var call = channel.CreateCallInvoker().AsyncUnaryCall(method, null,
                new CallOptions(deadline: Deadline), new Packet());
            await call.ResponseAsync;
        }, StatusCode.Unimplemented);
    }

    static async Task<int> Client(string target, string ca)
    {
        using var root = X509CertificateLoader.LoadCertificateFromFile(ca);
        // HttpClientHandler leaves QUIC connection establishment to HttpClient;
        // grpc-dotnet's socket subchannel connector probes TCP endpoints.
        var handler = new HttpClientHandler();
        handler.ServerCertificateCustomValidationCallback = (_, cert, _, errors) => {
            if (cert == null || (errors & SslPolicyErrors.RemoteCertificateNameMismatch) != 0) return false;
            using var chain = new X509Chain();
            chain.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
            chain.ChainPolicy.CustomTrustStore.Add(root);
            chain.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
            return chain.Build(new X509Certificate2(cert));
        };
        using var channel = GrpcChannel.ForAddress(target, new GrpcChannelOptions {
            HttpHandler = new VersionHandler(handler), HttpVersion = HttpVersion.Version30,
            HttpVersionPolicy = HttpVersionPolicy.RequestVersionExact,
            MaxReceiveMessageSize = 16 << 20, MaxSendMessageSize = 16 << 20
        });
        var client = new Matrix.MatrixClient(channel);
        (string Name, Func<Task> Run)[] cases = {
            ("unknown-method", () => Unknown(channel)),
            ("unary", () => Unary(client, false)),
            ("empty-message", () => Unary(client, true)),
            ("server-stream", () => ServerStream(client, 3)),
            ("empty-server-stream", () => ServerStream(client, 0)),
            ("client-stream", () => ClientStream(client, 3)),
            ("empty-client-stream", () => ClientStream(client, 0)),
            ("duplex", () => Duplex(client)),
            ("trailers-only-error", () => Status(async () => {
                await client.UnaryAsync(new Packet { Code = 5 }, deadline: Deadline);
            }, StatusCode.NotFound)),
            ("binary-metadata", () => Binary(client)),
            ("cancellation", () => Cancel(client)),
            ("deadline", () => Status(async () => {
                await client.UnaryAsync(new Packet { WaitCancel = true },
                    deadline: DateTime.UtcNow.AddMilliseconds(30));
            }, StatusCode.DeadlineExceeded)),
            ("concurrent", () => Task.WhenAll(Enumerable.Range(0, 16).Select(async i => {
                var packet = new Packet { Seq = (ulong)i };
                Check(packet.Equals(await client.UnaryAsync(packet, deadline: Deadline)), "concurrent response mismatch");
            })))
        };
        Console.WriteLine($"TAP version 13\n1..{cases.Length}");
        int failed = 0, number = 0;
        foreach (var test in cases)
        {
            try { await test.Run().WaitAsync(TimeSpan.FromSeconds(30));
                Console.WriteLine($"ok {++number} - {test.Name}"); }
            catch (Exception error) { ++failed;
                Console.WriteLine($"not ok {++number} - {test.Name}");
                foreach (var line in error.ToString().Split('\n')) Console.WriteLine("# " + line); }
        }
        return failed == 0 ? 0 : 1;
    }

    static async Task<WebApplication> Server(string cert, string key, int port)
    {
        var builder = WebApplication.CreateBuilder(Array.Empty<string>());
        builder.Logging.ClearProviders();
        builder.Logging.AddConsole(options => options.LogToStandardErrorThreshold = LogLevel.Trace);
        builder.Logging.SetMinimumLevel(LogLevel.Warning);
        builder.Services.AddGrpc(options => {
            options.MaxReceiveMessageSize = 16 << 20; options.MaxSendMessageSize = 16 << 20;
        });
        builder.WebHost.ConfigureKestrel(options => options.Listen(IPAddress.Loopback, port, listen => {
            listen.Protocols = HttpProtocols.Http3;
            listen.UseHttps(X509Certificate2.CreateFromPemFile(cert, key));
        }));
        var app = builder.Build();
        app.MapGrpcService<MatrixService>();
        await app.StartAsync();
        return app;
    }

    public static async Task<int> Main(string[] arguments)
    {
        try {
            Check(QuicConnection.IsSupported, "MsQuic is unavailable; HTTP/3 qualification cannot run");
            var args = arguments.Select(arg => arg.TrimStart('-').Split('=', 2))
                .ToDictionary(arg => arg[0], arg => arg.Length == 2 ? arg[1] : "");
            if (args["mode"] == "client") return await Client(args["target"], args["ca"]);
            await using var app = await Server(args["cert"], args["key"],
                args.TryGetValue("port", out var port) ? int.Parse(port) : 0);
            var address = app.Services.GetRequiredService<IServer>()
                .Features.Get<IServerAddressesFeature>()!.Addresses.Single();
            if (args["mode"] == "self-test") {
                int result = await Client(address.Replace("127.0.0.1", "localhost"), args["cert"]);
                await app.StopAsync(); return result;
            }
            Console.WriteLine("READY " + new Uri(address).Authority);
            await app.WaitForShutdownAsync(); return 0;
        } catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
