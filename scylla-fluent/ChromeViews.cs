using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace Scylla;

/// <summary>
/// Workbench-style chrome: File/Edit/View/Access/Help + status strip.
/// Agent menu removed — New/Stop live in the composer.
/// </summary>
internal sealed class TitleChrome : UserControl
{
    public event EventHandler? OpenFolderClicked;
    public event EventHandler? OpenFileClicked;
    public event EventHandler? SaveClicked;
    public event EventHandler? SettingsClicked;
    public event EventHandler? ExitClicked;
    public event EventHandler? ToggleFilesClicked;
    public event EventHandler? ToggleChatLogClicked;
    public event EventHandler? ToggleChatsClicked;
    public event EventHandler? ToggleFocusClicked;
    public event EventHandler? ToggleTerminalClicked;
    public event EventHandler? AccessSecurityClicked;
    public event EventHandler? AccessKeyringClicked;
    public event EventHandler<string>? ModelChanged;
    public event EventHandler<string>? ReasoningEffortChanged;
    public event EventHandler<string>? ProviderConnectRequested;

    private readonly ComboBox _model = new()
    {
        PlaceholderText = "Provider / Model",
        MinWidth = 180,
        Height = Design.RowCompact,
        VerticalAlignment = VerticalAlignment.Center,
        FontSize = 12,
        CornerRadius = Design.RadiusSm,
    };
    private readonly ComboBox _effort = new()
    {
        PlaceholderText = "Depth",
        MinWidth = 92,
        Height = Design.RowCompact,
        VerticalAlignment = VerticalAlignment.Center,
        FontSize = 12,
        CornerRadius = Design.RadiusSm,
        Visibility = Visibility.Collapsed,
    };
    private readonly MenuFlyoutItem _filesItem = new() { Text = "File Explorer" };
    private readonly MenuFlyoutItem _chatLogItem = new() { Text = "Chats" };
    private readonly MenuFlyoutItem _chatsItem = new() { Text = "Chat History" };
    private readonly MenuFlyoutItem _focusItem = new() { Text = "Focus Editor" };
    private readonly MenuFlyoutItem _termItem = new() { Text = "Terminal" };

    public TitleChrome()
    {
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(_effort, "Model reasoning depth");
        ToolTipService.SetToolTip(_effort, "Reasoning depth for the selected model");
        Background = ThemeColors.Brush(ThemeColors.Panel);
        var root = new Grid();
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var menu = BuildMenuBar();
        Grid.SetRow(menu, 0);

        _model.SelectionChanged += (_, _) =>
        {
            if (_syncingModels) return;
            if (_model.SelectedItem is ModelRow m)
            {
                if (m.Id.StartsWith("connect:")) {
                    ProviderConnectRequested?.Invoke(this, m.Id[8..]);
                    _syncingModels = true; _model.SelectedIndex = -1; _syncingModels = false;
                }
                else
                {
                    _syncingModels = true;
                    try { SetEfforts(m, "", true); }
                    finally { _syncingModels = false; }
                    ModelChanged?.Invoke(this, m.Id);
                }
            }
            else if (_model.SelectedItem is string s) ModelChanged?.Invoke(this, s);
        };
        _effort.SelectionChanged += (_, _) =>
        {
            if (_syncingModels || _effort.SelectedItem is not EffortRow effort) return;
            ReasoningEffortChanged?.Invoke(this, effort.Id);
        };

        root.Children.Add(menu);

        Content = root;
    }

