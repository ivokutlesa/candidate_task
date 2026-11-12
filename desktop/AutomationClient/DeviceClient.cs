using System.Net.Sockets;
using System.Text;

namespace AutomationClient;

public sealed class DeviceClient
{
    private readonly string _host;
    private readonly int _port;
    private readonly SemaphoreSlim _writeLock = new(1, 1);
    private StreamWriter? _writer;
    private volatile bool _heartbeatEnabled = true;

    public event Action<DeviceStatus>? StatusReceived;

    // When false, PING is not sent (used to demonstrate the device watchdog).
    public bool HeartbeatEnabled
    {
        get => _heartbeatEnabled;
        set => _heartbeatEnabled = value;
    }

    public DeviceClient(string host = "127.0.0.1", int port = 5042)
    {
        _host = host;
        _port = port;
    }

    public async Task RunAsync(CancellationToken token)
    {
        // TODO (candidate): A simulated cable pull, server restart, or an
        // initial unavailable server currently ends this method. Add bounded
        // retry/backoff and reconnect while preserving clean cancellation.
        try
        {
            using var client = new TcpClient();
            await client.ConnectAsync(_host, _port, token);
            Console.WriteLine("Connected to virtual controller.");
            using NetworkStream stream = client.GetStream();
            using var reader = new StreamReader(stream, Encoding.ASCII, false, 1024, leaveOpen: true);
            using var writer = new StreamWriter(stream, Encoding.ASCII, 1024, leaveOpen: true)
            {
                AutoFlush = true,
                NewLine = "\n"
            };
            using var session = CancellationTokenSource.CreateLinkedTokenSource(token);
            _writer = writer;
            Task heartbeat = HeartbeatLoopAsync(session.Token);
            try
            {
                while (!token.IsCancellationRequested)
                {
                    string? line = await reader.ReadLineAsync(token);
                    if (line is null) break;
                    if (Protocol.TryParseStatus(line, out DeviceStatus? status) && status is not null)
                    {
                        StatusReceived?.Invoke(status);
                    }
                    else if (!line.StartsWith("PONG") && !line.StartsWith("ACK,"))
                    {
                        Console.WriteLine("Device message (or invalid telemetry): " + line);
                    }
                }
            }
            finally
            {
                session.Cancel();
                _writer = null;
                try { await heartbeat; }
                catch (OperationCanceledException) { }
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { }
        catch (IOException ex) { Console.WriteLine("Disconnected: " + ex.Message); }
        catch (SocketException ex) { Console.WriteLine("Connection failed: " + ex.Message); }
        Console.WriteLine("Device session ended.");
    }

    private async Task HeartbeatLoopAsync(CancellationToken token)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(400));
        do
        {
            if (_heartbeatEnabled && !await SendAsync("PING", token)) return;
        } while (await timer.WaitForNextTickAsync(token));
    }

    public async Task<bool> SendAsync(string command, CancellationToken token)
    {
        await _writeLock.WaitAsync(token);
        try
        {
            StreamWriter? writer = _writer;
            if (writer is null) return false;
            await writer.WriteLineAsync(command.AsMemory(), token);
            return true;
        }
        catch (IOException) { return false; }
        catch (ObjectDisposedException) { return false; }
        finally { _writeLock.Release(); }
    }
}
