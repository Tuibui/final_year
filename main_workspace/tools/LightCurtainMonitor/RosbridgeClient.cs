using System.Net.WebSockets;
using System.Text;
using System.Text.Json;

namespace LightCurtainMonitor;

public sealed class RosbridgeClient(MonitorOptions options)
{
    private readonly object _gate = new();
    private AxisFrame? _x, _y;
    private string? _xError, _yError;
    private bool _connected;
    private string _detail = "Not connected";

    public ShelfSnapshot Snapshot()
    {
        lock (_gate)
        {
            double now = CurtainData.Now;
            return new(AxisView.Create(options.X, _x, _xError, _connected, now, options.StaleAfterMs),
                AxisView.Create(options.Y, _y, _yError, _connected, now, options.StaleAfterMs),
                _connected, _detail, options.Demo);
        }
    }

    public void Reset(string detail)
    {
        lock (_gate)
        {
            _connected = false;
            _detail = detail;
            _x = _y = null;
            _xError = _yError = null;
        }
    }

    public async Task RunAsync(string endpoint, CancellationToken cancellation)
    {
        while (!cancellation.IsCancellationRequested)
        {
            Reset("Connecting to ROSbridge...");
            using var socket = new ClientWebSocket();
            socket.Options.KeepAliveInterval = TimeSpan.FromSeconds(10);
            try
            {
                using var connectTimeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation);
                connectTimeout.CancelAfter(TimeSpan.FromSeconds(5));
                await socket.ConnectAsync(new Uri(endpoint), connectTimeout.Token);
                await Subscribe(socket, "shelf-x", options.X.Topic, cancellation);
                await Subscribe(socket, "shelf-y", options.Y.Topic, cancellation);
                lock (_gate) { _connected = true; _detail = "ROSbridge connected / read only"; }
                var buffer = new byte[16 * 1024];
                while (socket.State == WebSocketState.Open && !cancellation.IsCancellationRequested)
                {
                    using var packet = new MemoryStream();
                    WebSocketReceiveResult fragment;
                    do
                    {
                        fragment = await socket.ReceiveAsync(new ArraySegment<byte>(buffer), cancellation);
                        if (fragment.MessageType == WebSocketMessageType.Close)
                            throw new WebSocketException("ROSbridge closed the connection.");
                        if (fragment.MessageType != WebSocketMessageType.Text || packet.Length + fragment.Count > 1_048_576)
                            throw new FormatException("Unsupported or oversized ROSbridge message.");
                        packet.Write(buffer, 0, fragment.Count);
                    } while (!fragment.EndOfMessage);
                    ProcessPacket(packet.ToArray());
                }
            }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { break; }
            catch (Exception error) when (error is WebSocketException or OperationCanceledException or
                IOException or JsonException or FormatException or InvalidOperationException)
            {
                Reset($"Disconnected: {error.Message}");
            }
            if (!cancellation.IsCancellationRequested)
            {
                try { await Task.Delay(1500, cancellation); }
                catch (OperationCanceledException) { break; }
            }
        }
        Reset("Disconnected");
    }

    private static Task Subscribe(ClientWebSocket socket, string id, string topic, CancellationToken token)
    {
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(new
        {
            op = "subscribe", id, topic, type = "light_curtain/msg/CurtainStatus",
            throttle_rate = 100, queue_length = 1
        });
        return socket.SendAsync(new ArraySegment<byte>(bytes), WebSocketMessageType.Text, true, token);
    }

    private void ProcessPacket(byte[] bytes)
    {
        using var json = JsonDocument.Parse(bytes);
        var root = json.RootElement;
        if (!root.TryGetProperty("op", out var op)) return;
        if (op.GetString() == "status" && root.TryGetProperty("level", out var level) && level.GetString() == "error")
        {
            string id = root.TryGetProperty("id", out var identifier) ? identifier.GetString() ?? "" : "";
            string reason = root.TryGetProperty("msg", out var msg) ? msg.ToString() : "ROS subscription error";
            lock (_gate)
            {
                if (id is "shelf-x" or "") _xError = reason;
                if (id is "shelf-y" or "") _yError = reason;
            }
            return;
        }
        if (op.GetString() != "publish" || !root.TryGetProperty("topic", out var topicValue)) return;
        string? topic = topicValue.GetString();
        if (topic != options.X.Topic && topic != options.Y.Topic) return;
        bool xAxis = topic == options.X.Topic;
        try
        {
            var frame = CurtainData.Parse(root.GetProperty("msg"),
                xAxis ? options.X.BeamCount : options.Y.BeamCount, CurtainData.Now);
            lock (_gate)
            {
                if (xAxis) { _x = frame; _xError = null; }
                else { _y = frame; _yError = null; }
            }
        }
        catch (Exception error) when (error is JsonException or FormatException or
            InvalidOperationException or KeyNotFoundException or OverflowException)
        {
            lock (_gate)
            {
                if (xAxis) { _x = null; _xError = $"Invalid axis data: {error.Message}"; }
                else { _y = null; _yError = $"Invalid axis data: {error.Message}"; }
            }
        }
    }

    public void UpdateDemo(bool both, int step)
    {
        double now = CurtainData.Now;
        long stamp = (long)(now * 1_000_000_000);
        AxisFrame MakeFrame(AxisOptions axis, int center) => new(
            Enumerable.Range(0, axis.BeamCount)
                .Select(i => new BeamReading(Math.Abs(i - center) <= 2, 0)).ToArray(), stamp, now);
        lock (_gate)
        {
            _connected = true;
            _detail = "DEMO / synthetic data / no hardware connection";
            _x = MakeFrame(options.X, 16 + step % 12);
            _y = both ? MakeFrame(options.Y, 32) : null;
        }
    }
}
