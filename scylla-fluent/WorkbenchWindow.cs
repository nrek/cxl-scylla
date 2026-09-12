using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;
using Windows.Storage.Pickers;
using Windows.UI;
using WinRT.Interop;

namespace Scylla;

internal static class ThemeColors
{
    public static readonly Color AppBg = ColorHelper.FromArgb(255, 0x0C, 0x0F, 0x12);
    public static readonly Color Panel = ColorHelper.FromArgb(255, 0x10, 0x13, 0x17);
    public static readonly Color Surface = ColorHelper.FromArgb(255, 0x14, 0x18, 0x1D);
    public static readonly Color Input = ColorHelper.FromArgb(255, 0x0F, 0x13, 0x17);
    public static readonly Color Elevated = ColorHelper.FromArgb(255, 0x1A, 0x1F, 0x25);
    public static readonly Color Border = ColorHelper.FromArgb(255, 0x30, 0x37, 0x40);
    // Hairline used for cards and dividers — quieter than Border so boxes don't shout.
    public static readonly Color BorderSubtle = ColorHelper.FromArgb(255, 0x23, 0x29, 0x30);
    public static readonly Color Text = ColorHelper.FromArgb(255, 0xD7, 0xDC, 0xE2);
    public static readonly Color Secondary = ColorHelper.FromArgb(255, 0xA2, 0xAB, 0xB5);
    public static readonly Color Muted = ColorHelper.FromArgb(255, 0x72, 0x7C, 0x87);
    public static readonly Color Transparent = ColorHelper.FromArgb(0, 0, 0, 0);
    // Accent #e69405 — use sparingly (send action, active nav, status env chip).
    public static readonly Color Amber = ColorHelper.FromArgb(255, 230, 148, 5);
    public static readonly Color AmberDim = ColorHelper.FromArgb(64, 230, 148, 5);
    public static readonly Color OnAmber = ColorHelper.FromArgb(255, 0x17, 0x13, 0x0B);

    public static SolidColorBrush Brush(Color c) => new(c);
}

public sealed class WorkbenchWindow : Window
{
    private readonly bool _validation;
    private readonly Grid _root = new();
    private readonly TitleChrome _chrome;
    private readonly FilesPane _files;
    private readonly EditorPane _editor;
    private readonly AgentPane _agent;
    private readonly HistoryPane _history;
    private readonly BottomPanelView _bottom;
    private readonly StatusStripView _status;
    private readonly SplitterBar _splitFiles = new();
    private readonly SplitterBar _splitEditor = new();
    private readonly SplitterBar _splitAgent = new();
    private readonly SplitterBar _splitTerm = new(horizontal: true);
    private readonly Grid _columns = new();
    private readonly Grid _centerStack = new();

    private IntPtr _session = IntPtr.Zero;
    private WindowInteractionMonitor? _windowInteraction;
    private readonly DispatcherTimer _pollTimer = new() { Interval = TimeSpan.FromMilliseconds(250) };
    private SettingsSnapshot _settings = new();
    private int _filesMode;
    private int _historyMode;
    private int _agentMode;
    private bool _focusEditor;
    private int _narrowTab;
    private int _filesW = 300;
    private int _agentW = 650;
    private int _historyW = 300;
    private int _terminalH = 220;
    private bool _terminalVisible;
    private bool _terminalArmed;
    private int _drag; // 1 files|ed 2 ed|agent 3 agent|hist 4 term
    private double _dragStartX;
    private double _dragStartY;
    private int _dragStartW;
    private IntPtr _hwnd;
    private bool _closed;
    private bool _layoutQueued;

