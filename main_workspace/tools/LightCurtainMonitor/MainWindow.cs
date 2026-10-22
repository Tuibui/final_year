using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace LightCurtainMonitor;

public sealed class MainWindow : Window
{
    private readonly MonitorOptions _options;
    private readonly RosbridgeClient _client;
    private readonly ShelfView _shelf = new();
    private readonly TextBlock _xStatus = StatusLabel("X: DISCONNECTED");
    private readonly TextBlock _yStatus = StatusLabel("Y: DISCONNECTED");
    private readonly TextBlock _bridge = StatusLabel("ROSbridge: connecting");
    private readonly TextBox _endpoint = new() { Watermark = "ws://localhost:9090", MinWidth = 180, FontSize = 12 };
    private readonly Button _connect = new() { Content = "Reconnect", FontSize = 12 };
    private readonly DispatcherTimer _timer;
    private CancellationTokenSource? _cancellation;
    private Task? _connectionTask;
    private int _demoStep;
    private bool _closed;

    public MainWindow(MonitorOptions options, bool startServices = true)
    {
        _options = options;
        _client = new(options);
        Title = options.Demo ? "Light curtain / DEMO" : "Light curtain";
        Width = 1100;
        Height = 900;
        MinWidth = 900;
        MinHeight = 650;
        Background = Brushes.White;
        _endpoint.Text = options.Endpoint;
        _endpoint.IsEnabled = !options.Demo;
        _connect.IsEnabled = !options.Demo;

        var root = new Grid { RowDefinitions = new("Auto,*"), Margin = new Thickness(16, 12) };
        var toolbar = new Grid { ColumnDefinitions = new("Auto,Auto,Auto,*,Auto"),
            ColumnSpacing = 16, Margin = new Thickness(0, 0, 0, 8) };
        Control[] controls = [_xStatus, _yStatus, _bridge, _endpoint, _connect];
        for (int column = 0; column < controls.Length; column++)
        {
            Grid.SetColumn(controls[column], column);
            toolbar.Children.Add(controls[column]);
        }
        root.Children.Add(toolbar);
        var display = new Viewbox { Child = _shelf, Stretch = Stretch.Uniform,
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
        Grid.SetRow(display, 1);
        root.Children.Add(display);
        Content = root;

        _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
        _timer.Tick += (_, _) =>
        {
            if (_options.Demo) _client.UpdateDemo(_options.DemoBoth, _demoStep++ / 5);
            RenderSnapshot(_client.Snapshot());
        };
        _connect.Click += async (_, _) => await RestartConnectionAsync();
        Opened += async (_, _) =>
        {
            if (!startServices) return;
            _timer.Start();
            if (!options.Demo) await RestartConnectionAsync();
        };
        Closed += (_, _) =>
        {
            _closed = true;
            _timer.Stop();
            _cancellation?.Cancel();
        };
        RenderSnapshot(_client.Snapshot());
    }

    private async Task RestartConnectionAsync()
    {
        string endpoint = _endpoint.Text?.Trim() ?? "";
        try { MonitorOptions.ValidateEndpoint(endpoint); }
        catch (ArgumentException error)
        {
            _bridge.Text = "ROSbridge: invalid URL";
            _bridge.Foreground = Brush("#B42318");
            ToolTip.SetTip(_bridge, error.Message);
            return;
        }
        _connect.IsEnabled = false;
        try
        {
            if (_cancellation != null) await _cancellation.CancelAsync();
            if (_connectionTask != null) await _connectionTask;
            _cancellation?.Dispose();
            if (_closed) return;
            _cancellation = new();
            _client.Reset("Connecting...");
            _connectionTask = _client.RunAsync(endpoint, _cancellation.Token);
        }
        finally { if (!_closed) _connect.IsEnabled = true; }
    }

    public void RenderSnapshot(ShelfSnapshot state)
    {
        _shelf.Update(state);
        UpdateAxisStatus(_xStatus, "X", state.X);
        UpdateAxisStatus(_yStatus, "Y", state.Y);
        _bridge.Text = state.Demo ? "ROSbridge: DEMO" : state.BridgeConnected ? "ROSbridge: connected" : "ROSbridge: disconnected";
        _bridge.Foreground = Brush(state.Demo ? "#92400E" : state.BridgeConnected ? "#16734B" : "#B42318");
        ToolTip.SetTip(_bridge, state.BridgeDetail);
    }

    private static void UpdateAxisStatus(TextBlock label, string name, AxisView axis)
    {
        label.Text = $"{name}: {axis.Health.ToString().ToUpperInvariant()}";
        label.Foreground = Brush(axis.Live ? "#16734B" : axis.Health == AxisHealth.Fault ? "#92400E" : "#B42318");
        ToolTip.SetTip(label, axis.Detail);
    }

    private static TextBlock StatusLabel(string text) => new()
    {
        Text = text, FontSize = 11, FontWeight = FontWeight.SemiBold,
        VerticalAlignment = VerticalAlignment.Center
    };

    private static SolidColorBrush Brush(string color) => new(Color.Parse(color));
}
