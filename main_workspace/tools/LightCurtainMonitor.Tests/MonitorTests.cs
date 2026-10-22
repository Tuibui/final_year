using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Text.Json;
using Avalonia;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Xunit;

[assembly: AvaloniaTestApplication(typeof(LightCurtainMonitor.Tests.TestAppBuilder))]

namespace LightCurtainMonitor.Tests;

public static class TestAppBuilder
{
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
        .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
        .UseSkia().WithInterFont();
}

public sealed class MonitorTests
{
    private static AxisFrame Frame(int blockedIndex, long stamp = 1_000_000_000, double received = 100) => new(
        Enumerable.Range(0, 52).Select(i => new BeamReading(i == blockedIndex, 0)).ToArray(), stamp, received);

    private static AxisView View(AxisOptions options, AxisFrame? frame, double now = 100) =>
        AxisView.Create(options, frame, null, true, now, 1000);

    [Fact]
    public void SwappedAxesKeepRawBeamIdentityAndReverseTheVerticalBars()
    {
        var options = new MonitorOptions();
        options.Validate();
        Assert.Equal(51, options.X.DisplayIndex(0));
        Assert.Equal(0, options.X.DisplayIndex(51));
        Assert.Equal(0, options.Y.DisplayIndex(0));
        Assert.Equal(51, options.Y.DisplayIndex(51));
        Assert.Throws<ArgumentOutOfRangeException>(() => options.X.DisplayIndex(52));
        var x = View(options.X, Frame(15));
        Assert.True(x.BeamState(15));
        Assert.False(x.BeamState(14));
    }

    [Fact]
    public void MissingOrStaleAxisIsUnknownForEveryBeam()
    {
        var options = new MonitorOptions();
        var missing = View(options.Y, null);
        Assert.Equal(AxisHealth.Disconnected, missing.Health);
        Assert.All(Enumerable.Range(0, 52), i => Assert.Null(missing.BeamState(i)));
        var stale = View(options.Y, Frame(40), now: 102);
        Assert.Equal(AxisHealth.Stale, stale.Health);
        Assert.All(Enumerable.Range(0, 52), i => Assert.Null(stale.BeamState(i)));
    }

    [Fact]
    public void FaultAndBridgeLossDoNotAppearClear()
    {
        var options = new MonitorOptions();
        var frame = Frame(15);
        frame.Beams[0] = new(false, 2);
        var fault = View(options.X, frame);
        Assert.Equal(AxisHealth.Fault, fault.Health);
        Assert.False(fault.Live);
        var offline = AxisView.Create(options.X, Frame(15), null, false, 100, 1000);
        Assert.Null(offline.Frame);
        Assert.Null(offline.BeamState(15));
        Assert.Null(fault.BeamState(0));
        Assert.True(fault.BeamState(15));
    }

    private static object Message(bool duplicate = false) => new
    {
        header = new { stamp = new { sec = 1, nanosec = 0 } }, beam_count = 52,
        beams = Enumerable.Range(0, 52).Select(i => new { index = duplicate && i == 51 ? 0 : i,
            blocked = i == 15, health = 0 }).ToArray()
    };

    [Fact]
    public void MalformedDataAndInvalidConfigurationAreRejected()
    {
        var duplicate = JsonSerializer.SerializeToElement(Message(duplicate: true));
        Assert.Throws<FormatException>(() => CurtainData.Parse(duplicate, 52, 100));
        var valid = JsonSerializer.SerializeToElement(Message());
        Assert.Throws<FormatException>(() => CurtainData.Parse(valid, 64, 100));
        Assert.True(CurtainData.Parse(valid, 52, 100).Beams[15].Blocked);
        var config = new MonitorOptions();
        config.Y.Topic = config.X.Topic;
        Assert.Throws<ArgumentException>(config.Validate);
        config = new();
        config.X.BeamCount = 0;
        Assert.Throws<ArgumentException>(config.Validate);
    }

    [Fact]
    public async Task WebSocketSubscriptionsFragmentedFramesAndDisconnectAreHandled()
    {
        var portProbe = new TcpListener(IPAddress.Loopback, 0);
        portProbe.Start();
        int port = ((IPEndPoint)portProbe.LocalEndpoint).Port;
        portProbe.Stop();
        using var server = new HttpListener();
        server.Prefixes.Add($"http://127.0.0.1:{port}/");
        server.Start();
        using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var client = new RosbridgeClient(new MonitorOptions());
        var running = client.RunAsync($"ws://127.0.0.1:{port}/", cancellation.Token);
        try
        {
            var context = await server.GetContextAsync().WaitAsync(cancellation.Token);
            using var socket = (await context.AcceptWebSocketAsync(null)).WebSocket;
            var buffer = new byte[4096];
            var topics = new List<string>();
            for (int i = 0; i < 2; i++)
            {
                var response = await socket.ReceiveAsync(new ArraySegment<byte>(buffer), cancellation.Token);
                using var subscription = JsonDocument.Parse(buffer.AsMemory(0, response.Count));
                Assert.Equal("subscribe", subscription.RootElement.GetProperty("op").GetString());
                topics.Add(subscription.RootElement.GetProperty("topic").GetString()!);
            }
            Assert.Equal(new[] { "/gl_r/status", "/gl_r_y/status" }, topics);
            byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(new { op = "publish", topic = "/gl_r/status", msg = Message() });
            await socket.SendAsync(new ArraySegment<byte>(bytes, 0, 100), WebSocketMessageType.Text, false, cancellation.Token);
            await socket.SendAsync(new ArraySegment<byte>(bytes, 100, bytes.Length - 100), WebSocketMessageType.Text, true, cancellation.Token);
            await WaitUntil(() => client.Snapshot().X.Live, cancellation.Token);
            Assert.Equal(1, client.Snapshot().X.Blocked);
            Assert.Equal(AxisHealth.Disconnected, client.Snapshot().Y.Health);
            Assert.Null(client.Snapshot().Y.BeamState(15));
            await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "test complete", cancellation.Token);
            await WaitUntil(() => !client.Snapshot().BridgeConnected, cancellation.Token);
            Assert.Null(client.Snapshot().X.Frame);
        }
        finally
        {
            await cancellation.CancelAsync();
            await running;
            server.Stop();
        }
    }

    private static async Task WaitUntil(Func<bool> ready, CancellationToken token)
    {
        while (!ready()) await Task.Delay(10, token);
    }

    [AvaloniaFact]
    public void RenderDebugScreensWithoutHardware()
    {
        var options = new MonitorOptions { Demo = true };
        var client = new RosbridgeClient(options);
        var window = new MainWindow(options, startServices: false);
        window.Show();
        string directory = Environment.GetEnvironmentVariable("MONITOR_SCREENSHOT_DIR")
            ?? Path.GetFullPath("../../../../../../runtime/gui-validation", AppContext.BaseDirectory);
        Directory.CreateDirectory(directory);
        try
        {
            client.UpdateDemo(both: false, step: 0);
            window.RenderSnapshot(client.Snapshot());
            using (var screenshot = window.CaptureRenderedFrame())
            {
                Assert.NotNull(screenshot);
                screenshot.Save(Path.Combine(directory, "x-live-y-disconnected.png"));
            }
            client.UpdateDemo(both: true, step: 0);
            window.RenderSnapshot(client.Snapshot());
            using (var screenshot = window.CaptureRenderedFrame())
            {
                Assert.NotNull(screenshot);
                screenshot.Save(Path.Combine(directory, "both-axes-raw-beams.png"));
            }
        }
        finally { window.Close(); }
    }
}