    public WorkbenchWindow(bool validation = false)
    {
        _validation = validation;
        App.Log("WorkbenchWindow ctor enter");
        Title = validation ? "Scylla — isolated UI stress test" : "Scylla";
        ExtendsContentIntoTitleBar = false;

        App.Log("new FilesPane");
        _files = new FilesPane();
        App.Log("new HistoryPane");
        _history = new HistoryPane();
        App.Log("new BottomPanelView");
        _bottom = new BottomPanelView();
        App.Log("new StatusStripView");
        _status = new StatusStripView();
        App.Log("new EditorPane");
        _editor = new EditorPane(this);
        App.Log("new AgentPane");
        _agent = new AgentPane(this);
        _bottom.ReviewOutputRequested += (_, text) => _agent.AddReviewDraft(text);
        App.Log("new TitleChrome");
        _chrome = new TitleChrome();

        App.Log("BuildChrome");
        BuildChrome();
        Content = _root;
        App.Log("Content set");

        Activated += (_, _) =>
        {
            try
            {
                App.Log("Activated → EnsureHwnd");
                EnsureHwnd();
            }
            catch (Exception ex)
            {
                App.Log($"EnsureHwnd: {ex}");
            }
        };
        Closed += OnClosed;

        App.Log("wire events");
        _chrome.OpenFolderClicked += async (_, _) => await OpenFolderAsync();
        _chrome.OpenFileClicked += async (_, _) => await OpenFileAsync();
        _chrome.ToggleFilesClicked += (_, _) => { CycleMode(ref _filesMode); Relayout(); PersistLayout(); };
        _chrome.ToggleChatLogClicked += (_, _) =>
        {
            // Flip between force-on and force-off (default 0 = auto/show).
            _agentMode = _agentMode == 2 ? 1 : 2;
            Relayout();
            PersistLayout();
        };
        _chrome.ToggleChatsClicked += (_, _) => { CycleMode(ref _historyMode); Relayout(); PersistLayout(); };
        _chrome.ToggleFocusClicked += (_, _) => { _focusEditor = !_focusEditor; Relayout(); PersistLayout(); };
        _chrome.ToggleTerminalClicked += (_, _) =>
        {
            _terminalVisible = !_terminalVisible;
            if (!_terminalVisible) _terminalArmed = false;
            Relayout();
            if (_terminalVisible)
            {
                try
                {
                    EnsureHwnd();
                    _bottom.AttachWindow(_hwnd, _settings.ProjectFolder);
                    ArmTerminalPanel();
                    _bottom.EnsureTerminal();
                }
                catch (Exception ex)
                {
                    App.Log($"EnsureTerminal: {ex}");
                    _status.SetMessage("Terminal failed to start");
                }
            }
            PersistLayout();
        };
        _chrome.SettingsClicked += (_, _) => _editor.ShowSettings();
        _chrome.SaveClicked += (_, _) => _editor.SaveActive();
        _chrome.ExitClicked += (_, _) => Close();
        _chrome.AccessSecurityClicked += (_, _) => _editor.ShowSettings("Security");
        _chrome.AccessKeyringClicked += (_, _) => _editor.ShowSettings("Security");
        _chrome.ModelChanged += (_, id) =>
        {
            if (!NativeCore.SessionProviderAction(_session, _settings.DefaultProvider, "select_model", id, out var error))
                _status.SetMessage(error);
            _settings = SettingsSnapshot.Parse(NativeCore.SettingsJson());
        };
        _chrome.ReasoningEffortChanged += (_, effort) =>
        {
            if (!NativeCore.SessionProviderAction(_session, _settings.DefaultProvider, "select_effort", effort, out var error))
                _status.SetMessage(error);
            _settings = SettingsSnapshot.Parse(NativeCore.SettingsJson());
        };
        _chrome.ProviderConnectRequested += (_, id) =>
        {
            if (id == "openai") OnAccountClicked();
            else _editor.ConnectProvider(id);
        };
        _history.DeleteThreadRequested += (_, id) =>
        {
            if (_session == IntPtr.Zero) return;
            if (!NativeCore.SessionDeleteThread(_session, id, out var error)) _status.SetMessage(error);
            PollOnce();
        };

        _files.FileActivated += (_, path) => _editor.OpenPath(path);
        _files.PathChanged += (oldPath, newPath) => _editor.UpdateFilePath(oldPath, newPath);
        _files.ManageKnowledgeClicked += async (_, _) => await ManageKnowledgeAsync();
        _files.ManageFilesClicked += async (_, _) => await ManageFilesAsync();
        _files.StrataClicked += (_, _) => _editor.ShowStrataApp();
        _history.ThreadSelected += (_, id) =>
        {
            if (_session == IntPtr.Zero) return;
            NativeCore.SessionOpenThread(_session, id, out string _);
        };
        _agent.SetModelSelectors(_chrome.ModelSelector, _chrome.EffortSelector);
        _agent.OpenPlanRequested += (_, path) => _editor.OpenPath(path);
        _agent.OpenLinkRequested += (_, target) => _editor.OpenMarkdownLink(target, _settings.ProjectFolder);
        _agent.SendRequested += (_, request) => request.Accepted = SendUser(request.Payload, request.Mode);
        _history.NewChatRequested += (_, _) =>
        {
            if (_session == IntPtr.Zero) return;
            NativeCore.SessionNewChat(_session, out string _);
        };
        _agent.StopRequested += (_, _) =>
        {
            if (_session == IntPtr.Zero) return;
            NativeCore.SessionCancel(_session);
        };
        _agent.OpenThreadRequested += (_, id) =>
        {
            if (_session == IntPtr.Zero) return;
            NativeCore.SessionOpenThread(_session, id, out string _);
        };
        _editor.BackFromSettings += (_, _) => Relayout();

        _splitFiles.PointerPressed += (s, e) => BeginDrag(1, e);
        _splitEditor.PointerPressed += (s, e) => BeginDrag(2, e);
        _splitAgent.PointerPressed += (s, e) => BeginDrag(3, e);
        _splitTerm.PointerPressed += (s, e) => BeginDrag(4, e);
        _root.PointerMoved += OnDragMove;
        _root.PointerReleased += (_, _) => EndDrag();
        _root.PointerCaptureLost += (_, _) => EndDrag();

        App.Log("LoadSettings");
        LoadSettings();
        App.Log("EnsureSession");
        EnsureSession();
        _pollTimer.Tick += (_, _) =>
        {
            try { PollOnce(); }
            catch (Exception ex) { App.Log($"PollOnce: {ex}"); }
        };
        // Defer first poll/layout until after Activate — ctor-time Relayout/PollOnce can
        // stow XAML exceptions before the island HWND exists.
        Activated += (_, _) =>
        {
            if (_validation) { Relayout(); return; }
            if (_pollTimer.IsEnabled) return;
            App.Log("post-activate layout/poll");
            try
            {
                App.Log("Relayout");
                Relayout();
                ScheduleSizedRelayout();
                App.Log("LoadProject");
                _files.LoadProject(_settings.ProjectFolder);
                StartRuntime();
                App.Log("PollOnce");
                PollOnce();
                App.Log("poll timer start");
                _pollTimer.Start();
                var armSize = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
                armSize.Tick += OnArmSizeChangedTick;
                armSize.Start();
                _armSizeTimer = armSize;
                App.Log($"terminal arm schedule visible={_terminalVisible} (skipped auto-start - load STOW)");
                App.Log("post-activate done");
            }
            catch (Exception ex)
            {
                App.Log($"post-activate: {ex}");
            }
        };
        App.Log("WorkbenchWindow ctor exit");
    }