    private MenuBar BuildMenuBar()
    {
        var bar = new MenuBar
        {
            Background = ThemeColors.Brush(ThemeColors.Panel),
            Padding = new Thickness(4, 0, 4, 0),
        };

        var file = new MenuBarItem { Title = "File" };
        file.Items.Add(Mk("Open File…\tCtrl+O", () => OpenFileClicked?.Invoke(this, EventArgs.Empty)));
        file.Items.Add(Mk("Open Folder…\tCtrl+Shift+O", () => OpenFolderClicked?.Invoke(this, EventArgs.Empty)));
        file.Items.Add(Mk("Save\tCtrl+S", () => SaveClicked?.Invoke(this, EventArgs.Empty)));
        file.Items.Add(new MenuFlyoutSeparator());
        file.Items.Add(Mk("Settings…", () => SettingsClicked?.Invoke(this, EventArgs.Empty)));
        file.Items.Add(new MenuFlyoutSeparator());
        file.Items.Add(Mk("Exit", () => ExitClicked?.Invoke(this, EventArgs.Empty)));

        var edit = new MenuBarItem { Title = "Edit" };
        edit.Items.Add(Mk("Undo\tCtrl+Z", null, enabled: false));
        edit.Items.Add(Mk("Redo\tCtrl+Y", null, enabled: false));
        edit.Items.Add(new MenuFlyoutSeparator());
        edit.Items.Add(Mk("Cut\tCtrl+X", null, enabled: false));
        edit.Items.Add(Mk("Copy\tCtrl+C", null, enabled: false));
        edit.Items.Add(Mk("Paste\tCtrl+V", null, enabled: false));
        edit.Items.Add(Mk("Select All\tCtrl+A", null, enabled: false));

        var view = new MenuBarItem { Title = "View" };
        _filesItem.Click += (_, _) => ToggleFilesClicked?.Invoke(this, EventArgs.Empty);
        _chatLogItem.Click += (_, _) => ToggleChatLogClicked?.Invoke(this, EventArgs.Empty);
        _chatsItem.Click += (_, _) => ToggleChatsClicked?.Invoke(this, EventArgs.Empty);
        _focusItem.Click += (_, _) => ToggleFocusClicked?.Invoke(this, EventArgs.Empty);
        _termItem.Click += (_, _) => ToggleTerminalClicked?.Invoke(this, EventArgs.Empty);
        view.Items.Add(_filesItem);
        view.Items.Add(_chatLogItem);
        view.Items.Add(_chatsItem);
        view.Items.Add(_focusItem);
        view.Items.Add(new MenuFlyoutSeparator());
        view.Items.Add(_termItem);

        var access = new MenuBarItem { Title = "Access" };
        access.Items.Add(Mk("Project Security", () => AccessSecurityClicked?.Invoke(this, EventArgs.Empty)));
        access.Items.Add(Mk("Keyring…", () => AccessKeyringClicked?.Invoke(this, EventArgs.Empty)));

        var help = new MenuBarItem { Title = "Help" };
        help.Items.Add(Mk("About Scylla", () =>
        {
            var ver = "?";
            try { ver = NativeCore.Version() ?? "?"; } catch { /* ignore */ }
            if (XamlRoot is null) return;
            var dlg = new ContentDialog
            {
                Title = "Scylla",
                Content = $"Fluent shell · core {ver}",
                CloseButtonText = "OK",
                XamlRoot = XamlRoot,
            };
            _ = dlg.ShowAsync();
        }));

        bar.Items.Add(file);
        bar.Items.Add(edit);
        bar.Items.Add(view);
        bar.Items.Add(access);
        bar.Items.Add(help);
        return bar;
    }

    private static MenuFlyoutItem Mk(string text, Action? click, bool enabled = true)
    {
        var item = new MenuFlyoutItem { Text = text, IsEnabled = enabled };
        if (click is not null) item.Click += (_, _) => click();
        return item;
    }

    private List<ProviderCard> _providers = new();
    public void SetProviders(List<ProviderCard> providers) => _providers = providers;

    private bool _syncingModels;
    public ComboBox ModelSelector => _model;
    public ComboBox EffortSelector => _effort;

    public void SetModel(string model)
    {
        SetModels(_model.Items.OfType<ModelRow>().ToList(), model, "");
    }

