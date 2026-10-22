using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace LightCurtainMonitor;

public sealed class ShelfView : Control
{
    private ShelfSnapshot? _snapshot;
    private static readonly IBrush Ink = Brush("#18212B"), Muted = Brush("#687481"),
        Line = Brush("#8C969F"), Yellow = Brush("#F4CA25"), Red = Brush("#B42318"),
        Green = Brush("#16734B"), Unknown = Brush("#B5BCC4"), ClearRay = Brush("#E1ECE5"),
        Emitter = Brush("#5D542E");
    private static readonly Typeface Font = new("Inter, DejaVu Sans, sans-serif");
    private static readonly Typeface Mono = new("DejaVu Sans Mono, Consolas, monospace");
    private const double Left = 112, Top = 105, Size = 520;

    public ShelfView()
    {
        Width = 760;
        Height = 735;
        PointerMoved += (_, e) =>
        {
            if (_snapshot is not { } state) return;
            var point = e.GetPosition(this);
            bool nearShelf = point.X >= 62 && point.X <= 704 && point.Y >= 54 && point.Y <= 710;
            if (!nearShelf) { ToolTip.SetTip(this, null); return; }
            int xDisplay = Math.Clamp((int)((point.Y - Top) / Size * state.X.Options.BeamCount), 0, state.X.Options.BeamCount - 1);
            int yDisplay = Math.Clamp((int)((point.X - Left) / Size * state.Y.Options.BeamCount), 0, state.Y.Options.BeamCount - 1);
            int x = state.X.Options.DisplayIndex(xDisplay), y = state.Y.Options.DisplayIndex(yDisplay);
            ToolTip.SetTip(this, $"X beam {x + 1}: {Status(state.X.BeamState(x))}\nY beam {y + 1}: {Status(state.Y.BeamState(y))}");
        };
    }

    public void Update(ShelfSnapshot snapshot) { _snapshot = snapshot; InvalidateVisual(); }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        context.FillRectangle(Brushes.White, new Rect(Bounds.Size));
        if (_snapshot is not { } state) return;
        Text(context, "Y TRANSMITTER", Left + Size / 2, 29, 11, Ink, true, true);
        Text(context, "X TX", 75, 80, 11, Ink, true, true);
        Text(context, "X RX", 669, 80, 11, Ink, true, true);

        // Each received beam keeps its raw identity; shelf divisions are only visual references.
        DrawRays(context, state.Y, verticalRays: true, blockedOnly: false);
        DrawRays(context, state.X, verticalRays: false, blockedOnly: false);
        for (int edge = 0; edge <= 5; edge++)
        {
            double offset = edge * Size / 5;
            context.DrawLine(new Pen(Line, edge is 0 or 5 ? 1.5 : 1),
                new Point(Left + offset, Top), new Point(Left + offset, Top + Size));
            context.DrawLine(new Pen(Line, edge is 0 or 5 ? 1.5 : 1),
                new Point(Left, Top + offset), new Point(Left + Size, Top + offset));
        }
        DrawRays(context, state.Y, verticalRays: true, blockedOnly: true);
        DrawRays(context, state.X, verticalRays: false, blockedOnly: true);

        DrawBar(context, state.X, new Rect(62, Top, 26, Size), vertical: true, receiver: false);
        DrawBar(context, state.X, new Rect(656, Top, 26, Size), vertical: true, receiver: true);
        DrawBar(context, state.Y, new Rect(Left, 55, Size, 26), vertical: false, receiver: false);
        DrawBar(context, state.Y, new Rect(Left, 649, Size, 26), vertical: false, receiver: true);

        for (int raw = 0; raw < state.X.Options.BeamCount; raw++)
        {
            double y = Top + (state.X.Options.DisplayIndex(raw) + 0.5) * Size / state.X.Options.BeamCount;
            Text(context, $"{raw + 1:00}", 46, y - 4, 7.5, Muted, true, true);
            Text(context, $"{raw + 1:00}", 699, y - 4, 7.5, Muted, true, true);
        }
        for (int raw = 0; raw < state.Y.Options.BeamCount; raw++)
        {
            double x = Left + (state.Y.Options.DisplayIndex(raw) + 0.5) * Size / state.Y.Options.BeamCount;
            Text(context, $"{raw + 1:00}", x, 89, 7.5, Muted, true, true);
            Text(context, $"{raw + 1:00}", x, 685, 7.5, Muted, true, true);
        }
        Text(context, "Y RECEIVER", Left + Size / 2, 709, 11, Ink, true, true);
    }

    private static void DrawRays(DrawingContext context, AxisView axis, bool verticalRays, bool blockedOnly)
    {
        for (int raw = 0; raw < axis.Options.BeamCount; raw++)
        {
            bool? blocked = axis.BeamState(raw);
            if (blocked == null || blocked.Value != blockedOnly) continue;
            double offset = (axis.Options.DisplayIndex(raw) + 0.5) * Size / axis.Options.BeamCount;
            var pen = new Pen(blocked.Value ? Red : ClearRay, blocked.Value ? 1.3 : 0.6);
            context.DrawLine(pen, verticalRays ? new Point(Left + offset, Top) : new Point(Left, Top + offset),
                verticalRays ? new Point(Left + offset, Top + Size) : new Point(Left + Size, Top + offset));
        }
    }

    private static void DrawBar(DrawingContext context, AxisView axis, Rect bar, bool vertical, bool receiver)
    {
        context.FillRectangle(Yellow, bar);
        context.DrawRectangle(new Pen(Ink, 1), bar);
        double pitch = (vertical ? bar.Height : bar.Width) / axis.Options.BeamCount;
        for (int raw = 0; raw < axis.Options.BeamCount; raw++)
        {
            double offset = axis.Options.DisplayIndex(raw) * pitch;
            bool? blocked = axis.BeamState(raw);
            IBrush color = !receiver ? Emitter : blocked == null ? Unknown : blocked.Value ? Red : Green;
            Rect led = vertical
                ? new Rect(bar.X + 5, bar.Y + offset + 1.5, bar.Width - 10, Math.Max(1, pitch - 3))
                : new Rect(bar.X + offset + 1.5, bar.Y + 5, Math.Max(1, pitch - 3), bar.Height - 10);
            context.FillRectangle(color, led);
            if (receiver)
                Text(context, blocked == null ? "—" : blocked.Value ? "1" : "0", led.Center.X,
                    led.Center.Y - 3.5, 6.5, blocked == null ? Ink : Brushes.White, true, true);
        }
    }

    private static string Status(bool? blocked) => blocked == null ? "— / unavailable" : blocked.Value ? "1 / blocked" : "0 / clear";
    private static SolidColorBrush Brush(string color) => new(Color.Parse(color));
    private static void Text(DrawingContext context, string value, double x, double y,
        double size, IBrush color, bool mono = false, bool centered = false)
    {
        var text = new FormattedText(value, System.Globalization.CultureInfo.InvariantCulture,
            FlowDirection.LeftToRight, mono ? Mono : Font, size, color);
        context.DrawText(text, new Point(centered ? x - text.Width / 2 : x, y));
    }
}
