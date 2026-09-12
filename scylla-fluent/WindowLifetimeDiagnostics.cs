using System.Runtime.InteropServices;
using System.Text;

namespace Scylla;

// Record the native destruction stack before WinUI later fails on a stale bridge HWND.
internal static class WindowLifetimeDiagnostics
{
    private static readonly SubclassProc Callback = OnMessage;
    private static readonly HashSet<IntPtr> Watched = new();
    private const nuint Id = 0x5343594C;

    public static void WatchBridge(IntPtr owner)
    {
        var bridge = FindWindowEx(owner, IntPtr.Zero, "Microsoft.UI.Content.DesktopChildSiteBridge", null);
        if (bridge == IntPtr.Zero || Watched.Contains(bridge)) return;
        if (SetWindowSubclass(bridge, Callback, Id, 0))
        {
            Watched.Add(bridge);
            App.Log($"Watching XAML bridge hwnd=0x{bridge.ToInt64():x} owner=0x{owner.ToInt64():x}");
        }
    }

    private static IntPtr OnMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam, nuint id, nuint data)
    {
        if (message == 0x0082 /* WM_NCDESTROY */)
        {
            try
            {
                var frames = new IntPtr[40];
                var count = CaptureStackBackTrace(0, (uint)frames.Length, frames, IntPtr.Zero);
                var stack = new StringBuilder($"XAML bridge destroyed hwnd=0x{hwnd.ToInt64():x}\n");
                stack.AppendLine(Environment.StackTrace);
                for (var i = 0; i < count; i++)
                {
                    var name = new StringBuilder(512);
                    if (GetModuleHandleEx(6 /* FROM_ADDRESS | UNCHANGED_REFCOUNT */, frames[i], out var module))
                    {
                        GetModuleFileName(module, name, name.Capacity);
                        stack.AppendLine($"{Path.GetFileName(name.ToString())}+0x{frames[i].ToInt64() - module.ToInt64():x}");
                    }
                    else stack.AppendLine($"0x{frames[i].ToInt64():x}");
                }
                App.Log(stack.ToString());
            }
            catch { /* Diagnostics must never interfere with native message processing. */ }
            Watched.Remove(hwnd);
            RemoveWindowSubclass(hwnd, Callback, Id);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    private delegate IntPtr SubclassProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam, nuint id, nuint data);
    [DllImport("comctl32.dll")] private static extern bool SetWindowSubclass(IntPtr hwnd, SubclassProc callback, nuint id, nuint data);
    [DllImport("comctl32.dll")] private static extern bool RemoveWindowSubclass(IntPtr hwnd, SubclassProc callback, nuint id);
    [DllImport("comctl32.dll")] private static extern IntPtr DefSubclassProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string name, string? title);
    [DllImport("kernel32.dll", EntryPoint = "RtlCaptureStackBackTrace")] private static extern ushort CaptureStackBackTrace(uint skip, uint count, [Out] IntPtr[] frames, IntPtr hash);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern bool GetModuleHandleEx(uint flags, IntPtr address, out IntPtr module);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern uint GetModuleFileName(IntPtr module, StringBuilder name, int size);
}