    public void SetModels(IReadOnlyList<ModelRow> models, string selected, string selectedEffort)
    {
        if (_model.IsDropDownOpen || _effort.IsDropDownOpen) return;
        models = models.Concat(_providers.Where(p => !p.Connected).Select(p => new ModelRow {
            Id = "connect:" + p.Id, Label = p.DisplayName + " · Sign In…", Enabled = true
        })).ToList();
        _syncingModels = true;
        try
        {
            if (!_model.Items.OfType<ModelRow>().Select(m => (m.Id, m.Label, m.Specialty, m.DefaultReasoningEffort, string.Join("|", m.ReasoningEfforts)))
                .SequenceEqual(models.Select(m => (m.Id, m.Label, m.Specialty, m.DefaultReasoningEffort, string.Join("|", m.ReasoningEfforts)))))
            {
                _model.Items.Clear();
                foreach (var m in models) _model.Items.Add(m);
                _model.DisplayMemberPath = nameof(ModelRow.OptionLabel);
            }
            _model.SelectedIndex = Enumerable.Range(0, _model.Items.Count)
                .FirstOrDefault(i => _model.Items[i] is ModelRow m && m.Id == selected, -1);
            if (_model.SelectedItem is ModelRow selectedModel) SetEfforts(selectedModel, selectedEffort, false);
            else SetEfforts(null, "", false);
        }
        finally { _syncingModels = false; }
    }


    private void SetEfforts(ModelRow? model, string selected, bool resetToMiddle)
    {
        var options = model?.ReasoningEfforts ?? new List<string>();
        if (!_effort.Items.OfType<EffortRow>().Select(e => e.Id).SequenceEqual(options))
        {
            _effort.Items.Clear();
            foreach (var option in options)
                _effort.Items.Add(new EffortRow { Id = option, Label = EffortLabel(option) });
            _effort.DisplayMemberPath = nameof(EffortRow.Label);
        }
        _effort.Visibility = options.Count == 0 ? Visibility.Collapsed : Visibility.Visible;
        if (options.Count == 0) { _effort.SelectedIndex = -1; return; }
        var wanted = selected;
        if (resetToMiddle || !options.Contains(wanted))
        {
            wanted = options.FirstOrDefault(v => v == "medium")
                ?? (options.Contains(model?.DefaultReasoningEffort ?? "") ? model!.DefaultReasoningEffort : options[(options.Count - 1) / 2]);
        }
        _effort.SelectedIndex = options.IndexOf(wanted);
    }

    private static string EffortLabel(string effort) => effort switch
    {
        "xhigh" => "Extra High",
        "high" => "High",
        "medium" => "Medium",
        "low" => "Low",
        "minimal" => "Minimal",
        "none" => "None",
        _ => effort.Length == 0 ? "" : char.ToUpperInvariant(effort[0]) + effort[1..],
    };

    public void SyncToggles(bool files, bool chatLog, bool chats, bool focus, bool terminal)
    {
        Mark(_filesItem, "File Explorer", files);
        Mark(_chatLogItem, "Chats", chatLog);
        Mark(_chatsItem, "Chat History", chats);
        Mark(_focusItem, "Focus Editor", focus);
        Mark(_termItem, "Terminal", terminal);
    }

    private static void Mark(MenuFlyoutItem item, string baseLabel, bool on) =>
        item.Text = on ? $"✓  {baseLabel}" : $"     {baseLabel}";
}

internal sealed class StatusStripView : UserControl
{
    private readonly TextBlock _msg = new()
    {
        Foreground = ThemeColors.Brush(ThemeColors.Muted),
        FontSize = 12,
        VerticalAlignment = VerticalAlignment.Center,
        TextTrimming = TextTrimming.CharacterEllipsis,
    };
    private readonly TextBlock _env = new()
    {
        Foreground = ThemeColors.Brush(ThemeColors.Amber),
        FontSize = 12,
        VerticalAlignment = VerticalAlignment.Center,
        HorizontalAlignment = HorizontalAlignment.Right,
        TextTrimming = TextTrimming.CharacterEllipsis,
    };

    public StatusStripView()
    {
        Height = 26;
        Background = ThemeColors.Brush(ThemeColors.Panel);
        var g = new Grid
        {
            Padding = new Thickness(Design.Gap, 0, Design.Gap, 0),
            ColumnSpacing = Design.Gap,
            BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle),
            BorderThickness = new Thickness(0, 1, 0, 0),
        };
        g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        g.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Grid.SetColumn(_msg, 0);
        Grid.SetColumn(_env, 1);
        g.Children.Add(_msg);
        g.Children.Add(_env);
        Content = g;
    }

    public void SetMessage(string text) => _msg.Text = text;
    public void SetEnv(string text) => _env.Text = text;
}

