using System.Runtime.InteropServices;

namespace Scylla;

// Observe native modal move/size and minimize state without changing XAML from WndProc.
internal sealed class WindowInteractionMonitor : IDisposable
{
    private readonly IntPtr _hwnd;
    private readonly SubclassProc _callback;
    private const nuint Id = 0x5343594D;
    public bool MovingOrSizing { get; private set; }
    public bool Minimized { get; private set; }
    public bool DeferPresentation => MovingOrSizing || Minimized;
    public WindowInteractionMonitor(IntPtr hwnd)
    {
        _hwnd = hwnd;
        _callback = OnMessage;
        Minimized = IsIconic(hwnd);
        if (!SetWindowSubclass(hwnd, _callback, Id, 0)) throw new InvalidOperationException("Could not observe window interactions.");
    }
    private IntPtr OnMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam, nuint id, nuint data)
    {
        switch (message)
        {
            case 0x0231: MovingOrSizing = true; break; // WM_ENTERSIZEMOVE
            case 0x0232: MovingOrSizing = false; break; // WM_EXITSIZEMOVE
            case 0x0005: Minimized = wParam.ToInt64() == 1; break; // WM_SIZE / SIZE_MINIMIZED
            case 0x0082: RemoveWindowSubclass(hwnd, _callback, Id); break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
    public void Dispose() => RemoveWindowSubclass(_hwnd, _callback, Id);
    private delegate IntPtr SubclassProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam, nuint id, nuint data);
    [DllImport("comctl32.dll")] private static extern bool SetWindowSubclass(IntPtr hwnd, SubclassProc callback, nuint id, nuint data);
    [DllImport("comctl32.dll")] private static extern bool RemoveWindowSubclass(IntPtr hwnd, SubclassProc callback, nuint id);
    [DllImport("comctl32.dll")] private static extern IntPtr DefSubclassProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
}