    private void BuildChrome()
    {
        _root.Background = ThemeColors.Brush(ThemeColors.AppBg);
        _root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // toolbar
        _root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) }); // work
        _root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // status

        Grid.SetRow(_chrome, 0);
        _root.Children.Add(_chrome);

        _centerStack.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        _centerStack.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // term split
        _centerStack.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // bottom
        Grid.SetRow(_editor, 0);
        Grid.SetRow(_splitTerm, 1);
        Grid.SetRow(_bottom, 2);
        _centerStack.Children.Add(_editor);
        _centerStack.Children.Add(_splitTerm);
        _centerStack.Children.Add(_bottom);

        Grid.SetRow(_columns, 1);
        _root.Children.Add(_columns);

        Grid.SetRow(_status, 2);
        _root.Children.Add(_status);
    }

    private void EnsureHwnd()
    {
        if (_hwnd != IntPtr.Zero) return;
        App.Log("GetWindowHandle");
        _hwnd = WindowNative.GetWindowHandle(this);
        // Subclassing the top-level HWND + XAML bridge during early activation has
        // correlated with load-time STOW (0xc000027b). DeferPresentation stays false.
        // _windowInteraction = new WindowInteractionMonitor(_hwnd);
        // WindowLifetimeDiagnostics.WatchBridge(_hwnd);
        App.Log($"hwnd={_hwnd}");
        try
        {
            Win32Hwnd.ApplyDarkTitleBar(_hwnd);
            App.Log("dark title ok");
        }
        catch (Exception ex)
        {
            App.Log($"dark title: {ex.Message}");
        }
        try
        {
            _editor.AttachWindowHwnd(_hwnd);
            App.Log("editor attach ok");
        }
        catch (Exception ex)
        {
            App.Log($"editor attach: {ex}");
        }
        try
        {
            _bottom.AttachWindow(_hwnd, _settings.ProjectFolder);
            App.Log("bottom attach ok");
        }
        catch (Exception ex)
        {
            App.Log($"bottom attach: {ex}");
        }
        // AppWindow.Resize during first Activated has been observed to STOW-crash
        // (0xC000027B / combase 80070578). Use default size; operator can resize.
        App.Log("EnsureHwnd done");
    }

    public async Task<string?> PickFolderAsync()
    {
        EnsureHwnd();
        var picker = new FolderPicker();
        InitializeWithWindow.Initialize(picker, _hwnd);
        picker.FileTypeFilter.Add("*");
        var folder = await picker.PickSingleFolderAsync();
        return folder?.Path;
    }

    public void RefreshKnowledgeAndRoots()
    {
        _files.ReloadRoots();
        _files.LoadProject(_settings.ProjectFolder);
        _files.RefreshKnowledge();
    }

    private void LoadSettings()
    {
        if (_validation) return;
        try
        {
            _settings = SettingsSnapshot.Parse(NativeCore.SettingsJson());
        }
        catch
        {
            _settings = new SettingsSnapshot();
        }
        _filesW = _settings.FilesW;
        _agentW = _settings.AgentW;
        _historyW = _settings.HistoryW;
        _filesMode = _settings.FilesMode;
        _historyMode = _settings.HistoryMode;
        _agentMode = _settings.AgentMode;
        _focusEditor = _settings.FocusEditor;
        _terminalH = Math.Max(140, _settings.TerminalH);
        _terminalVisible = _settings.TerminalVisible;
        _bottom.SetSurface(_settings.PanelSurface);
        _chrome.SetModel(_settings.SelectedModel);
    }

    private void PersistLayout()
    {
        if (_validation) return;
        var json =
            $"{{\"files_w\":{_filesW},\"agent_w\":{_agentW},\"history_w\":{_historyW}," +
            $"\"files_mode\":{_filesMode},\"history_mode\":{_historyMode},\"agent_mode\":{_agentMode}," +
            $"\"focus_editor\":{(_focusEditor ? "true" : "false")}," +
            $"\"terminal_h\":{_terminalH},\"terminal_visible\":{(_terminalVisible ? "true" : "false")}}}";
        NativeCore.PatchSettings(json, out string _);
    }

    private static void CycleMode(ref int mode)
    {
        // 0 auto -> 1 on -> 2 off -> 0
        mode = mode switch { 0 => 1, 1 => 2, _ => 0 };
    }

    private void OnRootSizeChanged(object sender, SizeChangedEventArgs e) => QueueLayout();

    private void QueueLayout()
    {
        if (_closed || _layoutQueued) return;
        _layoutQueued = DispatcherQueue.TryEnqueue(() =>
        {
            _layoutQueued = false;
            if (!_closed) Relayout();
        });
    }

    /// <summary>
    /// First Activated Relayout often sees ActualWidth 0 (or a pre-measure stub).
    /// Pixel-sized columns then sit at a 1440 fallback until a splitter drag
    /// forces Relayout — panes look jammed/overlapping until that click.
    /// Prefer the live tree; fall back to AppWindow client DIPs.
    /// </summary>
    private int ClientWidthDip()
    {
        var actual = (int)Math.Round(_root.ActualWidth);
        if (actual >= 200) return actual;
        try
        {
            var scale = _root.XamlRoot?.RasterizationScale ?? 1.0;
            if (scale < 0.25) scale = 1.0;
            var px = AppWindow?.ClientSize.Width ?? 0;
            if (px <= 0) px = AppWindow?.Size.Width ?? 0;
            var dip = (int)Math.Round(px / scale);
            if (dip >= 200) return dip;
        }
        catch
        {
            // keep ActualWidth / fallback
        }
        return actual >= 2 ? actual : 1440;
    }

    private void Relayout()
    {
        if (_closed) return;
        var widthDip = ClientWidthDip();
        var layout = PaneLayout.Compute(widthDip, _filesW, _agentW, _historyW, _focusEditor, _filesMode,
            _historyMode, _agentMode, _narrowTab);
        if (!layout.NarrowTabs) _narrowTab = 0;

        var col = 0;
        void AddPane(FrameworkElement pane, int width, bool show)
        {
            if (_columns.ColumnDefinitions.Count <= col)
                _columns.ColumnDefinitions.Add(new ColumnDefinition());
            _columns.ColumnDefinitions[col].Width = new GridLength(show ? Math.Max(0, width) : 0);
            Grid.SetColumn(pane, col++);
            pane.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
            if (!_columns.Children.Contains(pane)) _columns.Children.Add(pane);
        }
        void AddSplit(FrameworkElement split, bool afterShown)
        {
            AddPane(split, layout.Splitter, afterShown);
        }

        AddPane(_files, layout.Files, layout.ShowFiles);
        AddSplit(_splitFiles, layout.ShowFiles && (layout.ShowEditor || layout.ShowAgent || layout.ShowHistory));
        AddPane(_centerStack, layout.Editor, layout.ShowEditor);
        _editor.Visibility = layout.ShowEditor && !_managingFiles && !_managingKnowledge
            ? Visibility.Visible : Visibility.Collapsed;
        AddSplit(_splitEditor, layout.ShowEditor && (layout.ShowAgent || layout.ShowHistory));
        AddPane(_agent, layout.Agent, layout.ShowAgent);
        AddSplit(_splitAgent, layout.ShowAgent && layout.ShowHistory);
        AddPane(_history, layout.History, layout.ShowHistory);

        // Showing the ConPTY bottom host during the first Activate Relayout STOWs WinUI
        // (0xc000027b / E_POINTER). Keep it collapsed until ArmTerminalPanel() runs.
        var showTerm = _terminalVisible && _terminalArmed && layout.ShowEditor;
        _bottom.Visibility = showTerm ? Visibility.Visible : Visibility.Collapsed;
        _splitTerm.Visibility = showTerm ? Visibility.Visible : Visibility.Collapsed;
        _bottom.Height = showTerm ? Math.Min(_terminalH, Math.Max(80, _columns.ActualHeight - 160)) : 0;

        _chrome.SyncToggles(layout.ShowFiles, layout.ShowAgent, layout.ShowHistory, _focusEditor, _terminalVisible && _terminalArmed);
        _columns.InvalidateMeasure();
        _columns.InvalidateArrange();
        // Skip OnHostLayoutChanged on the Activate path — empty-editor HideScintilla /
        // cutout churn during first Relayout STOWs WinUI. OpenPath still repositions.
    }

    private DispatcherTimer? _deferredTerminalTimer;
    private DispatcherTimer? _armSizeTimer;
    private DispatcherTimer? _sizedRelayoutTimer;

    private void OnArmSizeChangedTick(object? sender, object e)
    {
        _armSizeTimer?.Stop();
        _armSizeTimer = null;
        if (_closed) return;
        _root.SizeChanged -= OnRootSizeChanged;
        _root.SizeChanged += OnRootSizeChanged;
        App.Log("SizeChanged Relayout armed");
        // SizeChanged may never fire again if the window already finished sizing
        // during the 2s deferral — apply the real width now.
        QueueLayout();
    }

    private void ScheduleSizedRelayout()
    {
        _sizedRelayoutTimer?.Stop();
        var follow = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(120) };
        follow.Tick += (_, _) =>
        {
            follow.Stop();
            if (ReferenceEquals(_sizedRelayoutTimer, follow)) _sizedRelayoutTimer = null;
            if (_closed) return;
            Relayout();
            App.Log($"sized Relayout w={ClientWidthDip()}");
        };
        _sizedRelayoutTimer = follow;
        follow.Start();
    }

    private void OnDeferredTerminalTick(object? sender, object e)
    {
        _deferredTerminalTimer?.Stop();
        _deferredTerminalTimer = null;
        if (_closed || !_terminalVisible) return;
        try
        {
            EnsureHwnd();
            _bottom.AttachWindow(_hwnd, _settings.ProjectFolder);
            App.Log("deferred: create ConPTY before reveal");
            _bottom.EnsureTerminal();
            App.Log("deferred EnsureTerminal ok");
            // Keep the bottom host collapsed on load — revealing it (Relayout showTerm)
            // STOWs WinUI. Operator can toggle Terminal after the window is stable.
            App.Log("deferred: leaving terminal host collapsed until View toggle");
        }
        catch (Exception ex)
        {
            App.Log($"deferred EnsureTerminal: {ex}");
        }
    }

    private void ArmTerminalPanel()
    {
        if (_terminalArmed || !_terminalVisible || _closed) return;
        _terminalArmed = true;
        Relayout();
    }

    private void BeginDrag(int which, PointerRoutedEventArgs e)
    {
        _drag = which;
        var pt = e.GetCurrentPoint(_root);
        _dragStartX = pt.Position.X;
        _dragStartY = pt.Position.Y;
        _dragStartW = which switch
        {
            1 => _filesW,
            2 => _agentW,
            3 => _historyW,
            _ => _terminalH,
        };
        _root.CapturePointer(e.Pointer);
    }

    private void OnDragMove(object sender, PointerRoutedEventArgs e)
    {
        if (_drag == 0) return;
        var pt = e.GetCurrentPoint(_root);
        if (_drag == 4)
        {
            var dy = _dragStartY - pt.Position.Y;
            _terminalH = Math.Max(140, _dragStartW + (int)dy);
            Relayout();
            return;
        }
        var dx = pt.Position.X - _dragStartX;
        if (_drag == 1) _filesW = Math.Clamp(_dragStartW + (int)dx, 180, 600);
        else if (_drag == 2) _agentW = Math.Max(300, _dragStartW - (int)dx);
        else if (_drag == 3) _historyW = Math.Clamp(_dragStartW - (int)dx, 180, 600);
        Relayout();
    }

    private void EndDrag()
    {
        if (_drag == 0) return;
        _drag = 0;
        PersistLayout();
    }

    private void EnsureSession()
    {
        if (_validation) return;
        if (_session != IntPtr.Zero) return;
        try
        {
            _session = NativeCore.SessionCreate();
            _editor.BindSession(_session);
        }
        catch (DllNotFoundException)
        {
            _status.SetMessage("scylla-core.dll missing beside scylla.exe");
        }
    }

    /// <summary>Start Codex runtime if needed. Required before ChatGPT sign-in.</summary>
    public bool EnsureRuntimeLive(out string error)
    {
        error = "";
        EnsureSession();
        if (_session == IntPtr.Zero)
        {
            error = "No session";
            return false;
        }
        try
        {
            var snap = SessionSnapshot.Parse(NativeCore.SessionPoll(_session));
            if (snap?.RuntimeLive == true) return true;
        }
        catch { /* fall through to start */ }
        return NativeCore.SessionStart(_session, out error);
    }

    private void OnAccountClicked()
    {
        EnsureSession();
        // Signed-in chip opens Providers; signed-out starts ChatGPT login.
        var snap = SessionSnapshot.Parse(_session == IntPtr.Zero ? null : NativeCore.SessionPoll(_session));
        if (snap?.AccountLoaded != true)
        {
            if (!EnsureRuntimeLive(out var restoreError)) _status.SetMessage(restoreError);
            else _status.SetMessage("Checking saved ChatGPT sign-in…");
            return;
        }
        if (snap?.AccountSignedIn == true)
        {
            _editor.ShowSettings("Providers");
            return;
        }
        if (!EnsureRuntimeLive(out var err))
        {
            _status.SetMessage(string.IsNullOrWhiteSpace(err) ? "start failed" : err);
            _editor.ShowSettings("Providers");
            return;
        }
        if (!NativeCore.SessionProviderAction(_session, "openai", "sign_in", null, out var actionErr))
        {
            _status.SetMessage(string.IsNullOrWhiteSpace(actionErr) ? "sign-in failed" : actionErr);
            _editor.ShowSettings("Providers");
            return;
        }
        _status.SetMessage("Complete ChatGPT sign-in in the browser…");
        _editor.ShowSettings("Providers");
    }

    private void StartRuntime()
    {
        if (!EnsureRuntimeLive(out var err))
            _status.SetMessage(string.IsNullOrWhiteSpace(err) ? "start failed" : err);
    }

    private bool SendUser(string text, string mode = "Execute")
    {
        EnsureSession();
        if (_session == IntPtr.Zero || string.IsNullOrWhiteSpace(text)) return false;
        if (!EnsureRuntimeLive(out var startError))
        {
            _status.SetMessage(string.IsNullOrWhiteSpace(startError) ? "start failed" : startError);
            return false;
        }
        if (!NativeCore.SessionSendUserMode(_session, text, mode.ToLowerInvariant(), out var err))
        {
            _status.SetMessage(string.IsNullOrWhiteSpace(err) ? "send failed" : err);
            return false;
        }
        return true;
    }

    private DateTime _nextComposerProviderRefresh;
    internal int StressMessageRenderCount => _agent.MessageRenderCount;
    internal void StressOpenFile(string path) => _editor.OpenPath(path);
    internal void StressTerminal(string surface)
    {
        _terminalVisible = true;
        Relayout();
        EnsureHwnd();
        _bottom.AttachWindow(_hwnd, AppContext.BaseDirectory);
        _bottom.EnsureTerminal();
        _bottom.SetSurface(surface);
    }
    internal void StressSnapshot(SessionSnapshot snapshot)
    {
        if (_windowInteraction?.DeferPresentation == true) return;
        _agent.ApplySnapshot(snapshot);
        _history.ApplySnapshot(snapshot);
    }
    private string _lastPlanPath = "";
    private void PollOnce()
    {
        if (_session == IntPtr.Zero) return;
        try
        {
            var snap = SessionSnapshot.Parse(NativeCore.SessionPoll(_session));
            if (snap is null) return;
            // Continue draining the runtime, but publish its latest state after native
            // move/size/minimize transitions finish instead of rebuilding mid-message.
            if (_windowInteraction?.DeferPresentation == true || _drag != 0) return;
            _bottom.SetPayload(snap.PayloadLog);
            _agent.ApplySnapshot(snap, _settings.VerboseAgentProgress);
            _history.ApplySnapshot(snap);
            if (snap.LastPlanPath != _lastPlanPath)
            {
                _lastPlanPath = snap.LastPlanPath;
                _files.RefreshKnowledge();
            }
            if (DateTime.UtcNow >= _nextComposerProviderRefresh) {
                _chrome.SetProviders(ProviderCard.ParseList(NativeCore.SessionProvidersJson(_session))
                    .Where(p => p.Id != "openai" || snap.AccountLoaded).ToList());
                _nextComposerProviderRefresh = DateTime.UtcNow.AddSeconds(2);
                _settings = SettingsSnapshot.Parse(NativeCore.SettingsJson());
                _editor.RefreshProviderCatalog();
            }
            var msg = $"{snap.State} · {snap.Status}";
            if (!string.IsNullOrWhiteSpace(snap.LastError)) msg += $" · {snap.LastError}";
            _status.SetMessage(msg);
            _status.SetEnv(string.IsNullOrWhiteSpace(snap.GrantLabel) ? snap.ProjectFolder : snap.GrantLabel);
            _chrome.SetModels(snap.Models, snap.SelectedModel, snap.ReasoningEffort);
        }
        catch (Exception ex)
        {
            _status.SetMessage(ex.Message);
        }
    }

    private bool _managingFiles;
    private bool _managingKnowledge;
    private async Task ManageFilesAsync()
    {
        if (_managingFiles) return;
        _managingFiles = true;
        // Native editor windows otherwise paint over XAML dialogs.
        _editor.Visibility = Visibility.Collapsed;
        _editor.OnHostLayoutChanged();
        try
        {
            var prefs = BrowserPreferences.Load();
            var paths = ProjectRootRow.Parse(NativeCore.ProjectRootsJson()).Select(r => r.Path)
                .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            var list = new StackPanel { Spacing = 8 };
            var checks = new Dictionary<string, CheckBox>(StringComparer.OrdinalIgnoreCase);
            void AddRow(string path)
            {
                if (checks.ContainsKey(path)) { checks[path].IsChecked = true; return; }
                var check = new CheckBox { Content = path, IsChecked = true };
                checks[path] = check;
                list.Children.Add(check);
            }
            foreach (var path in paths) AddRow(path);
            var error = Design.Caption("");
            var add = Design.GhostButton("Add Folders…", () =>
            {
                try
                {
                    EnsureHwnd();
                    foreach (var path in NativeCore.PickFolders(_hwnd))
                    {
                        AddRow(path);
                        checks[path].IsChecked = true;
                    }
                }
                catch (Exception ex) { error.Text = ex.Message; }
            });
            var dialog = new ContentDialog
            {
                Title = "Manage Files · Project folders", XamlRoot = _root.XamlRoot,
                PrimaryButtonText = "Apply", CloseButtonText = "Cancel",
                Content = new StackPanel { Spacing = 12, Children = {
                    Design.Caption("Choose open project folders for Files, chat history, and agent context."),
                    new ScrollViewer { Content = list, MaxHeight = 360 }, add, error } },
            };
            dialog.PrimaryButtonClick += (_, e) =>
            {
                try
                {
                    EnsureSession();
                    var selected = checks.Where(c => c.Value.IsChecked == true).Select(c => c.Key).ToList();
                    if (!NativeCore.SessionSetProjectRoots(_session, selected, out var rootError))
                        throw new InvalidOperationException(rootError);
                    _settings = SettingsSnapshot.Parse(NativeCore.SettingsJson());
                    prefs.HiddenFolders.Clear();
                    prefs.AddedFolders.Clear();
                    prefs.Save();
                }
                catch (Exception ex) { error.Text = ex.Message; e.Cancel = true; }
            };
            if (await dialog.ShowAsync() == ContentDialogResult.Primary) _files.LoadProject(_settings.ProjectFolder);
        }
        catch (Exception ex) { _status.SetMessage(ex.Message); }
        finally { _managingFiles = false; Relayout(); }
    }

    private async Task ManageKnowledgeAsync()
    {
        if (_managingKnowledge) return;
        _managingKnowledge = true;
        _editor.Visibility = Visibility.Collapsed;
        _editor.OnHostLayoutChanged();
        try
        {
            var prefs = BrowserPreferences.Load();
            var existing = KnowledgeRow.ParseList(NativeCore.KnowledgeJson());
            var list = new StackPanel { Spacing = 8 };
            var rows = new Dictionary<string, (CheckBox Check, ToggleSwitch Agent, string Id)>(StringComparer.OrdinalIgnoreCase);
            var pendingAdds = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            void AddRow(string path, string id, bool agentAvailable, bool visible)
            {
                if (string.IsNullOrWhiteSpace(path)) return;
                if (rows.ContainsKey(path))
                {
                    rows[path].Check.IsChecked = true;
                    return;
                }
                var check = new CheckBox
                {
                    Content = path,
                    IsChecked = visible,
                    VerticalAlignment = VerticalAlignment.Center,
                };
                var agent = new ToggleSwitch
                {
                    IsOn = agentAvailable,
                    OnContent = "Agent",
                    OffContent = "UI only",
                    MinWidth = 0,
                };
                var row = new Grid { ColumnSpacing = Design.Gap };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                Grid.SetColumn(agent, 1);
                row.Children.Add(check);
                row.Children.Add(agent);
                rows[path] = (check, agent, id);
                list.Children.Add(row);
            }

            foreach (var k in existing)
            {
                AddRow(
                    k.Path,
                    k.Id,
                    k.AgentAvailable,
                    !prefs.HiddenKnowledgePaths.Contains(k.Path, StringComparer.OrdinalIgnoreCase));
            }

            var error = Design.Caption("");
            var add = Design.GhostButton("Add Folders…", () =>
            {
                try
                {
                    EnsureHwnd();
                    foreach (var path in NativeCore.PickFolders(_hwnd))
                    {
                        AddRow(path, "", agentAvailable: true, visible: true);
                        rows[path].Check.IsChecked = true;
                        if (string.IsNullOrWhiteSpace(rows[path].Id)) pendingAdds.Add(path);
                    }
                }
                catch (Exception ex) { error.Text = ex.Message; }
            });
            var dialog = new ContentDialog
            {
                Title = "Manage Knowledge · Sources",
                XamlRoot = _root.XamlRoot,
                PrimaryButtonText = "Apply",
                CloseButtonText = "Cancel",
                Content = new StackPanel
                {
                    Spacing = 12,
                    Children =
                    {
                        Design.Caption("Choose which folders appear in Knowledge. Sources in Scylla are always enabled."),
                        new ScrollViewer { Content = list, MaxHeight = 360 },
                        add,
                        error,
                    },
                },
            };
            dialog.PrimaryButtonClick += (_, e) =>
            {
                try
                {
                    foreach (var path in pendingAdds)
                    {
                        if (!rows.TryGetValue(path, out var row) || row.Check.IsChecked != true) continue;
                        if (!NativeCore.KnowledgeAdd("", path, out var addErr))
                            throw new InvalidOperationException(string.IsNullOrWhiteSpace(addErr) ? "Add failed" : addErr);
                    }

                    var after = KnowledgeRow.ParseList(NativeCore.KnowledgeJson())
                        .ToDictionary(k => k.Path, k => k, StringComparer.OrdinalIgnoreCase);
                    foreach (var (path, row) in rows)
                    {
                        if (!after.TryGetValue(path, out var k)) continue;
                        if (!NativeCore.KnowledgeSetFlags(k.Id, enabled: true, agentAvailable: row.Agent.IsOn, out var flagErr))
                            throw new InvalidOperationException(string.IsNullOrWhiteSpace(flagErr) ? "Update failed" : flagErr);
                    }

                    prefs.HiddenKnowledgePaths = rows
                        .Where(r => r.Value.Check.IsChecked != true)
                        .Select(r => r.Key)
                        .ToList();
                    prefs.Save();
                }
                catch (Exception ex) { error.Text = ex.Message; e.Cancel = true; }
            };
            if (await dialog.ShowAsync() == ContentDialogResult.Primary)
                _files.RefreshKnowledge();
        }
        catch (Exception ex) { _status.SetMessage(ex.Message); }
        finally { _managingKnowledge = false; Relayout(); }
    }

    private async Task OpenFolderAsync()
    {
        EnsureHwnd();
        var picker = new FolderPicker();
        InitializeWithWindow.Initialize(picker, _hwnd);
        picker.FileTypeFilter.Add("*");
        var folder = await picker.PickSingleFolderAsync();
        if (folder is null) return;
        EnsureSession();
        if (!NativeCore.SessionSetProjectRoots(_session, new[] { folder.Path }, out var err))
        {
            _status.SetMessage(err);
            return;
        }
        _settings = SettingsSnapshot.Parse(NativeCore.SettingsJson());
        _files.LoadProject(folder.Path);
        if (_filesMode == 2) _filesMode = 1;
        Relayout();
        PersistLayout();
    }

    private async Task OpenFileAsync()
    {
        EnsureHwnd();
        var picker = new FileOpenPicker();
        InitializeWithWindow.Initialize(picker, _hwnd);
        picker.FileTypeFilter.Add("*");
        var file = await picker.PickSingleFileAsync();
        if (file is null) return;
        _editor.OpenPath(file.Path);
    }

    private void OnClosed(object sender, WindowEventArgs args)
    {
        _closed = true;
        _armSizeTimer?.Stop();
        _sizedRelayoutTimer?.Stop();
        _deferredTerminalTimer?.Stop();
        _windowInteraction?.Dispose();
        _pollTimer.Stop();
        _editor.DisposeEditor();
        _bottom.DisposeTerminal();
        if (_session != IntPtr.Zero)
        {
            try
            {
                NativeCore.SessionStop(_session);
                NativeCore.SessionDestroy(_session);
            }
            catch { /* ignore */ }
            _session = IntPtr.Zero;
        }
        try { NativeCore.ReleaseInstance(); } catch { /* ignore */ }
    }

    private static string Escape(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
}
