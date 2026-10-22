using System.Text.Json;

namespace LightCurtainMonitor;

public sealed class AxisOptions
{
    public string Port { get; set; } = "A";
    public string Topic { get; set; } = "/gl_r/status";
    public int BeamCount { get; set; } = 52;
    public bool Reverse { get; set; }

    public void Validate()
    {
        if (Port is not ("A" or "B")) throw new ArgumentException("GC-Link port must be A or B.");
        if (!Topic.StartsWith('/') || Topic.Any(char.IsWhiteSpace))
            throw new ArgumentException("Each axis needs an absolute ROS topic without spaces.");
        if (BeamCount is < 1 or > 240)
            throw new ArgumentException("beamCount must be between 1 and 240.");
    }

    public int DisplayIndex(int rawIndex)
    {
        if (rawIndex < 0 || rawIndex >= BeamCount) throw new ArgumentOutOfRangeException(nameof(rawIndex));
        return Reverse ? BeamCount - 1 - rawIndex : rawIndex;
    }
}

public sealed class MonitorOptions
{
    public string Endpoint { get; set; } = "ws://172.20.10.3:9090";
    public int StaleAfterMs { get; set; } = 1000;
    public AxisOptions X { get; set; } = new() { Reverse = true };
    public AxisOptions Y { get; set; } = new() { Port = "B", Topic = "/gl_r_y/status" };
    public bool Demo { get; set; }
    public bool DemoBoth { get; set; }

    public void Validate()
    {
        ValidateEndpoint(Endpoint);
        X.Validate();
        Y.Validate();
        if (X.Topic == Y.Topic) throw new ArgumentException("X and Y must use different topics.");
        if (StaleAfterMs is < 100 or > 60000)
            throw new ArgumentException("Invalid stale timeout.");
    }

    public static void ValidateEndpoint(string value)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out var uri) ||
            uri.Scheme is not ("ws" or "wss") || string.IsNullOrWhiteSpace(uri.Host))
            throw new ArgumentException("Endpoint must be a ws:// or wss:// URL.");
    }

    public static MonitorOptions FromArguments(string[] args)
    {
        string path = Path.Combine(AppContext.BaseDirectory, "monitor.json");
        string? endpoint = null;
        bool demo = false, both = false;
        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--config" when i + 1 < args.Length: path = args[++i]; break;
                case "--endpoint" when i + 1 < args.Length: endpoint = args[++i]; break;
                case "--demo": demo = true; break;
                case "--demo-both": demo = both = true; break;
                default: throw new ArgumentException($"Unknown or incomplete argument: {args[i]}");
            }
        }
        var options = JsonSerializer.Deserialize<MonitorOptions>(File.ReadAllText(path),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new ArgumentException("Empty monitor configuration.");
        if (endpoint != null) options.Endpoint = endpoint;
        // Synthetic data requires a command-line flag, never a connection failure or config value.
        options.Demo = demo;
        options.DemoBoth = both;
        options.Validate();
        return options;
    }
}
