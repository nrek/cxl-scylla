using System.Runtime.InteropServices;

namespace Scylla;

// UI-thread only. All native surfaces share one composition region per window.
internal static class NativeSurfaceCutouts
{
    private sealed class State
    {
        public readonly Dictionary<object, (int X, int Y, int W, int H)> Holes = new();
        public string Applied = "";
    }
    private static readonly Dictionary<IntPtr, State> States = new();

    internal static void Update(IntPtr bridge, object owner, int x, int y, int w, int h)
    {
        if (!States.TryGetValue(bridge, out var state)) States[bridge] = state = new();
        state.Holes[owner] = (x, y, w, h);
        Apply(bridge, state);
    }

    internal static void Remove(IntPtr bridge, object owner)
    {
        if (!States.TryGetValue(bridge, out var state) || !state.Holes.Remove(owner)) return;
        if (state.Holes.Count == 0)
        {
            SetWindowRgn(bridge, IntPtr.Zero, true);
            States.Remove(bridge);
        }
        else Apply(bridge, state);
    }

    private static void Apply(IntPtr bridge, State state)
    {
        if (!GetClientRect(bridge, out var bounds)) return;
        var key = $"{bounds.Right},{bounds.Bottom}:" + string.Join(";", state.Holes.Values);
        if (state.Applied == key) return;
        var region = CreateRectRgn(0, 0, bounds.Right, bounds.Bottom);
        if (region == IntPtr.Zero) return;
        try
        {
            foreach (var (x, y, w, h) in state.Holes.Values)
            {
                var hole = CreateRectRgn(x, y, x + w, y + h);
                if (hole == IntPtr.Zero) return;
                try { if (CombineRgn(region, region, hole, 4) == 0) return; }
                finally { DeleteObject(hole); }
            }
            if (SetWindowRgn(bridge, region, true) == 0) return;
            region = IntPtr.Zero;
            state.Applied = key;
        }
        finally { if (region != IntPtr.Zero) DeleteObject(region); }
    }

    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] private static extern int SetWindowRgn(IntPtr hwnd, IntPtr region, bool redraw);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateRectRgn(int left, int top, int right, int bottom);
    [DllImport("gdi32.dll")] private static extern int CombineRgn(IntPtr target, IntPtr first, IntPtr second, int mode);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);
}
