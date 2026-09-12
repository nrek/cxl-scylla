using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using WinRT.Interop;

namespace Scylla;

// Opt-in validation process: no instance mutex, session, provider, or workspace writes.
internal static class UiStressTest
{
    public static async void Run(WorkbenchWindow window)
    {
        try
        {
            await Task.Delay(400);
            var fixture = Path.Combine(AppContext.BaseDirectory, "ui-stress-fixture.cs");
            await File.WriteAllTextAsync(fixture, "// Resize stress fixture\nclass Example { int Value = 42; }\n");
            App.Log("Stress: opening editor");
            if (!Environment.GetCommandLineArgs().Contains("--ui-stress-no-editor")) window.StressOpenFile(fixture);
            App.Log("Stress: editor ready");
            var hwnd = WindowNative.GetWindowHandle(window);
            if (Environment.GetCommandLineArgs().Contains("--terminal-ui-test"))
            {
                await ValidateTerminal(window, hwnd);
                App.Log("PASS terminal UI: composition, prompt, focus, input, Payload and editor coexistence");
                window.Close();
                return;
            }
            var history = Enumerable.Range(0, 100).Select(i => new HistoryRow
            {
                User = i % 2 == 0,
                Text = $"Message {i}\n\nA completed message with **formatting**, `code`, and a [link](https://example.com).",
            }).ToList();
            var thread = new ThreadRow { Id = "stress", Name = "UI generation stress", UpdatedAt = DateTimeOffset.Now.ToUnixTimeSeconds() };
            SessionSnapshot? latest = null;
            using var stop = new CancellationTokenSource();
            var finished = false;
            var watch = Stopwatch.StartNew();
            _ = Task.Run(async () =>
            {
                await Task.Delay(55000);
                if (!Volatile.Read(ref finished)) { App.Log("FAIL UI stress watchdog expired"); Environment.Exit(4); }
            });
            var producer = Task.Run(async () =>
            {
                var n = 0;
                while (!stop.IsCancellationRequested)
                {
                    n++;
                    var rows = history.Concat(new[] { new HistoryRow { Text = $"Streaming update {n}\n" + new string('x', n % 16000) } }).ToList();
                    Volatile.Write(ref latest, new SessionSnapshot { State = "generating", ActiveThreadId = "stress",
                        Activity = $"Working · update {n}", History = rows, Threads = new() { thread } });
                    await Task.Delay(10);
                }
            });
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(40) };
            var tick = 0;
            var bridgeLost = false;
            var editorChecks = 0;
            var mediaChecks = 0;
            var hasEditor = !Environment.GetCommandLineArgs().Contains("--ui-stress-no-editor");
            var done = new TaskCompletionSource();
            timer.Tick += (_, _) =>
            {
                var snapshot = Volatile.Read(ref latest);
                if (snapshot != null) window.StressSnapshot(snapshot);
                if (tick % 5 == 0)
                {
                    var phase = tick / 5 % 6;
                    App.Log($"Stress: window phase {phase}, tick {tick}");
                    if (phase == 3) ShowWindow(hwnd, 6); // minimize
                    else if (phase == 4) ShowWindow(hwnd, 9); // restore
                    else SetWindowPos(hwnd, IntPtr.Zero, 40 + phase * 25, 50 + phase * 15,
                        phase == 1 ? 780 : phase == 2 ? 1060 : 2400, phase == 2 ? 640 : 1300, 0x14);
                    if (hasEditor && phase == 2) window.StressOpenFile(Path.Combine(AppContext.BaseDirectory, "Assets", "scylla-logo.png"));
                    if (hasEditor && phase == 5) window.StressOpenFile(fixture);
                    if (phase == 5) { GC.Collect(); GC.WaitForPendingFinalizers(); }
                }
                if (hasEditor && tick % 30 == 2) { ValidateEditor(hwnd, true); editorChecks++; }
                if (hasEditor && tick % 30 == 12) { ValidateEditor(hwnd, false); mediaChecks++; }
                bridgeLost |= FindWindowEx(hwnd, IntPtr.Zero, "Microsoft.UI.Content.DesktopChildSiteBridge", null) == IntPtr.Zero;
                if (++tick >= 360) { timer.Stop(); done.SetResult(); }
            };
            timer.Start();
            await done.Task;
            stop.Cancel();
            await producer;
            var rendered = window.StressMessageRenderCount;
            var pass = !bridgeLost && rendered >= 100 && rendered < 1000 && (!hasEditor || (editorChecks == 12 && mediaChecks == 12));
            App.Log($"{(pass ? "PASS" : "FAIL")} UI stress: ticks={tick}, messageRenders={rendered}, bridgeLost={bridgeLost}, editorChecks={editorChecks}, mediaChecks={mediaChecks}, elapsedMs={watch.ElapsedMilliseconds}");
            Volatile.Write(ref finished, true);
            Environment.ExitCode = pass ? 0 : 5;
            if (Environment.GetCommandLineArgs().Contains("--ui-stress-hold")) await Task.Delay(10000);
            window.Close();
        }
        catch (Exception ex) { App.Log("FAIL UI stress: " + ex); Environment.Exit(6); }
    }
    private static async Task ValidateTerminal(WorkbenchWindow window, IntPtr hwnd)
    {
        window.StressTerminal("terminal");
        var terminal = IntPtr.Zero;
        for (int i = 0; i < 100; i++)
        {
            await Task.Delay(100);
            terminal = FindWindowEx(hwnd, IntPtr.Zero, "RICHEDIT50W", null);
            if (terminal != IntPtr.Zero && SendMessage(terminal, 14, IntPtr.Zero, IntPtr.Zero).ToInt64() > 0) break;
        }
        if (terminal == IntPtr.Zero || SendMessage(terminal, 14, IntPtr.Zero, IntPtr.Zero).ToInt64() == 0)
            throw new InvalidOperationException("No terminal prompt rendered");
        ValidateCutout(hwnd, terminal);
        var editor = FindWindowEx(hwnd, IntPtr.Zero, "Scintilla", null);
        ValidateCutout(hwnd, editor);
        SetFocus(terminal);
        if (GetFocus() != terminal) throw new InvalidOperationException("Terminal cannot receive keyboard focus");
        var gui = new GuiInfo { Size = Marshal.SizeOf<GuiInfo>() };
        if (!GetGUIThreadInfo(GetWindowThreadProcessId(terminal, out _), ref gui) || gui.Caret != terminal)
            throw new InvalidOperationException("Terminal has no input caret");
        foreach (var c in "echo SCYLLA_UI_INPUT_OK") SendMessage(terminal, 0x102, (IntPtr)c, IntPtr.Zero);
        SendMessage(terminal, 0x100, (IntPtr)13, IntPtr.Zero);
        await Task.Delay(1500);
        var text = new System.Text.StringBuilder(65536);
        GetText(terminal, 13, (IntPtr)text.Capacity, text);
        if (text.ToString().Split("SCYLLA_UI_INPUT_OK").Length < 3)
            throw new InvalidOperationException("Terminal did not echo input and return command output");
        window.StressTerminal("payload");
        await Task.Delay(200);
        if (IsWindowVisible(terminal)) throw new InvalidOperationException("Terminal covers Payload");
        ValidateCutout(hwnd, editor);
        window.StressTerminal("terminal");
        window.StressOpenFile(Path.Combine(AppContext.BaseDirectory, "Assets", "scylla-logo.png"));
        await Task.Delay(200);
        ValidateCutout(hwnd, terminal);
        await ValidateTerminalNavigation(window, hwnd, terminal);
    }

    private static IEnumerable<Microsoft.UI.Xaml.DependencyObject> Descendants(Microsoft.UI.Xaml.DependencyObject root)
    {
        yield return root;
        for (int i = 0; i < Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChildrenCount(root); i++)
            foreach (var child in Descendants(Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChild(root, i))) yield return child;
    }

    private static void InvokeButton(Microsoft.UI.Xaml.Controls.Button button)
    {
        var peer = new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(button);
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)peer.GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
    }

    private static async Task ValidateTerminalNavigation(WorkbenchWindow window, IntPtr hwnd, IntPtr first)
    {
        var list = Descendants(window.Content).OfType<Microsoft.UI.Xaml.Controls.ListView>()
            .Single(v => Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(v) == "Terminal sessions");
        var add = Descendants(window.Content).OfType<Microsoft.UI.Xaml.Controls.Button>()
            .Single(v => Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(v) == "New terminal (default profile)");
        InvokeButton(add);
        await Task.Delay(700);
        if (list.Items.Count != 2 || list.SelectedIndex != 1 || IsWindowVisible(first))
            throw new InvalidOperationException("New terminal not selected in session list");
        // Native controls can change z-order when selected; use their stable control IDs.
        var second = GetDlgItem(hwnd, 9202);
        ValidateCutout(hwnd, second);
        list.SelectedIndex = 0;
        await Task.Delay(200);
        ValidateCutout(hwnd, first);
        if (IsWindowVisible(second)) throw new InvalidOperationException("Inactive terminal is visible");
        SetWindowPos(hwnd, IntPtr.Zero, 40, 50, 780, 700, 0x14);
        await Task.Delay(300);
        var start = list.TransformToVisual(null).TransformPoint(new Windows.Foundation.Point(0, 0));
        var bridge = FindWindowEx(hwnd, IntPtr.Zero, "Microsoft.UI.Content.DesktopChildSiteBridge", null);
        GetClientRect(first, out var bounds);
        var edge = new NativePoint { X = bounds.Right, Y = 0 };
        MapWindowPoints(first, bridge, ref edge, 1);
        if (list.ActualWidth < 40 || edge.X > start.X * list.XamlRoot.RasterizationScale + 2)
            throw new InvalidOperationException("Terminal overlaps session list after resize");
        var inactive = (Microsoft.UI.Xaml.Controls.ListViewItem)list.Items[1];
        InvokeButton(Descendants(inactive).OfType<Microsoft.UI.Xaml.Controls.Button>().Single());
        await Task.Delay(200);
        if (list.Items.Count != 1 || list.SelectedIndex != 0 || !IsWindowVisible(first))
            throw new InvalidOperationException("Closing inactive session changed active terminal");
        InvokeButton(Descendants((Microsoft.UI.Xaml.Controls.ListViewItem)list.Items[0]).OfType<Microsoft.UI.Xaml.Controls.Button>().Single());
        await Task.Delay(200);
        if (list.Items.Count != 0) throw new InvalidOperationException("Last terminal did not close");
        InvokeButton(add);
        await Task.Delay(300);
        if (list.Items.Count != 1 || list.SelectedIndex != 0) throw new InvalidOperationException("Cannot create terminal after closing last session");
        App.Log("PASS terminal navigation: create, select, narrow layout, inactive close, last close, recreate");
    }

    private static void ValidateCutout(IntPtr hwnd, IntPtr child)
    {
        var bridge = FindWindowEx(hwnd, IntPtr.Zero, "Microsoft.UI.Content.DesktopChildSiteBridge", null);
        if (child == IntPtr.Zero || !IsWindowVisible(child)) throw new InvalidOperationException("Native surface hidden");
        GetClientRect(child, out var rect);
        var point = new NativePoint { X = rect.Right / 2, Y = rect.Bottom / 2 };
        MapWindowPoints(child, bridge, ref point, 1);
        var region = CreateRectRgn(0, 0, 0, 0);
        try
        {
            if (GetWindowRgn(bridge, region) == 0 || PtInRegion(region, point.X, point.Y))
                throw new InvalidOperationException("Fluent composition covers the native surface");
        }
        finally { DeleteObject(region); }
    }

    [DllImport("user32.dll")] private static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
    [DllImport("user32.dll")] private static extern IntPtr GetFocus();
    [StructLayout(LayoutKind.Sequential)] private struct GuiInfo
    {
        public int Size, Flags;
        public IntPtr Active, Focus, Capture, MenuOwner, MoveSize, Caret;
        public NativeRect CaretRect;
    }
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll")] private static extern bool GetGUIThreadInfo(uint thread, ref GuiInfo info);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetText(IntPtr hwnd, uint message, IntPtr size, System.Text.StringBuilder text);

    private static void ValidateEditor(IntPtr hwnd, bool visible)
    {
        var editor = FindWindowEx(hwnd, IntPtr.Zero, "Scintilla", null);
        var bridge = FindWindowEx(hwnd, IntPtr.Zero, "Microsoft.UI.Content.DesktopChildSiteBridge", null);
        if (editor == IntPtr.Zero || bridge == IntPtr.Zero || IsWindowVisible(editor) != visible)
            throw new InvalidOperationException("Editor must be a visible/hidden sibling of a live WinUI bridge.");
        var region = CreateRectRgn(0, 0, 0, 0);
        try
        {
            var kind = GetWindowRgn(bridge, region);
            if (!visible && kind != 0) throw new InvalidOperationException("Media preview did not restore the WinUI surface.");
            if (visible)
            {
                GetClientRect(editor, out var rect);
                var point = new NativePoint { X = rect.Right / 2, Y = rect.Bottom / 2 };
                MapWindowPoints(editor, bridge, ref point, 1);
                if (kind == 0 || PtInRegion(region, point.X, point.Y) || rect.Right < 2 || rect.Bottom < 2 ||
                    SendMessage(editor, 2006 /* SCI_GETLENGTH */, IntPtr.Zero, IntPtr.Zero).ToInt64() == 0)
                    throw new InvalidOperationException("Native editor content or composition cutout is missing.");
            }
        }
        finally { DeleteObject(region); }
    }
    [StructLayout(LayoutKind.Sequential)] private struct NativePoint { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct NativeRect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr hwnd, out NativeRect rect);
    [DllImport("user32.dll")] private static extern int MapWindowPoints(IntPtr from, IntPtr to, ref NativePoint point, uint count);
    [DllImport("user32.dll")] private static extern int GetWindowRgn(IntPtr hwnd, IntPtr region);
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateRectRgn(int left, int top, int right, int bottom);
    [DllImport("gdi32.dll")] private static extern bool PtInRegion(IntPtr region, int x, int y);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string name, string? title);
}