/// <summary>Editor panel: Terminal / Payload with ConPTY host.</summary>
internal sealed partial class BottomPanelView : UserControl
{
    // Inset the anchor itself so the native HWND and its cutout share the gutter.
    private readonly Border _termAnchor = new()
    {
        Background = ThemeColors.Brush(ThemeColors.AppBg),
        Margin = new Thickness(Design.GapSm),
    };

    private readonly TextBlock _output = new()
    {
        FontFamily = new FontFamily("Cascadia Mono"),
        FontSize = 12,
        Foreground = ThemeColors.Brush(ThemeColors.Secondary),
        TextWrapping = TextWrapping.WrapWholeWords,
        IsTextSelectionEnabled = true,
    };

    private readonly Grid _body = new();
    private readonly Dictionary<string, Button> _tabs = new();
    private string _surface = "terminal";
    private IntPtr _hwndWindow;
    private IntPtr _term;
    private string _cwd = "";
    private bool _positionQueued;
    private bool _disposed;
    private (int X, int Y, int W, int H)? _lastPosition;
    private IntPtr _terminalBridge;

    private void HideTerminalSurface()
    {
        if (_term != IntPtr.Zero) NativeCore.TerminalSetVisible(_term, 0);
        NativeSurfaceCutouts.Remove(_terminalBridge, this);
        _terminalBridge = IntPtr.Zero;
        _lastPosition = null;
    }

    [StructLayout(LayoutKind.Sequential)] private struct TerminalPoint { public int X, Y; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string name, string? title);
    [DllImport("user32.dll")]
    private static extern int MapWindowPoints(IntPtr from, IntPtr to, ref TerminalPoint point, uint count);

    public BottomPanelView()
    {
        Background = ThemeColors.Brush(ThemeColors.Panel);
        BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle);
        BorderThickness = new Thickness(0);

