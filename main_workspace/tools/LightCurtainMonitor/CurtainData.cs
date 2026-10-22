using System.Diagnostics;
using System.Text.Json;

namespace LightCurtainMonitor;

public sealed record BeamReading(bool Blocked, int Health);

public sealed record AxisFrame(BeamReading[] Beams, long SourceNanoseconds, double ReceivedAt);

public enum AxisHealth { Disconnected, Live, Stale, Fault }

public sealed record AxisView(AxisOptions Options, AxisHealth Health, AxisFrame? Frame,
    double AgeMs, string Detail)
{
    public bool Live => Health == AxisHealth.Live;
    public int Blocked => Live ? Frame!.Beams.Count(beam => beam.Blocked) : 0;
    public bool? BeamState(int rawIndex)
    {
        if (rawIndex < 0 || rawIndex >= Options.BeamCount)
            throw new ArgumentOutOfRangeException(nameof(rawIndex));
        if (Health is AxisHealth.Disconnected or AxisHealth.Stale || Frame == null)
            return null;
        return Frame.Beams[rawIndex].Health == 0 ? Frame.Beams[rawIndex].Blocked : null;
    }

    public static AxisView Create(AxisOptions options, AxisFrame? frame, string? error,
        bool connected, double now, int staleAfterMs)
    {
        double age = frame == null ? double.PositiveInfinity : Math.Max(0, now - frame.ReceivedAt) * 1000;
        if (!connected) return new(options, AxisHealth.Disconnected, null, age, "ROSbridge offline");
        if (error != null) return new(options, AxisHealth.Fault, null, age, error);
        if (frame == null) return new(options, AxisHealth.Disconnected, null, age, "No axis data / not connected");
        if (age > staleAfterMs) return new(options, AxisHealth.Stale, null, age, "No recent axis data");
        if (frame.Beams.Any(beam => beam.Health != 0))
            return new(options, AxisHealth.Fault, frame, age, "Beam health stale or faulted");
        return new(options, AxisHealth.Live, frame, age, "Beam data live");
    }
}

public sealed record ShelfSnapshot(AxisView X, AxisView Y, bool BridgeConnected,
    string BridgeDetail, bool Demo);

public static class CurtainData
{
    public static double Now => Stopwatch.GetTimestamp() / (double)Stopwatch.Frequency;

    public static AxisFrame Parse(JsonElement message, int expectedCount, double receivedAt)
    {
        int count = message.GetProperty("beam_count").GetInt32();
        if (count != expectedCount) throw new FormatException($"Expected {expectedCount} beams, received {count}.");
        var array = message.GetProperty("beams");
        if (array.GetArrayLength() != count) throw new FormatException("Incomplete beam list.");
        var readings = new BeamReading[count];
        foreach (var beam in array.EnumerateArray())
        {
            int index = beam.GetProperty("index").GetInt32();
            int health = beam.GetProperty("health").GetInt32();
            if (index < 0 || index >= count || readings[index] != null || health is < 0 or > 2)
                throw new FormatException("Invalid or duplicate beam index / health.");
            readings[index] = new(beam.GetProperty("blocked").GetBoolean(), health);
        }
        var stamp = message.GetProperty("header").GetProperty("stamp");
        long seconds = stamp.GetProperty("sec").GetInt64();
        long nanos = stamp.GetProperty("nanosec").GetInt64();
        if (seconds < 0 || seconds > int.MaxValue || nanos is < 0 or >= 1_000_000_000)
            throw new FormatException("Invalid ROS timestamp.");
        return new(readings, checked(seconds * 1_000_000_000 + nanos), receivedAt);
    }
}
