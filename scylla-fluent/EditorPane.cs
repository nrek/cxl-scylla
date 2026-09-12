using System.Runtime.InteropServices;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace Scylla;

internal sealed class EditorPane : UserControl
{
    public event EventHandler? BackFromSettings;

    private readonly WorkbenchWindow _window;
    private bool _positionQueued;
    private bool _disposed;
    private (IntPtr Handle, int X, int Y, int W, int H)? _lastPosition;
    private (IntPtr Handle, int X, int Y, int W, int H)? _lastMinimapPosition;
    private readonly Grid _host = new() { BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle), BorderThickness = new Thickness(0, 1, 0, 0) };
    private readonly Grid _editorSurface = new();
    private readonly Border _scintillaAnchor = new() { Background = ThemeColors.Brush(ThemeColors.Surface) };
    private readonly Border _minimapAnchor = new() { Background = ThemeColors.Brush(ThemeColors.Surface) };
    private readonly ColumnDefinition _minimapSeparator = new() { Width = new GridLength(1) };
    private readonly ColumnDefinition _minimapColumn = new() { Width = new GridLength(100) };
    private readonly DispatcherTimer _minimapTimer = new() { Interval = TimeSpan.FromMilliseconds(33) };
    private bool _showMinimap;
    private readonly Grid _settingsView = new();
    private readonly Microsoft.UI.Xaml.Controls.WebView2 _strataWeb = new() { Visibility = Visibility.Collapsed };
    private readonly TextBlock _crumb = new()
    {
        FontSize = 12,
        Foreground = ThemeColors.Brush(ThemeColors.Muted),
        VerticalAlignment = VerticalAlignment.Center,
        TextTrimming = TextTrimming.CharacterEllipsis,
        HorizontalAlignment = HorizontalAlignment.Right,
    };
    private readonly TextBlock _empty = new()
    {
        Text = "Open a file or folder to start editing.",
        Foreground = ThemeColors.Brush(ThemeColors.Muted),
        HorizontalAlignment = HorizontalAlignment.Center,
        VerticalAlignment = VerticalAlignment.Center,
        TextWrapping = TextWrapping.WrapWholeWords,
    };
    private readonly StackPanel _emptyState = new()
    {
        HorizontalAlignment = HorizontalAlignment.Center,
        VerticalAlignment = VerticalAlignment.Center,
        Spacing = 16,
    };
    private readonly Image _emptyLogo = new()
    {
        Width = 120, Height = 120, Stretch = Stretch.Uniform,
        HorizontalAlignment = HorizontalAlignment.Center,
        Source = new Microsoft.UI.Xaml.Media.Imaging.BitmapImage(
            new Uri(System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "scylla-app-logo.png"))),
    };
    private readonly StackPanel _tabStrip = new()
    {
        Orientation = Orientation.Horizontal,
        Spacing = Design.GapXs,
    };
    private readonly ScrollViewer _tabScroll = new()
    {
        HorizontalScrollBarVisibility = ScrollBarVisibility.Hidden,
        VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
        HorizontalScrollMode = ScrollMode.Auto,
    };
    private readonly Border _tabsHost;
    private readonly Button _backToEditor;
    private readonly ScrollViewer _preview = new() { Visibility = Visibility.Collapsed, Padding = new Thickness(20) };
    private readonly Button _previewButton = new() { Content = "Preview", Visibility = Visibility.Collapsed };
    private bool _previewing;
    private MediaPreview? _media;
    private bool _closingDocs;
    private bool _restoredPinnedTabs;

    private IntPtr _hwndWindow;
    private IntPtr _sci;
    private IntPtr _session;
    private readonly List<EditorDoc> _docs = new();
    private EditorDoc? _active;
    private bool _showingSettings;
    private string _settingsSection = "Providers";
    private readonly StackPanel _settingsNav = new() { Spacing = 2 };
    private readonly Dictionary<string, Border> _settingsNavRows = new();

    private static readonly string[] SettingsSections =
    {
        "Providers", "Editor", "Terminal", "MCP", "Strata", "Security", "Advanced"
    };

    public EditorPane(WorkbenchWindow window)
    {
        _window = window;
        _backToEditor = Design.GhostButton("\u2190  Back to editor", ShowEditorSurface, Design.RowH);
        _backToEditor.HorizontalAlignment = HorizontalAlignment.Left;
        _backToEditor.Visibility = Visibility.Collapsed;
        Background = ThemeColors.Brush(ThemeColors.Surface);

        BuildSettingsShell();

        _tabScroll.Content = _tabStrip;
        _tabScroll.Height = 36;

        var top = new Grid
        {
            ColumnSpacing = Design.Gap,
            Padding = new Thickness(Design.Gap, Design.GapSm, Design.Gap, 0),
            MinHeight = 40,
        };
        _previewButton.Click += (_, _) => { _previewing = !_previewing; ShowEditorSurface(); };
        top.Children.Add(_crumb);
        top.Visibility = Visibility.Collapsed;
        _crumb.RegisterPropertyChangedCallback(TextBlock.TextProperty, (_, _) =>
            top.Visibility = string.IsNullOrWhiteSpace(_crumb.Text) ? Visibility.Collapsed : Visibility.Visible);

        var tabRow = new Grid { ColumnSpacing = Design.Gap };
        tabRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        tabRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _previewButton.HorizontalAlignment = HorizontalAlignment.Right;
        _previewButton.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(_previewButton, 1);
        tabRow.Children.Add(_tabScroll);
        tabRow.Children.Add(_backToEditor);
        tabRow.Children.Add(_previewButton);

        _tabsHost = new Border
        {
            Padding = new Thickness(Design.Gap, Design.GapSm, Design.Gap, Design.GapSm),
            Child = tabRow,
        };
        var tabsHost = _tabsHost;

        _emptyState.Children.Add(_emptyLogo);
        _emptyState.Children.Add(_empty);
        _editorSurface.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        _editorSurface.ColumnDefinitions.Add(_minimapSeparator);
        _editorSurface.ColumnDefinitions.Add(_minimapColumn);
        _editorSurface.Children.Add(_scintillaAnchor);
        var separator = new Border { Background = ThemeColors.Brush(ThemeColors.BorderSubtle) };
        Grid.SetColumn(separator, 1);
        _editorSurface.Children.Add(separator);
        Grid.SetColumn(_minimapAnchor, 2);
        _editorSurface.Children.Add(_minimapAnchor);
        _host.Children.Add(_emptyState);
        _host.Children.Add(_editorSurface);
        _host.Children.Add(_preview);
        _host.Children.Add(_settingsView);
        _host.Children.Add(_strataWeb);
        _settingsView.Visibility = Visibility.Collapsed;
        _editorSurface.Visibility = Visibility.Collapsed;

        var chrome = new Grid();
        chrome.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        chrome.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        chrome.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        Grid.SetRow(top, 0);
        Grid.SetRow(tabsHost, 1);
        Grid.SetRow(_host, 2);
        chrome.Children.Add(top);
        chrome.Children.Add(tabsHost);
        chrome.Children.Add(_host);
        Content = chrome;

        _scintillaAnchor.SizeChanged += (_, _) => { if (_sci != IntPtr.Zero) QueueScintillaPosition(); };
        _minimapAnchor.SizeChanged += (_, _) => { if (_sci != IntPtr.Zero) QueueScintillaPosition(); };
        _editorSurface.SizeChanged += (_, _) =>
        {
            UpdateMinimapLayout();
            if (_sci != IntPtr.Zero) QueueScintillaPosition();
        };
        _scintillaAnchor.Loaded += (_, _) => { if (_sci != IntPtr.Zero) QueueScintillaPosition(); };
        LayoutUpdated += (_, _) =>
        {
            // Skip empty-editor layout storms — DispatcherQueue DeferInvoke STOW'd on load.
            if (_sci != IntPtr.Zero) QueueScintillaPosition();
        };
        Unloaded += (_, _) => HideScintilla();
        _minimapTimer.Tick += (_, _) =>
        {
            if (_sci != IntPtr.Zero && _active is not null && _active.MinimapHandle != IntPtr.Zero && IsMinimapVisible())
                NativeCore.EditorSyncMinimap(_sci, _active.MinimapHandle);
        };
        // Start when a document activates; do not tick on the empty editor.

        RefreshTabs();
    }

    public void BindSession(IntPtr session) => _session = session;

    public void AttachWindowHwnd(IntPtr hwnd)
    {
        _hwndWindow = hwnd;
        RestorePinnedTabs();
        // Do not create Scintilla here — island HWND / DirectWrite init during
        // Window.Activated has been crashing the process (0xC000027B). Create on first OpenPath.
    }

    public void OnHostLayoutChanged()
    {
        if (Visibility != Visibility.Visible) HideScintilla();
        QueueScintillaPosition();
    }

    private void QueueScintillaPosition()
    {
        if (_disposed || _positionQueued) return;
        _positionQueued = DispatcherQueue.TryEnqueue(() =>
        {
            _positionQueued = false;
            if (!_disposed) RepositionScintilla();
        });
    }

    public async void OpenMarkdownLink(string target, string baseDirectory)
    {
        try
        {
            var link = MarkdownLink.Resolve(target, baseDirectory);
            if (link.File != null)
            {
                OpenPath(link.File);
                if (link.Line > 0 && _sci != IntPtr.Zero)
                    SendMessage(_sci, 2024 /* SCI_GOTOLINE */, (IntPtr)(link.Line - 1), IntPtr.Zero);
            }
            else if (link.Web != null)
            {
                if (!await Windows.System.Launcher.LaunchUriAsync(link.Web)) _crumb.Text = "Could not open browser.";
            }
            else _crumb.Text = "Link unavailable: " + target;
        }
        catch (Exception ex) { _crumb.Text = "Could not open link: " + ex.Message; }
    }

    public void OpenPath(string path, bool pinned = false)
    {
        var existing = _docs.FirstOrDefault(d => string.Equals(d.Path, path, StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            if (pinned && !existing.IsPinned) { existing.IsPinned = true; SortAndPersistPinnedTabs(); }
            ActivateDoc(existing);
            return;
        }
        var doc = new EditorDoc { Path = path, Title = System.IO.Path.GetFileName(path), IsPinned = pinned };
        _docs.Add(doc);
        if (pinned) SortAndPersistPinnedTabs();
        ActivateDoc(doc);
    }

    private void RestorePinnedTabs()
    {
        if (_restoredPinnedTabs) return;
        _restoredPinnedTabs = true;
        foreach (var path in BrowserPreferences.Load().PinnedTabs.Where(File.Exists))
            OpenPath(path, true);
    }

    private void SortAndPersistPinnedTabs()
    {
        var active = _active;
        var ordered = _docs.OrderByDescending(d => d.IsPinned).ToList();
        _docs.Clear();
        _docs.AddRange(ordered);
        _active = active;
        var preferences = BrowserPreferences.Load();
        preferences.PinnedTabs = _docs.Where(d => d.IsPinned).Select(d => d.Path)
            .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        preferences.Save();
        RefreshTabs();
    }

    public void UpdateFilePath(string oldPath, string? newPath)
    {
        var prefix = oldPath.TrimEnd('\\', '/') + System.IO.Path.DirectorySeparatorChar;
        foreach (var doc in _docs.ToList()) {
            if (!doc.Path.Equals(oldPath, StringComparison.OrdinalIgnoreCase) &&
                !doc.Path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
            if (newPath is null) {
                // Keep unsaved buffers available, but do not silently recreate a recycled file.
                doc.Error = "This file was deleted. Copy any unsaved content before closing.";
                doc.Deleted = true;
            } else {
                doc.Path = newPath + doc.Path[oldPath.Length..];
                doc.Title = System.IO.Path.GetFileName(doc.Path);
            }
        }
        RefreshTabs();
        if (_active != null) _crumb.Text = _active.Deleted ? _active.Error : "";
    }

    public void SaveActive()
    {
        if (_active is null || !_active.Loaded || _sci == IntPtr.Zero) return;
        if (_active.Deleted) { _crumb.Text = _active.Error; return; }
        _crumb.Text = NativeCore.EditorSavePath(_sci, _active.Path, out var err)
            ? $"Saved {_active.Title}"
            : (string.IsNullOrWhiteSpace(err) ? "Save failed" : err);
    }

    public void ShowSettings(string? section = null)
    {
        CloseMediaPreview();
        if (!string.IsNullOrWhiteSpace(section)) _settingsSection = section!;
        _showingSettings = true;
        UpdateTabArea();
        _previewButton.Visibility = Visibility.Collapsed;
        _emptyState.Visibility = Visibility.Collapsed;
        _editorSurface.Visibility = Visibility.Collapsed;
        _settingsView.Visibility = Visibility.Visible;
        _strataWeb.Visibility = Visibility.Collapsed;
        _preview.Visibility = Visibility.Collapsed;
        HideScintilla();
        SelectSettingsNav(_settingsSection);
        ReloadSettingsBody();
    }

    public void ShowStrataApp()
    {
        CloseMediaPreview();
        _showingSettings = true;
        UpdateTabArea();
        _previewButton.Visibility = Visibility.Collapsed;
        _emptyState.Visibility = Visibility.Collapsed;
        _editorSurface.Visibility = Visibility.Collapsed;
        _settingsView.Visibility = Visibility.Collapsed;
        _preview.Visibility = Visibility.Collapsed;
        HideScintilla();
        // The embedded dashboard is always the local STRATA web app. Team endpoint settings
        // configure API/sync traffic and must not redirect this editor surface.
        _strataWeb.Source = new Uri("http://127.0.0.1:8765/");
        _strataWeb.Visibility = Visibility.Visible;
    }

    public void DisposeEditor()
    {
        CloseMediaPreview();
        HideScintilla();
        _disposed = true;
        _minimapTimer.Stop();
        foreach (var doc in _docs)
        {
            if (doc.MinimapHandle != IntPtr.Zero) { NativeCore.EditorDestroy(doc.MinimapHandle); doc.MinimapHandle = IntPtr.Zero; }
            if (doc.Handle != IntPtr.Zero) { NativeCore.EditorDestroy(doc.Handle); doc.Handle = IntPtr.Zero; }
        }
        _sci = IntPtr.Zero;
    }

    private void ShowEditorSurface()
    {
        CloseMediaPreview();
        _showingSettings = false;
        UpdateTabArea();
        _settingsView.Visibility = Visibility.Collapsed;
        _strataWeb.Visibility = Visibility.Collapsed;
        var markdown = _active is not null && new[] { ".md", ".markdown", ".mdown" }.Contains(System.IO.Path.GetExtension(_active.Path).ToLowerInvariant());
        _previewButton.Visibility = markdown ? Visibility.Visible : Visibility.Collapsed;
        _previewButton.Content = _previewing ? "Source" : "Preview";
        _preview.Visibility = Visibility.Collapsed;
        if (_docs.Count == 0)
        {
            _empty.Text = "Open a file or folder to start editing.";
            _emptyLogo.Visibility = Visibility.Visible;
            _crumb.Text = "";
            _emptyState.Visibility = Visibility.Visible;
            _editorSurface.Visibility = Visibility.Collapsed;
            HideScintilla();
        }
        else if (_active?.Error is not null)
        {
            _empty.Text = _active.Error;
            _emptyLogo.Visibility = Visibility.Collapsed;
            _emptyState.Visibility = Visibility.Visible;
            _editorSurface.Visibility = Visibility.Collapsed;
            HideScintilla();
        }
        else if (_active != null && MediaPreview.Supports(_active.Path))
        {
            _emptyState.Visibility = Visibility.Collapsed;
            _editorSurface.Visibility = Visibility.Collapsed;
            HideScintilla();
            _media = new MediaPreview(_active.Path);
            _host.Children.Add(_media);
        }
        else if (markdown && _previewing && _sci != IntPtr.Zero)
        {
            _emptyState.Visibility = Visibility.Collapsed;
            _editorSurface.Visibility = Visibility.Collapsed;
            HideScintilla();
            var length = (int)SendMessage(_sci, 2006 /* SCI_GETLENGTH */, IntPtr.Zero, IntPtr.Zero);
            var buffer = Marshal.AllocHGlobal(length + 1);
            try
            {
                SendMessage(_sci, 2182 /* SCI_GETTEXT */, (IntPtr)(length + 1), buffer);
                var directory = Path.GetDirectoryName(_active!.Path) ?? AppContext.BaseDirectory;
                _preview.Content = MermaidPreview.Render(Marshal.PtrToStringUTF8(buffer, length) ?? "",
                    target => OpenMarkdownLink(target, directory), imageBaseDirectory: directory);
            }
            finally { Marshal.FreeHGlobal(buffer); }
            _preview.Visibility = Visibility.Visible;
        }
        else
        {
            _emptyState.Visibility = Visibility.Collapsed;
            _editorSurface.Visibility = Visibility.Visible;
            EnsureMinimap();
            UpdateMinimapLayout();
            ShowScintilla();
            RepositionScintilla();
        }
        BackFromSettings?.Invoke(this, EventArgs.Empty);
    }

    private void ActivateDoc(EditorDoc doc)
    {
        HideScintilla();
        _active = doc;
        _sci = doc.Handle;
        _crumb.Text = "";
        RefreshTabs();
        if (MediaPreview.Supports(doc.Path))
        {
            ShowEditorSurface();
            return;
        }
        EnsureScintilla();
        doc.Handle = _sci;
        if (_sci == IntPtr.Zero)
        {
            _crumb.Text = "Editor host unavailable — Scintilla create failed";
            doc.Error = _crumb.Text;
            ShowEditorSurface();
            return;
        }
        if (!doc.Loaded)
        {
            doc.Loaded = NativeCore.EditorLoadPath(_sci, doc.Path, out var err);
            doc.Error = doc.Loaded ? null : (string.IsNullOrWhiteSpace(err) ? "Open failed" : err);
        }
        _crumb.Text = doc.Error ?? "";
        ShowEditorSurface();
    }

    private void CloseMediaPreview()
    {
        if (_media == null) return;
        _media.Dispose();
        _host.Children.Remove(_media);
        _media = null;
    }

    private void CloseDoc(EditorDoc doc)
    {
        if (!_docs.Remove(doc)) return;
        if (doc.IsPinned)
        {
            doc.IsPinned = false;
            var preferences = BrowserPreferences.Load();
            preferences.PinnedTabs.RemoveAll(path => path.Equals(doc.Path, StringComparison.OrdinalIgnoreCase));
            preferences.Save();
        }
        if (doc.MinimapHandle != IntPtr.Zero) NativeCore.EditorDestroy(doc.MinimapHandle);
        doc.MinimapHandle = IntPtr.Zero;
        if (doc.Handle != IntPtr.Zero) NativeCore.EditorDestroy(doc.Handle);
        doc.Handle = IntPtr.Zero;
        if (_active == doc) _sci = IntPtr.Zero;
        if (_active == doc) _active = _docs.LastOrDefault();
        RefreshTabs();
        if (_active is not null) ActivateDoc(_active);
        else ShowEditorSurface();
    }

    private async Task CloseDocsAsync(IEnumerable<EditorDoc> docs)
    {
        if (_closingDocs) return;
        _closingDocs = true;
        try
        {
            foreach (var doc in docs.ToList())
            {
                if (!_docs.Contains(doc)) continue;
                if (doc.Handle != IntPtr.Zero &&
                    SendMessage(doc.Handle, 2159 /* SCI_GETMODIFY */, IntPtr.Zero, IntPtr.Zero) != IntPtr.Zero)
                {
                    HideScintilla();
                    var dialog = new ContentDialog
                    {
                        XamlRoot = XamlRoot,
                        Title = "Save changes to " + doc.Title + "?",
                        Content = doc.Deleted ? doc.Error : doc.Path,
                        PrimaryButtonText = "Save", SecondaryButtonText = "Discard", CloseButtonText = "Cancel",
                        IsPrimaryButtonEnabled = !doc.Deleted,
                        DefaultButton = ContentDialogButton.Primary,
                    };
                    var result = await dialog.ShowAsync();
                    if (result == ContentDialogResult.None) break;
                    if (result == ContentDialogResult.Primary &&
                        !NativeCore.EditorSavePath(doc.Handle, doc.Path, out var error))
                    {
                        await FileActions.Message(this, "Save failed", string.IsNullOrWhiteSpace(error) ? doc.Path : error);
                        break;
                    }
                }
                CloseDoc(doc);
            }
        }
        finally
        {
            _closingDocs = false;
            if (!_showingSettings) ShowEditorSurface();
        }
    }

    private MenuFlyout TabMenu(EditorDoc doc)
    {
        var menu = new MenuFlyout();
        void Add(string text, Func<Task> action, bool enabled = true)
        {
            var item = new MenuFlyoutItem { Text = text, IsEnabled = enabled };
            item.Click += async (_, _) =>
            {
                try { await action(); }
                catch (Exception ex) { await FileActions.Message(this, "Tab action failed", ex.Message); }
            };
            menu.Items.Add(item);
        }
        Add(doc.IsPinned ? "Unpin Tab" : "Pin Tab", () =>
        {
            doc.IsPinned = !doc.IsPinned;
            SortAndPersistPinnedTabs();
            return Task.CompletedTask;
        });
        menu.Items.Add(new MenuFlyoutSeparator());
        Add("Close", () => CloseDocsAsync(new[] { doc }));
        Add("Close Others", () => CloseDocsAsync(_docs.Where(d => d != doc)), _docs.Count > 1);
        Add("Close All to the Right", () =>
        {
            var index = _docs.IndexOf(doc);
            return index < 0 ? Task.CompletedTask : CloseDocsAsync(_docs.Skip(index + 1));
        }, _docs.IndexOf(doc) < _docs.Count - 1);
        menu.Items.Add(new MenuFlyoutSeparator());
        var exists = !doc.Deleted && File.Exists(doc.Path);
        Add("Reveal in Explorer", () => { FileActions.RevealInExplorer(doc.Path); return Task.CompletedTask; }, exists);
        Add("Open in Browser", () => { FileActions.OpenInBrowser(doc.Path); return Task.CompletedTask; }, exists);
        return menu;
    }

    /// <summary>Flat pill tabs — no stock ListView chrome, no selection bar fighting the theme.</summary>
    private void RefreshTabs()
    {
        _tabStrip.Children.Clear();
        // No open documents means no tab gutter at all — keeps the empty state uncluttered.
        UpdateTabArea();
        foreach (var doc in _docs)
        {
            var on = doc == _active;
            var title = doc.Title;
            var characters = System.Globalization.StringInfo.ParseCombiningCharacters(title);
            if (_docs.Count >= 5 && characters.Length > 18)
                title = title[..characters[12]] + "…" + title[characters[^5]..];
            var label = new TextBlock
            {
                Text = (doc.IsPinned ? "  " : "") + title,
                FontSize = 12,
                Foreground = ThemeColors.Brush(on ? ThemeColors.Text : ThemeColors.Muted),
                VerticalAlignment = VerticalAlignment.Center,
            };
            ToolTipService.SetToolTip(label, doc.Title);
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(label, doc.Title);
            var close = new Button
            {
                Content = "\u2715",
                Width = 32, Height = 32, MinWidth = 32, MinHeight = 32,
                Padding = new Thickness(0),
                BorderThickness = new Thickness(0),
                Background = ThemeColors.Brush(ThemeColors.Transparent),
                FontSize = 10,
                Foreground = ThemeColors.Brush(ThemeColors.Muted),
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(Design.GapSm, 0, 0, 0),
            };
            ToolTipService.SetToolTip(close, $"Close {doc.Title}");
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(close, $"Close {doc.Title}");
            close.Click += async (_, _) => await CloseDocsAsync(new[] { doc });

            var tab = new Border
            {
                Height = 36,
                Padding = new Thickness(Design.Gap, 0, Design.GapSm, 0),
                CornerRadius = Design.RadiusSm,
                Background = ThemeColors.Brush(on ? ThemeColors.Elevated : ThemeColors.Transparent),
                BorderBrush = ThemeColors.Brush(on ? ThemeColors.BorderSubtle : ThemeColors.Transparent),
                BorderThickness = new Thickness(1),
                Child = new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    Children = { label, close },
                },
            };
            label.PointerPressed += (_, e) =>
            {
                if (!e.GetCurrentPoint(label).Properties.IsLeftButtonPressed) return;
                e.Handled = true;
                ActivateDoc(doc);
            };
            tab.ContextFlyout = TabMenu(doc);
            ToolTipService.SetToolTip(tab, doc.Title);
            _tabStrip.Children.Add(tab);
        }
    }

    private void EnsureScintilla()
    {
        if (_sci != IntPtr.Zero || _hwndWindow == IntPtr.Zero) return;
        try
        {
            // Application HWNDs must remain siblings of WinUI's private input bridge.
            // Parenting Scintilla inside it triggers bridge teardown on window moves.
            var parent = _hwndWindow;
            _sci = NativeCore.EditorCreate(parent, 9101);
            if (_sci == IntPtr.Zero)
            {
                _crumb.Text = "EditorCreate failed (Scintilla HWND)";
                return;
            }
            var dpi = (int)Math.Round((_scintillaAnchor.XamlRoot?.RasterizationScale ?? 1.0) * 96.0);
            NativeCore.EditorApplyChrome(_sci, dpi > 0 ? dpi : 96);
            // Scintilla handles fold clicks without a Win32 WM_NOTIFY parent handler.
            SendMessage(_sci, 2240 /* SCI_SETMARGINTYPEN */, (IntPtr)2, (IntPtr)0 /* SC_MARGIN_SYMBOL */);
            SendMessage(_sci, 2244 /* SCI_SETMARGINMASKN */, (IntPtr)2, (IntPtr)unchecked((int)0xFE000000));
            SendMessage(_sci, 2246 /* SCI_SETMARGINSENSITIVEN */, (IntPtr)2, (IntPtr)1);
            for (var marker = 25; marker <= 31; marker++)
                SendMessage(_sci, 2040 /* SCI_MARKERDEFINE */, (IntPtr)marker, (IntPtr)(marker is 25 or 30 ? 8 : marker is 26 or 31 ? 7 : 5));
            SendMessage(_sci, 2663 /* SCI_SETAUTOMATICFOLD */, (IntPtr)7, IntPtr.Zero);
            try
            {
                var snap = SettingsSnapshot.Parse(NativeCore.SettingsJson());
                NativeCore.EditorSetWordWrap(_sci, snap.WordWrap ? 1 : 0);
                NativeCore.EditorSetWhitespace(_sci, snap.ShowWhitespace ? 1 : 0);
                _showMinimap = snap.ShowMinimap;
            }
            catch { /* optional */ }
            ShowScintilla();
            RepositionScintilla();
        }
        catch (Exception ex)
        {
            _crumb.Text = ex.Message;
        }
    }

    private void RepositionScintilla()
    {
        if (_sci == IntPtr.Zero || !IsLoaded || Visibility != Visibility.Visible || _showingSettings ||
            _editorSurface.Visibility != Visibility.Visible || _preview.Visibility == Visibility.Visible ||
            _scintillaAnchor.Visibility != Visibility.Visible)
        {
            HideScintilla();
            return;
        }
        try
        {
            var topLeft = _scintillaAnchor.TransformToVisual(null).TransformPoint(new Point(0, 0));
            var w = _scintillaAnchor.ActualWidth;
            var h = _scintillaAnchor.ActualHeight;
            if (w < 2 || h < 2) { HideScintilla(); return; }
            // Map the XAML island origin into the top-level client area.
            var scale = _scintillaAnchor.XamlRoot?.RasterizationScale ?? 1.0;
            var origin = new NativePoint { X = (int)Math.Round(topLeft.X * scale), Y = (int)Math.Round(topLeft.Y * scale) };
            var bridge = FindWindowEx(_hwndWindow, IntPtr.Zero, "Microsoft.UI.Content.DesktopChildSiteBridge", null);
            if (bridge == IntPtr.Zero) { HideScintilla(); return; }
            var islandX = origin.X;
            var islandY = origin.Y;
            MapWindowPoints(bridge, _hwndWindow, ref origin, 1);
            var position = (_sci, origin.X,
                origin.Y, (int)Math.Round(w * scale),
                (int)Math.Round(h * scale));
            // Composition otherwise paints over sibling HWNDs. Exclude only the
            // active editor rectangle; restore the complete island when hidden.
            var surfaceTopLeft = _editorSurface.TransformToVisual(null).TransformPoint(new Point(0, 0));
            UpdateEditorCutout(bridge, (int)Math.Round(surfaceTopLeft.X * scale),
                (int)Math.Round(surfaceTopLeft.Y * scale), (int)Math.Round(_editorSurface.ActualWidth * scale),
                (int)Math.Round(_editorSurface.ActualHeight * scale));
            if (_lastPosition != position)
            {
                NativeCore.EditorMove(position.Item1, position.Item2, position.Item3,
                    position.Item4, position.Item5);
                _lastPosition = position;
            }
            // EditorMove already uses SWP_NOACTIVATE | SWP_SHOWWINDOW. An extra
            // SW_SHOW here can activate native children during WinUI focus loss.
            if (_active is not null && _active.MinimapHandle != IntPtr.Zero && IsMinimapVisible())
            {
                var miniTopLeft = _minimapAnchor.TransformToVisual(null).TransformPoint(new Point(0, 0));
                var miniOrigin = new NativePoint { X = (int)Math.Round(miniTopLeft.X * scale), Y = (int)Math.Round(miniTopLeft.Y * scale) };
                MapWindowPoints(bridge, _hwndWindow, ref miniOrigin, 1);
                var miniPosition = (_active.MinimapHandle, miniOrigin.X, miniOrigin.Y,
                    (int)Math.Round(_minimapAnchor.ActualWidth * scale), (int)Math.Round(_minimapAnchor.ActualHeight * scale));
                if (_lastMinimapPosition != miniPosition)
                {
                    NativeCore.EditorMove(miniPosition.Item1, miniPosition.Item2, miniPosition.Item3,
                        miniPosition.Item4, miniPosition.Item5);
                    _lastMinimapPosition = miniPosition;
                }
                NativeCore.EditorSyncMinimap(_sci, _active.MinimapHandle);
            }
        }
        catch
        {
            // ignore layout races
        }
    }

    private void HideScintilla()
    {
        _lastPosition = null;
        _lastMinimapPosition = null;
        if (_sci != IntPtr.Zero) ShowWindow(_sci, 0 /* SW_HIDE */);
        if (_active is not null && _active.MinimapHandle != IntPtr.Zero)
            ShowWindow(_active.MinimapHandle, 0 /* SW_HIDE */);
        NativeSurfaceCutouts.Remove(_cutoutBridge, this);
        _cutoutBridge = IntPtr.Zero;

    }

    private IntPtr _cutoutBridge;
    private void UpdateEditorCutout(IntPtr bridge, int x, int y, int width, int height)
    {
        if (_cutoutBridge != bridge) NativeSurfaceCutouts.Remove(_cutoutBridge, this);
        _cutoutBridge = bridge;
        NativeSurfaceCutouts.Update(bridge, this, x, y, width, height);
    }
    private void ShowScintilla()
    {
        if (_editorSurface.Visibility != Visibility.Visible || _preview.Visibility == Visibility.Visible)
        {
            HideScintilla();
            return;
        }
        if (_sci != IntPtr.Zero) ShowWindow(_sci, 8 /* SW_SHOWNA */);
        if (_active is not null && _active.MinimapHandle != IntPtr.Zero && IsMinimapVisible())
            ShowWindow(_active.MinimapHandle, 8 /* SW_SHOWNA */);
    }

    private bool IsMinimapVisible() => _showMinimap && _editorSurface.ActualWidth >= 500;

    private void UpdateMinimapLayout()
    {
        var visible = IsMinimapVisible();
        _minimapSeparator.Width = new GridLength(visible ? 1 : 0);
        _minimapColumn.Width = new GridLength(visible ? 100 : 0);
        if (!visible && _active is not null && _active.MinimapHandle != IntPtr.Zero)
            ShowWindow(_active.MinimapHandle, 0 /* SW_HIDE */);
        _lastPosition = null;
        _lastMinimapPosition = null;
    }

    private void EnsureMinimap()
    {
        if (!_showMinimap || _active is null || _sci == IntPtr.Zero || _active.MinimapHandle != IntPtr.Zero) return;
        var dpi = (int)Math.Round((_minimapAnchor.XamlRoot?.RasterizationScale ?? 1.0) * 96.0);
        _active.MinimapHandle = NativeCore.EditorCreateMinimap(_hwndWindow, _sci, 9102, dpi > 0 ? dpi : 96);
        if (_active.MinimapHandle != IntPtr.Zero && !_minimapTimer.IsEnabled) _minimapTimer.Start();
    }

    // ---------------------------------------------------------------- settings

    private void BuildSettingsShell()
    {
        _settingsView.Background = ThemeColors.Brush(ThemeColors.Surface);
        _settingsView.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(168) });
        _settingsView.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        foreach (var s in SettingsSections)
        {
            var section = s;
            var row = new Border
            {
                Height = Design.RowH,
                CornerRadius = Design.RadiusSm,
                Padding = new Thickness(Design.Gap, 0, Design.GapSm, 0),
                Background = ThemeColors.Brush(ThemeColors.Transparent),
                Child = new TextBlock
                {
                    Text = section,
                    FontSize = 13,
                    VerticalAlignment = VerticalAlignment.Center,
                    Foreground = ThemeColors.Brush(ThemeColors.Secondary),
                },
                Tag = section,
            };
            row.PointerPressed += (_, _) =>
            {
                _settingsSection = section;
                SelectSettingsNav(section);
                ReloadSettingsBody();
            };
            _settingsNavRows[section] = row;
            _settingsNav.Children.Add(row);
        }

        var left = new Grid
        {
            Padding = new Thickness(Design.Gap),
            RowSpacing = Design.Gap,
            Background = ThemeColors.Brush(ThemeColors.Panel),
        };
        left.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        var navScroll = new ScrollViewer { Content = _settingsNav };
        left.Children.Add(navScroll);

        var bodyHost = new Grid { Tag = "settings-body" };
        Grid.SetColumn(left, 0);
        Grid.SetColumn(bodyHost, 1);
        _settingsView.Children.Add(left);
        _settingsView.Children.Add(bodyHost);
        SelectSettingsNav(_settingsSection);
    }

    private void UpdateTabArea()
    {
        _tabsHost.Visibility = _showingSettings || _docs.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        _backToEditor.Visibility = _showingSettings ? Visibility.Visible : Visibility.Collapsed;
        _tabScroll.Visibility = _showingSettings ? Visibility.Collapsed : Visibility.Visible;
    }

    private void SelectSettingsNav(string section)
    {
        foreach (var (key, row) in _settingsNavRows)
        {
            var on = key == section;
            row.Background = ThemeColors.Brush(on ? ThemeColors.Elevated : ThemeColors.Transparent);
            if (row.Child is TextBlock label)
            {
                label.Foreground = ThemeColors.Brush(on ? ThemeColors.Text : ThemeColors.Secondary);
                label.FontWeight = on ? FontWeights.SemiBold : FontWeights.Normal;
            }
        }
    }

    private void ReloadSettingsBody()
    {
        var bodyHost = _settingsView.Children.OfType<Grid>().FirstOrDefault(g => g.Tag as string == "settings-body");
        if (bodyHost is null) return;
        bodyHost.Children.Clear();

        string json;
        try { json = NativeCore.SettingsJson() ?? "{}"; }
        catch { json = "{}"; }
        var snap = SettingsSnapshot.Parse(json);

        var stack = new StackPanel
        {
            Spacing = Design.GapLg,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Margin = new Thickness(Design.GapLg, Design.GapLg, Design.GapLg, Design.GapXl),
        };
        var heading = new Grid { ColumnSpacing = Design.Gap };
        heading.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        heading.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        heading.Children.Add(Design.Heading(_settingsSection));
        if (_settingsSection == "Providers")
        {
            var toggle = new ToggleSwitch {
                IsOn = snap.VerboseAgentProgress, OnContent = "", OffContent = "", MinWidth = 0,
                VerticalAlignment = VerticalAlignment.Center,
            };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(toggle, "Verbose Agent Progress");
            ToolTipService.SetToolTip(toggle, "Show agent replies in the chat as they arrive.");
            var saving = false;
            toggle.Toggled += (_, _) => {
                if (saving) return;
                if (!NativeCore.PatchSettings(System.Text.Json.JsonSerializer.Serialize(new {
                    verbose_agent_progress = toggle.IsOn,
                }), out var error)) {
                    saving = true;
                    toggle.IsOn = !toggle.IsOn;
                    saving = false;
                    _crumb.Text = "Could not save progress preference: " + error;
                }
            };
            var option = new StackPanel {
                Orientation = Orientation.Horizontal, Spacing = Design.GapSm,
                VerticalAlignment = VerticalAlignment.Center,
            };
            var label = Design.Caption("Verbose Agent Progress");
            label.VerticalAlignment = VerticalAlignment.Center;
            option.Children.Add(label);
            option.Children.Add(toggle);
            Grid.SetColumn(option, 1);
            heading.Children.Add(option);
        }
        stack.Children.Add(new StackPanel
        {
            Spacing = Design.GapXs,
            Children =
            {
                heading,
                Design.Caption(SectionBlurb(_settingsSection)),
            },
        });

        switch (_settingsSection)
        {
            case "Providers":
                foreach (var el in BuildProvidersSection(snap))
                    stack.Children.Add(el);
                break;
            case "Editor":
                foreach (var el in BuildEditorSettings(snap))
                    stack.Children.Add(el);
                break;
            case "Terminal":
                foreach (var el in BuildTerminalSettings(snap))
                    stack.Children.Add(el);
                break;
            case "MCP":
                foreach (var el in BuildMcpSettings())
                    stack.Children.Add(el);
                break;
            case "Strata":
                foreach (var el in BuildStrataSettings())
                    stack.Children.Add(el);
                break;
            case "Security":
                foreach (var el in BuildSecuritySettings())
                    stack.Children.Add(el);
                break;
            case "Advanced":
                foreach (var el in BuildAdvancedSettings(snap))
                    stack.Children.Add(el);
                break;
            default:
                stack.Children.Add(PendingCard(_settingsSection));
                break;
        }

        bodyHost.Children.Add(new ScrollViewer
        {
            Content = stack,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
        });
    }

    private string _providerCatalogKey = "";
    private DateTime _nextProviderRefresh;

    private static string ProviderCatalogKey(IEnumerable<ProviderCard> cards) =>
        System.Text.Json.JsonSerializer.Serialize(cards.Select(c => new
        {
            c.Id, c.Connected, c.AuthLabel, c.Detail,
            Models = c.Models.Select(m => new { m.Id, m.Label }),
        }));

    public void RefreshProviderCatalog()
    {
        if (!_showingSettings || _settingsSection != "Providers" || _session == IntPtr.Zero ||
            DateTime.UtcNow < _nextProviderRefresh) return;
        _nextProviderRefresh = DateTime.UtcNow.AddSeconds(2);
        var cards = ProviderCard.ParseList(NativeCore.SessionProvidersJson(_session));
        if (ProviderCatalogKey(cards) != _providerCatalogKey) ReloadSettingsBody();
    }

    private IEnumerable<FrameworkElement> BuildProvidersSection(SettingsSnapshot snap)
    {
        var cards = new List<ProviderCard>();
        if (_session != IntPtr.Zero)
        {
            try { cards = ProviderCard.ParseList(NativeCore.SessionProvidersJson(_session)); }
            catch { /* ignore */ }
        }

        _providerCatalogKey = ProviderCatalogKey(cards);
        if (cards.Count == 0)
        {
            yield return Design.Card(Design.Caption(
                "Start the agent runtime (sends auto-start it) so provider status can load."),
                new Thickness(Design.GapLg));
        }
        else
        {
            foreach (var card in cards)
                yield return AuthProviderCard(card);
        }

        var refresh = QuietButton("Refresh providers and models");
        refresh.Click += (_, _) =>
        {
            if (!_window.EnsureRuntimeLive(out var error)) { _crumb.Text = error; return; }
            foreach (var provider in cards.Where(c => c.Connected))
                if (!NativeCore.SessionProviderAction(_session, provider.Id, "refresh_models", null, out error))
                { _crumb.Text = error; return; }
            ReloadSettingsBody();
        };
        yield return refresh;
        yield return DefaultProviderCard(snap, cards);
    }

    public async void ConnectProvider(string id)
    {
        try {
            var card = ProviderCard.ParseList(NativeCore.SessionProvidersJson(_session)).FirstOrDefault(p => p.Id == id);
            if (card is null || card.Connected) return;
            ShowSettings("Providers");
            if (id == "claude") RunProviderAction(id, "login_cli", null);
            else await PromptAndConnectKeyAsync(card);
        } catch (Exception ex) { await FileActions.Message(this, "Connection failed", ex.Message); }
    }

    private Border AuthProviderCard(ProviderCard card)
    {
        var titleRow = new Grid { ColumnSpacing = Design.Gap };
        titleRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        titleRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var name = new TextBlock
        {
            Text = string.IsNullOrWhiteSpace(card.DisplayName) ? card.Id : card.DisplayName,
            FontSize = 14,
            FontWeight = FontWeights.SemiBold,
            Foreground = ThemeColors.Brush(ThemeColors.Text),
            VerticalAlignment = VerticalAlignment.Center,
        };
        var badge = new TextBlock
        {
            Text = card.Connected ? "Connected" : "Not connected",
            FontSize = 12,
            Foreground = ThemeColors.Brush(card.Connected ? ThemeColors.Secondary : ThemeColors.Muted),
            VerticalAlignment = VerticalAlignment.Center,
        };
        Grid.SetColumn(name, 0);
        Grid.SetColumn(badge, 1);
        titleRow.Children.Add(name);
        titleRow.Children.Add(badge);

        var meta = new StackPanel
        {
            Spacing = 2,
            Children =
            {
                Design.Caption(card.Product),
                Design.Caption($"Authentication: {(string.IsNullOrWhiteSpace(card.AuthLabel) ? (card.Connected ? "Connected" : "Not connected") : card.AuthLabel)}"),
            },
        };
        if (!string.IsNullOrWhiteSpace(card.Detail))
            meta.Children.Add(Design.Caption(card.Detail));
        if (card.Id == "claude" && !card.ClaudeCliPresent)
            meta.Children.Add(Design.Caption("Install Claude Code and ensure `claude` is on PATH."));

        var actions = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = Design.GapSm,
            Margin = new Thickness(0, Design.GapSm, 0, 0),
        };

        var (primaryLabel, primaryAction, secondaryLabel, secondaryAction) = card.Id switch
        {
            "openai" => ("Sign in", "sign_in", "Sign out", "sign_out"),
            "openai-api" => ("Connect", "connect_key", "Disconnect", "disconnect"),
            "claude" => ("Connect", "login_cli", "Disconnect", "disconnect"),
            "claude-api" => ("Connect", "connect_key", "Disconnect", "disconnect"),
            _ => ("Connect", "connect_key", "Disconnect", "disconnect"),
        };

        var primary = QuietButton(primaryLabel);
        primary.IsEnabled = !card.Connected && (card.Id != "claude" || card.ClaudeCliPresent);
        primary.Click += async (_, _) =>
        {
            if (primaryAction == "connect_key")
                await PromptAndConnectKeyAsync(card);
            else
                RunProviderAction(card.Id, primaryAction, null);
        };

        var secondary = QuietButton(secondaryLabel);
        secondary.IsEnabled = card.Connected;
        secondary.Click += (_, _) => RunProviderAction(card.Id, secondaryAction, null);

        actions.Children.Add(primary);
        actions.Children.Add(secondary);

        var body = new StackPanel
        {
            Spacing = Design.GapSm,
            Padding = new Thickness(0, Design.GapSm, 0, Design.GapSm),
            Children = { titleRow, meta, actions },
        };
        if (card.Connected)
        {
            body.Children.Add(Design.Caption("Models shown in the Agent dropdown when this provider is selected:"));
            var modelList = new StackPanel { Spacing = Design.GapXs };
            var feedback = Design.Caption("");
            foreach (var model in card.Models)
            {
                var check = new CheckBox { Content = model.Label, IsChecked = model.Enabled };
                ToolTipService.SetToolTip(check, model.Id);
                check.Click += (_, _) =>
                {
                    var enabled = check.IsChecked == true;
                    if (!NativeCore.SessionProviderAction(_session, card.Id,
                        enabled ? "enable_model" : "disable_model", model.Id, out var error))
                    {
                        check.IsChecked = !enabled;
                        feedback.Text = error;
                    }
                    else feedback.Text = "Saved";
                };
                modelList.Children.Add(check);
            }
            if (card.Models.Count == 0)
                modelList.Children.Add(Design.Caption("No models loaded yet. Start the runtime, then refresh."));
            body.Children.Add(new ScrollViewer
            {
                Content = modelList, MaxHeight = 260,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            });
            body.Children.Add(feedback);
        }
        return Design.Card(body, new Thickness(Design.GapLg, Design.GapSm, Design.GapLg, Design.GapSm));
    }

    private static Button QuietButton(string label)
    {
        var b = new Button
        {
            Content = label,
            Height = Design.RowCompact,
            Padding = new Thickness(Design.Gap, 0, Design.Gap, 0),
            CornerRadius = Design.RadiusSm,
            BorderThickness = new Thickness(1),
            BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle),
            Background = ThemeColors.Brush(ThemeColors.Elevated),
            Foreground = ThemeColors.Brush(ThemeColors.Text),
            FontSize = 12,
        };
        return b;
    }

    private async Task PromptAndConnectKeyAsync(ProviderCard card)
    {
        if (_session == IntPtr.Zero || XamlRoot is null) return;
        var box = new PasswordBox { PlaceholderText = "API key", MinWidth = 280 };
        var dlg = new ContentDialog
        {
            Title = card.Id == "claude-api" ? "Anthropic API key" : "OpenAI API key",
            Content = box,
            PrimaryButtonText = "Save",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = XamlRoot,
        };
        var result = await dlg.ShowAsync();
        if (result != ContentDialogResult.Primary) return;
        var key = box.Password?.Trim() ?? "";
        if (key.Length == 0)
        {
            _crumb.Text = "API key required";
            return;
        }
        RunProviderAction(card.Id, "connect_key", key);
    }

    private void RunProviderAction(string providerId, string action, string? secret)
    {
        if (_session == IntPtr.Zero)
        {
            _crumb.Text = "No session";
            return;
        }
        if ((action == "sign_in" || action == "sign_out") && !_window.EnsureRuntimeLive(out var startErr))
        {
            _crumb.Text = string.IsNullOrWhiteSpace(startErr) ? "Could not start runtime" : startErr;
            return;
        }
        if (!NativeCore.SessionProviderAction(_session, providerId, action, secret, out var err))
        {
            _crumb.Text = string.IsNullOrWhiteSpace(err) ? "Provider action failed" : err;
            return;
        }
        _crumb.Text = action switch
        {
            "sign_in" => "Complete ChatGPT sign-in in the browser…",
            "login_cli" => "Complete Claude Code login in the console/browser…",
            "connect_key" => "API key saved",
            "sign_out" or "disconnect" => "Disconnected",
            _ => "Updated",
        };
        if (_showingSettings && _settingsSection == "Providers")
            ReloadSettingsBody();
    }

    private FrameworkElement DefaultProviderCard(SettingsSnapshot snap, IReadOnlyList<ProviderCard> cards)
    {
        var connected = cards.Where(c => c.Connected).ToList();
        var providerBox = new ComboBox
        {
            ItemsSource = connected,
            DisplayMemberPath = nameof(ProviderCard.DisplayName),
            SelectedItem = connected.FirstOrDefault(c => c.Id == snap.DefaultProvider),
            PlaceholderText = connected.Count == 0 ? "Connect a provider first" : "Select a provider",
            IsEnabled = connected.Count > 0,
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };

        var apply = QuietButton("Apply");
        apply.IsEnabled = providerBox.SelectedItem is ProviderCard;
        providerBox.SelectionChanged += (_, _) => apply.IsEnabled = providerBox.SelectedItem is ProviderCard;
        apply.Click += (_, _) =>
        {
            if (providerBox.SelectedItem is not ProviderCard provider) return;
            if (!NativeCore.SessionProviderAction(_session, provider.Id, "set_default", null, out var error))
            {
                _crumb.Text = error;
                return;
            }
            ReloadSettingsBody();
        };

        var editor = new Grid { ColumnSpacing = Design.GapSm };
        editor.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        editor.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Grid.SetColumn(providerBox, 0);
        Grid.SetColumn(apply, 1);
        editor.Children.Add(providerBox);
        editor.Children.Add(apply);

        return SettingsCard(
            Row("Default provider", "Backend used for new chats.", editor),
            Row("Selected model", "Chosen in the toolbar model picker.", ReadOnlyValue(snap.SelectedModel)),
            Row("Codex path", "Override the Codex CLI location.",
                ReadOnlyValue(string.IsNullOrWhiteSpace(snap.CodexPath) ? "(default)" : snap.CodexPath)));
    }

    private IEnumerable<FrameworkElement> BuildEditorSettings(SettingsSnapshot snap)
    {
        var wrap = new ToggleSwitch { IsOn = snap.WordWrap, OnContent = "On", OffContent = "Off" };
        wrap.Toggled += (_, _) =>
        {
            NativeCore.PatchSettings($"{{\"word_wrap\":{(wrap.IsOn ? "true" : "false")}}}", out string _);
            if (_sci != IntPtr.Zero) NativeCore.EditorSetWordWrap(_sci, wrap.IsOn ? 1 : 0);
        };
        var ws = new ToggleSwitch { IsOn = snap.ShowWhitespace, OnContent = "On", OffContent = "Off" };
        ws.Toggled += (_, _) =>
        {
            NativeCore.PatchSettings($"{{\"show_whitespace\":{(ws.IsOn ? "true" : "false")}}}", out string _);
            foreach (var doc in _docs)
                if (doc.Handle != IntPtr.Zero) NativeCore.EditorSetWhitespace(doc.Handle, ws.IsOn ? 1 : 0);
        };
        var minimap = new ToggleSwitch { IsOn = snap.ShowMinimap, OnContent = "On", OffContent = "Off" };
        minimap.Toggled += (_, _) =>
        {
            _showMinimap = minimap.IsOn;
            NativeCore.PatchSettings($"{{\"show_minimap\":{(_showMinimap ? "true" : "false")}}}", out string _);
            if (_showMinimap) EnsureMinimap();
            UpdateMinimapLayout();
            QueueScintillaPosition();
        };
        yield return SettingsCard(
            Row("Word wrap", "Wrap long lines in the Scintilla editor.", wrap),
            Row("Show whitespace", "Render spaces and tabs as glyphs.", ws),
            Row("Show minimap", "Show a compact code overview along the right edge of the editor.", minimap));
        yield return Design.Caption("Line numbers are always on.");
    }

    private IEnumerable<FrameworkElement> BuildTerminalSettings(SettingsSnapshot snap)
    {
        string? json = null;
        try { json = NativeCore.TerminalProfilesJson(); } catch { /* ignore */ }
        var policy = new ComboBox { MinWidth = 160 };
        foreach (var p in new[] { "ask", "allow", "block" }) policy.Items.Add(p);
        policy.SelectedItem = string.IsNullOrWhiteSpace(snap.AgentTerminalPolicy) ? "ask" : snap.AgentTerminalPolicy;
        policy.SelectionChanged += (_, _) =>
        {
            if (policy.SelectedItem is string s)
                NativeCore.TerminalProfilesSave($"{{\"agent_terminal_policy\":\"{Escape(s)}\"}}", out string _);
        };
        var mouse = new ComboBox
        {
            ItemsSource = new[] { "Highlight Copy", "Right Click Copy", "Right Click Menu" },
            SelectedIndex = (int)PanelPreferences.Current.TerminalMouse,
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        var mouseHelp = Design.Caption("");
        void UpdateMouseHelp() => mouseHelp.Text = mouse.SelectedIndex switch
        {
            1 => "Right-click a selection to copy it. Right-click again to paste.",
            2 => "Right-click for Copy, Paste, Clear, and New Terminal.",
            _ => "Selecting text copies it. Right-click to paste.",
        };
        UpdateMouseHelp();
        mouse.SelectionChanged += (_, _) =>
        {
            if (mouse.SelectedIndex < 0) return;
            PanelPreferences.Current.TerminalMouse = (TerminalMouseBehavior)mouse.SelectedIndex;
            UpdateMouseHelp();
            try { PanelPreferences.Current.Save(); }
            catch (Exception ex) { _crumb.Text = "Could not save terminal preferences: " + ex.Message; }
        };
        var top = new Grid { ColumnSpacing = Design.GapLg };
        top.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        top.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        top.Children.Add(new StackPanel { Spacing = Design.GapSm, Children = {
            new TextBlock { Text = "Default agent policy" }, policy,
            Design.Caption("Used by terminals without their own policy.") } });
        var mouseColumn = new StackPanel { Spacing = Design.GapSm, Children = {
            new TextBlock { Text = "Terminal mouse behavior" }, mouse, mouseHelp } };
        Grid.SetColumn(mouseColumn, 1);
        top.Children.Add(mouseColumn);
        yield return Design.Card(top, new Thickness(Design.GapLg));
        yield return Design.GhostButton("Refresh terminals", ReloadSettingsBody);

        if (string.IsNullOrWhiteSpace(json))
        {
            yield return Design.Caption("No terminal profiles discovered.");
            yield break;
        }
        using var doc = System.Text.Json.JsonDocument.Parse(json);
        var defaultId = doc.RootElement.TryGetProperty("default_id", out var d) ? d.GetString() ?? "" : "";
        if (doc.RootElement.TryGetProperty("profiles", out var profiles) && profiles.ValueKind == System.Text.Json.JsonValueKind.Array)
        {
            var list = new StackPanel { Spacing = Design.GapSm };
            foreach (var item in profiles.EnumerateArray())
            {
                var id = item.TryGetProperty("id", out var i) ? i.GetString() ?? "" : "";
                var name = item.TryGetProperty("name", out var n) ? n.GetString() ?? id : id;
                var enabled = !item.TryGetProperty("enabled", out var e) || e.ValueKind != System.Text.Json.JsonValueKind.False;
                var row = new Grid { ColumnSpacing = Design.Gap };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                var label = new TextBlock { Text = name, TextWrapping = TextWrapping.Wrap, VerticalAlignment = VerticalAlignment.Center };
                var tog = new ToggleSwitch { IsOn = enabled, OnContent = "", OffContent = "", MinWidth = 0 };
                Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(tog, "Enable " + name);
                var idCopy = id;
                var agentPolicy = new ComboBox { MinWidth = 80, VerticalAlignment = VerticalAlignment.Center };
                foreach (var value in new[] { "block", "ask", "allow" }) agentPolicy.Items.Add(value);
                agentPolicy.SelectedItem = item.TryGetProperty("agent_policy", out var ap) ? ap.GetString() : "ask";
                Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(agentPolicy, name + " agent policy");
                agentPolicy.SelectionChanged += (_, _) => {
                    if (!NativeCore.TerminalProfilesSave(System.Text.Json.JsonSerializer.Serialize(new {
                        policies = new Dictionary<string, string> { [idCopy] = agentPolicy.SelectedItem as string ?? "ask" }
                    }), out var error)) _crumb.Text = error;
                };
                tog.Toggled += (_, _) =>
                {
                    NativeCore.TerminalProfilesSave(
                        $"{{\"enabled\":{{\"{Escape(idCopy)}\":{(tog.IsOn ? "true" : "false")}}}}}", out string _);
                };
                var def = Design.GhostButton(id == defaultId ? "Default" : "Make default", () =>
                {
                    NativeCore.TerminalProfilesSave($"{{\"default_id\":\"{Escape(idCopy)}\"}}", out string _);
                    ReloadSettingsBody();
                });
                Grid.SetColumn(tog, 0);
                Grid.SetColumn(label, 1);
                Grid.SetColumn(agentPolicy, 2);
                Grid.SetColumn(def, 3);
                row.Children.Add(label);
                row.Children.Add(tog);
                row.Children.Add(agentPolicy);
                row.Children.Add(def);
                list.Children.Add(row);
            }
            yield return Design.Card(list, new Thickness(Design.GapLg));
        }
    }

    private IEnumerable<FrameworkElement> BuildMcpSettings()
    {
        yield return new McpSettingsPane();
    }

    private IEnumerable<FrameworkElement> BuildStrataSettings()
    {
        string? json = null;
        try { json = NativeCore.StrataJson(); } catch { /* ignore */ }
        var enabled = new ToggleSwitch { Header = "Enable STRATA", OffContent = "Off", OnContent = "On" };
        var mode = new ComboBox { ItemsSource = new[] { "Solo", "Team" }, SelectedIndex = 0,
            HorizontalAlignment = HorizontalAlignment.Stretch };
        var endpoint = Design.Field("https://strata.domain.com");
        var bearer = new PasswordBox { PlaceholderText = "strata_live_xxxxxxxxx" };
        var verification = Design.Caption("");
        var teamFields = new StackPanel { Spacing = Design.GapSm };
        if (!string.IsNullOrWhiteSpace(json))
        {
            using var doc = System.Text.Json.JsonDocument.Parse(json);
            endpoint.Text = doc.RootElement.TryGetProperty("endpoint", out var ep) ? ep.GetString() ?? "" : "";
            enabled.IsOn = doc.RootElement.TryGetProperty("enabled", out var en) && en.GetBoolean();
            mode.SelectedIndex = doc.RootElement.TryGetProperty("mode", out var md) && md.GetString() == "team" ? 1 : 0;
        }
        try
        {
            var credential = NativeCore.StrataAction("credential", "{}", out var credentialError);
            if (string.IsNullOrWhiteSpace(credentialError) && !string.IsNullOrWhiteSpace(credential))
            {
                using var stored = System.Text.Json.JsonDocument.Parse(credential);
                bearer.Password = stored.RootElement.TryGetProperty("bearer", out var key) ? key.GetString() ?? "" : "";
            }
        }
        catch { /* credential remains protected and can be replaced */ }
        void SyncFields()
        {
            mode.IsEnabled = enabled.IsOn;
            teamFields.Visibility = enabled.IsOn && mode.SelectedIndex == 1 ? Visibility.Visible : Visibility.Collapsed;
        }
        enabled.Toggled += (_, _) => SyncFields();
        mode.SelectionChanged += (_, _) => SyncFields();
        var save = Design.PrimaryButton("Save", () =>
        {
            var payload =
                $"{{\"enabled\":{(enabled.IsOn ? "true" : "false")},\"mode\":\"{(mode.SelectedIndex == 1 ? "team" : "solo")}\"," +
                $"\"endpoint\":\"{Escape(endpoint.Text ?? "")}\",\"bearer\":\"{Escape(bearer.Password ?? "")}\"}}";
            NativeCore.StrataAction("save", payload, out var err);
            verification.Text = string.IsNullOrWhiteSpace(err) ? "Settings saved" : "Could not save: " + err;
        });
        var verify = Design.GhostButton("Verify", () =>
        {
            if (!bearer.Password.StartsWith("strata_live_", StringComparison.Ordinal))
            {
                verification.Text = "Invalid key — use the strata_live_ key issued for this host.";
                verification.Foreground = new SolidColorBrush(Microsoft.UI.Colors.IndianRed);
                return;
            }
            if (!Uri.TryCreate(endpoint.Text, UriKind.Absolute, out var host) || host.Scheme != Uri.UriSchemeHttps)
            {
                verification.Text = "Host not found — enter a valid HTTPS STRATA address.";
                verification.Foreground = new SolidColorBrush(Microsoft.UI.Colors.IndianRed);
                return;
            }
            var payload =
                $"{{\"enabled\":true,\"mode\":\"team\",\"endpoint\":\"{Escape(endpoint.Text ?? "")}\"," +
                $"\"bearer\":\"{Escape(bearer.Password ?? "")}\"}}";
            NativeCore.StrataAction("save", payload, out var saveError);
            if (!string.IsNullOrWhiteSpace(saveError))
            {
                verification.Text = "Could not save STRATA settings: " + saveError;
                verification.Foreground = new SolidColorBrush(Microsoft.UI.Colors.IndianRed);
                return;
            }
            var result = NativeCore.StrataAction("test", "{}", out var err);
            var ok = false;
            var status = 0;
            var message = "";
            try
            {
                using var health = System.Text.Json.JsonDocument.Parse(result ?? "{}");
                ok = health.RootElement.TryGetProperty("ok", out var healthy) && healthy.GetBoolean();
                status = health.RootElement.TryGetProperty("http_status", out var code) ? code.GetInt32() : 0;
                message = health.RootElement.TryGetProperty("message", out var detail) ? detail.GetString() ?? "" : "";
            }
            catch { message = result ?? ""; }
            verification.Text = ok && status == 200 ? "✓ Verified" :
                status is 401 or 403 ? "Invalid key." :
                status == 0 ? "Host not found or unavailable." :
                $"STRATA returned HTTP {status}{(message.Length > 0 ? ": " + message : ".")}";
            verification.Foreground = new SolidColorBrush(ok && status == 200
                ? Microsoft.UI.Colors.LimeGreen : Microsoft.UI.Colors.IndianRed);
        });
        teamFields.Children.Add(Row("Host", "Installed STRATA host address.", endpoint));
        teamFields.Children.Add(Row("Remote API Key", "Stored securely and displayed as a protected password.", bearer));
        teamFields.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = Design.Gap,
            Children = { verify, verification } });
        SyncFields();
        yield return SettingsCard(
            Row("STRATA", "Workspace knowledge index and handoff sync.", enabled),
            Row("Mode", "Solo uses the local app. Team connects to a shared host.", mode),
            teamFields,
            Row("", "", save));
    }

    private IEnumerable<FrameworkElement> BuildSecuritySettings()
    {
        yield return SettingsCard(
            Row("Keyring", "Encrypted app vault and project connection settings.",
                Design.Caption("Unlock below to manage saved values.")));
        yield return new SecuritySettingsPane(_window);
    }

    private IEnumerable<FrameworkElement> BuildAdvancedSettings(SettingsSnapshot snap)
    {
        var path = Design.Field("Codex path");
        path.Text = snap.CodexPath;
        var save = Design.PrimaryButton("Save Codex path", () =>
        {
            NativeCore.PatchSettings($"{{\"codex_path\":\"{Escape(path.Text ?? "")}\"}}", out string _);
            _crumb.Text = "Codex path saved";
        });
        yield return SettingsCard(
            Row("Codex executable", "Override discovery; leave blank for auto.", path),
            Row("", "", save),
            Row("Core version", "Shared DLL beside scylla.exe.",
                ReadOnlyValue(NativeCore.Version() ?? "?")));
    }

    private static FrameworkElement PendingCard(string section) => SettingsCard(
        Row($"{section} editor", "Not wired yet — lands in the settings-depth slice.",
            ReadOnlyValue("Read-only")));

    /// <summary>One rounded container holding rows split by hairlines — the only settings shape.</summary>
    private static Border SettingsCard(params FrameworkElement[] rows)
    {
        var stack = new StackPanel();
        for (var i = 0; i < rows.Length; i++)
        {
            if (i > 0) stack.Children.Add(Design.Divider(new Thickness(0)));
            stack.Children.Add(rows[i]);
        }
        return Design.Card(stack, new Thickness(Design.GapLg, Design.GapSm, Design.GapLg, Design.GapSm));
    }

    /// <summary>
    /// Label / description / control stacked vertically. Settings lives inside the editor
    /// column, which can be ~280px wide — a side-by-side row shreds long paths there.
    /// </summary>
    private static FrameworkElement Row(string label, string description, FrameworkElement control)
    {
        control.HorizontalAlignment = HorizontalAlignment.Stretch;
        control.Margin = new Thickness(0, Design.GapSm, 0, 0);
        return new StackPanel
        {
            Spacing = 2,
            Padding = new Thickness(0, Design.Gap, 0, Design.Gap),
            Children =
            {
                new TextBlock
                {
                    Text = label,
                    FontSize = 13,
                    FontWeight = FontWeights.SemiBold,
                    Foreground = ThemeColors.Brush(ThemeColors.Text),
                },
                Design.Caption(description),
                control,
            },
        };
    }

    private static FrameworkElement ReadOnlyValue(string value) => new TextBlock
    {
        Text = value,
        FontSize = 13,
        FontFamily = new FontFamily("Consolas"),
        Foreground = ThemeColors.Brush(ThemeColors.Secondary),
        TextWrapping = TextWrapping.Wrap,
        IsTextSelectionEnabled = true,
    };

    private static string SectionBlurb(string section) => section switch
    {
        "Providers" => "Sign in or connect each backend. Secrets stay in Windows Credential Manager — never in settings.json.",
        "Editor" => "Font, indentation, and Scintilla behavior.",
        "Terminal" => "Shell, profile, and startup directory for the bottom panel.",
        "MCP" => "Model Context Protocol servers available to the agent.",
        "Strata" => "Workspace knowledge index and handoff sync.",
        "Security" => "Grants, Keyring, and command approval policy.",
        _ => "Diagnostics and low-level overrides.",
    };

    private static string Escape(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");

    [DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string className, string? title);

    [StructLayout(LayoutKind.Sequential)] private struct NativePoint { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct NativeRect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern int MapWindowPoints(IntPtr from, IntPtr to, ref NativePoint point, uint count);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr hwnd, out NativeRect rect);
    [DllImport("user32.dll")] private static extern int SetWindowRgn(IntPtr hwnd, IntPtr region, bool redraw);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateRectRgn(int left, int top, int right, int bottom);
    [DllImport("gdi32.dll")] private static extern int CombineRgn(IntPtr target, IntPtr first, IntPtr second, int mode);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);

    private sealed class EditorDoc
    {
        public string Path { get; set; } = "";
        public string Title { get; set; } = "";
        public IntPtr Handle { get; set; }
        public IntPtr MinimapHandle { get; set; }
        public bool Loaded { get; set; }
        public bool Deleted { get; set; }
        public string? Error { get; set; }
        public bool IsPinned { get; set; }
    }
}