        var tabs = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = Design.GapSm,
            Children =
            {
                Tab("Terminal", "terminal"),
                Tab("Payload", "payload"),


            },
        };

        _body.Children.Add(BuildTerminalBody());

        _body.Children.Add(Wrap(_output));

        ShowSurface("terminal");

        var root = new Grid();
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        var header = BuildTerminalHeader(tabs);
        Grid.SetRow(header, 0);
        Grid.SetRow(_body, 1);
        root.Children.Add(header);
        root.Children.Add(_body);
        Content = root;

        _termAnchor.SizeChanged += (_, _) => QueueTerminalPosition();
        // Do not EnsureTerminal from Loaded — Relayout during Activated races the
        // XAML island. WorkbenchWindow starts ConPTY after a short deferral.
        Loaded += (_, _) => QueueTerminalPosition();
        LayoutUpdated += (_, _) =>
        {
            // Only reposition after ConPTY exists; empty-panel layout storms STOW WinUI.
            if (_term != IntPtr.Zero) QueueTerminalPosition();
        };
        Unloaded += (_, _) => { HideTerminalSurface(); _lastPosition = null; };
    }

    private FrameworkElement Tab(string label, string surface)
    {
        var b = Design.GhostButton(label, () =>
        {
            _surface = surface;
            ShowSurface(surface);
            NativeCore.PatchSettings($"{{\"panel_surface\":\"{surface}\"}}", out string _);
        });
        b.BorderThickness = new Thickness(0);
        b.Background = ThemeColors.Brush(ThemeColors.Transparent);
        _tabs[surface] = b;
        return b;
    }

    private static ScrollViewer Wrap(UIElement child) => new()
    {
        Content = child,
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        Visibility = Visibility.Collapsed,
    };

    private void ShowSurface(string surface)
    {
        foreach (var (name, tab) in _tabs)
            tab.Foreground = ThemeColors.Brush(name == surface ? ThemeColors.Amber : ThemeColors.Secondary);
        foreach (var child in _body.Children)
        {
            if (child is FrameworkElement fe) fe.Visibility = Visibility.Collapsed;
        }
        if (surface == "terminal")
        {
            _terminalBody.Visibility = Visibility.Visible;
            _termAnchor.Visibility = Visibility.Visible;
            _lastPosition = null;
            RepositionTerminal();
        }
        else
        {
            _termAnchor.Visibility = Visibility.Collapsed;
            HideTerminalSurface();
            _lastPosition = null;
            var idx = 1;
            if (_body.Children[idx] is FrameworkElement fe) fe.Visibility = Visibility.Visible;
        }
    }

    public void AttachWindow(IntPtr hwnd, string cwd)
    {
        _hwndWindow = hwnd;
        _cwd = cwd ?? "";
    }

    public void SetSurface(string surface)
    {
        _surface = surface == "payload" ? "payload" : "terminal";
        ShowSurface(_surface);
    }

    public void EnsureTerminal()
    {
        if (_disposed || _term != IntPtr.Zero || _hwndWindow == IntPtr.Zero) return;
        if (!_started)
        {
            _started = true;
            var surface = _surface;
            CreateTerminal();
            SetSurface(surface);
        }
    }

    public void SetPayload(string text)
    {
        var display = string.IsNullOrEmpty(text) ? "Agent payloads and replies will appear here." : text;
        if (_output.Text != display) _output.Text = display;
    }
    public void DisposeTerminal()
    {
        HideTerminalSurface();
        _disposed = true;
        _terminalTimer.Stop();
        foreach (var session in _terminals) NativeCore.TerminalDestroy(session.Handle);
        _terminals.Clear();
        _term = IntPtr.Zero;
    }

    private void QueueTerminalPosition()
    {
        if (_disposed || _positionQueued) return;
        _positionQueued = DispatcherQueue.TryEnqueue(() =>
        {
            _positionQueued = false;
            if (!_disposed) RepositionTerminal();
        });
    }

    private void RepositionTerminal()
    {
        if (_disposed || _term == IntPtr.Zero) return;
        if (_overlayOpen || !IsLoaded || Visibility != Visibility.Visible || _termAnchor.Visibility != Visibility.Visible)
        {
            HideTerminalSurface();
            _lastPosition = null;
            return;
        }
        try
        {
            var topLeft = _termAnchor.TransformToVisual(null).TransformPoint(new Point(0, 0));
            var w = (int)_termAnchor.ActualWidth;
            var h = (int)_termAnchor.ActualHeight;
            if (w < 2 || h < 2) { HideTerminalSurface(); _lastPosition = null; return; }
            var scale = _termAnchor.XamlRoot?.RasterizationScale ?? 1.0;
            var px = (int)Math.Round(w * scale);
            var py = (int)Math.Round(h * scale);
            var bridge = FindWindowEx(_hwndWindow, IntPtr.Zero, "Microsoft.UI.Content.DesktopChildSiteBridge", null);
            if (bridge == IntPtr.Zero) { HideTerminalSurface(); return; }
            var origin = new TerminalPoint { X = (int)Math.Round(topLeft.X * scale), Y = (int)Math.Round(topLeft.Y * scale) };
            // SetWindowRgn on the XAML DesktopChildSiteBridge during early show has
            // STOW-crashed Fluent on load (0xc000027b). Move the HWND first; cut out later.
            MapWindowPoints(bridge, _hwndWindow, ref origin, 1);
            var position = (origin.X, origin.Y, px, py);
            if (_lastPosition == position) return;
            NativeCore.TerminalMove(_term,
                origin.X,
                origin.Y,
                px, py);
            NativeCore.TerminalResizePixels(_term, px, py);
            if (_lastPosition is null) NativeCore.TerminalSetVisible(_term, 1);
            _lastPosition = position;
            if (_terminalBridge != bridge) NativeSurfaceCutouts.Remove(_terminalBridge, this);
            _terminalBridge = bridge;
            try { NativeSurfaceCutouts.Update(bridge, this, (int)Math.Round(topLeft.X * scale), (int)Math.Round(topLeft.Y * scale), px, py); }
            catch { /* cutout is best-effort */ }
        }
        catch { /* layout race */ }
    }
}
