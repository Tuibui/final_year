using Avalonia;

namespace LightCurtainMonitor;

internal static class Program
{
    public static MonitorOptions Options { get; private set; } = new();

    [STAThread]
    public static int Main(string[] args)
    {
        try
        {
            Options = MonitorOptions.FromArguments(args);
            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"LightCurtainMonitor: {error.Message}");
            return 1;
        }
    }

    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
        .UsePlatformDetect().WithInterFont().LogToTrace();
}
