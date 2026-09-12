using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Scylla;

internal sealed class AgentPane : UserControl
{
    private readonly Window _window;
    public void AddReviewDraft(string text)
    {
        _composer.Text = string.IsNullOrWhiteSpace(_composer.Text) ? text : _composer.Text + "\n\n" + text;
    }

    public event EventHandler<ChatSendRequest>? SendRequested;
    public event EventHandler? StopRequested;
    public event EventHandler<string>? OpenThreadRequested;
    public event EventHandler<string>? OpenPlanRequested;
    public event EventHandler<string>? OpenLinkRequested;
    private readonly Button _openPlan = new() { Content = "Open saved Knowledge plan", Visibility = Visibility.Collapsed };
    private string _lastPlanPath = "";
    private bool _scrollPending;

    private readonly ScrollViewer _scroll = new()
    {
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
    };
    private readonly StackPanel _transcript = new() { Spacing = 0 };
    private readonly TextBlock _empty = Design.Caption("No messages yet. Ask Scylla something to begin.");

    private readonly Border _activityCard;
    private readonly TextBlock _activity = new()
    {
        FontSize = 12,
        Foreground = ThemeColors.Brush(ThemeColors.Secondary),
        TextWrapping = TextWrapping.WrapWholeWords,
    };

    private readonly StackPanel _chatTabs = new()
    {
        Orientation = Orientation.Horizontal,
        Spacing = Design.GapXs,
    };
    private readonly ScrollViewer _chatTabScroll = new()
    {
        HorizontalScrollBarVisibility = ScrollBarVisibility.Hidden,
        VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
        HorizontalScrollMode = ScrollMode.Auto,
        Height = 30,
    };

    private readonly Border _composerBox;
    private readonly ComboBox _mode = new()
    {
        Items = { "Execute", "Plan", "Ask" }, SelectedIndex = 0,
        MinWidth = 100, Height = 30, FontSize = 12,
    };
    private readonly Grid _composerFooter = new() { ColumnSpacing = 6 };
    private readonly StackPanel _planSuggestion = new() { Spacing = 6, Visibility = Visibility.Collapsed };
    public string Mode => _mode.SelectedItem as string ?? "Execute";
    public void SetModelSelectors(ComboBox model, ComboBox effort)
    {
        model.MinWidth = 0;
        model.HorizontalAlignment = HorizontalAlignment.Stretch;
        effort.MinWidth = 82;
        effort.HorizontalAlignment = HorizontalAlignment.Stretch;
        var slot = new Grid { ColumnSpacing = 6 };
        slot.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        slot.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        slot.Children.Add(model);
        Grid.SetColumn(effort, 1);
        slot.Children.Add(effort);
        Grid.SetColumn(slot, 1);
        _composerFooter.Children.Add(slot);
    }
    private readonly List<ChatAttachment> _attachments = new();
    private readonly StackPanel _attachmentTray = new() { Orientation = Orientation.Horizontal, Spacing = 6 };
    private readonly TextBlock _pasteError = Design.Caption("");
    private readonly HashSet<string> _expandedAttachments = new();
    private bool _pasting;
    private readonly TextBox _composer = new()
    {
        AcceptsReturn = true,
        IsSpellCheckEnabled = false,
        IsTextPredictionEnabled = false,
        TextWrapping = TextWrapping.Wrap,
        MinHeight = 40,
        MaxHeight = 148,
        PlaceholderText = "Message Scylla…",
        Padding = new Thickness(Design.GapSm, Design.GapSm, Design.GapXs, Design.GapSm),
        VerticalAlignment = VerticalAlignment.Center,
    };
    private readonly Button _send;
    private readonly Button _attach;
    private readonly FontIcon _sendGlyph = Design.Icon("arrow");

    private readonly List<HistoryRow> _renderedHistory = new();
    private string _renderedThread = "";
    private string _renderedDay = "";
    internal int MessageRenderCount { get; private set; }
    private bool _generating;
    private readonly HashSet<string> _openThreadIds = new(StringComparer.Ordinal);
    private string _activeThreadId = "";
    private List<ThreadRow> _threads = new();
    private string _lastTabsKey = "";

    public AgentPane(Window window)
    {
        _window = window;
        _openPlan.Click += (_, _) => OpenPlanRequested?.Invoke(this, _lastPlanPath);
        Background = ThemeColors.Brush(ThemeColors.Surface);

        _activityCard = Design.Card(_activity, new Thickness(Design.Gap, Design.GapSm, Design.Gap, Design.GapSm));
        _activityCard.Visibility = Visibility.Collapsed;

        _send = BuildSendButton();
        _attach = Design.IconButton("plus", "", "Attach file", () => { });
        _attach.Click += async (_, _) => await AttachFileAsync();
        _composerBox = BuildComposer();
        _chatTabScroll.Content = _chatTabs;

        _transcript.Children.Add(_empty);
        _scroll.Content = _transcript;
        _scroll.LayoutUpdated += (_, _) =>
        {
            if (!_scrollPending || !IsLoaded) return;
            _scrollPending = false;
            // Wait for normal layout instead of recursively laying out WinUI
            // and native editor children from the generation polling callback.
            _scroll.ChangeView(null, _scroll.ScrollableHeight, null, true);
        };

        var root = new Grid { Padding = Design.PanePad, RowSpacing = Design.Gap };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Grid.SetRow(_chatTabScroll, 0);
        Grid.SetRow(_scroll, 1);
        Grid.SetRow(_activityCard, 2);
        Grid.SetRow(_composerBox, 3);
        root.Children.Add(_chatTabScroll);
        root.Children.Add(_scroll);
        root.Children.Add(_activityCard);
        root.Children.Add(_composerBox);
        Content = root;
        RefreshChatTabs();
    }

    public void ApplySnapshot(SessionSnapshot snap, bool verboseAgentProgress = false)
    {
        _lastPlanPath = snap.LastPlanPath;
        _openPlan.Visibility = _lastPlanPath.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        _generating = string.Equals(snap.State, "generating", StringComparison.OrdinalIgnoreCase)
                      || string.Equals(snap.State, "awaiting_action", StringComparison.OrdinalIgnoreCase);
        SyncSendState();

        _threads = snap.Threads.ToList();
        _activeThreadId = snap.ActiveThreadId ?? "";
        if (!string.IsNullOrWhiteSpace(_activeThreadId))
            _openThreadIds.Add(_activeThreadId);

        var active = _threads.FirstOrDefault(t => t.Id == _activeThreadId);

        var activity = snap.Activity?.Trim() ?? "";
        _activity.Text = activity;
        _activityCard.Visibility = activity.Length == 0 ? Visibility.Collapsed : Visibility.Visible;

        RefreshChatTabs();

        var dayLabel = DayGrouping.Label(active?.LocalUpdated ?? DateTime.Now);
        // Public assistant deltas belong in the Chat Log while the turn is active.
        // The verbose preference remains available for richer activity presentation,
        // but must not gate the reply text itself.
        RenderTranscript(snap.DisplayHistory(true), dayLabel);
    }

    private static string ClampTitle(string title)
    {
        title = title.Replace('\r', ' ').Replace('\n', ' ').Trim();
        if (title.Length <= 48) return title;
        return title[..45] + "…";
    }

    private void RefreshChatTabs()
    {
        _openThreadIds.RemoveWhere(id => id != _activeThreadId && !_threads.Any(t => t.Id == id));
        var tabIds = _openThreadIds.ToList();
        if (string.IsNullOrWhiteSpace(_activeThreadId)) tabIds.Add("");
        var key = System.Text.Json.JsonSerializer.Serialize(new { active = _activeThreadId,
            tabs = tabIds.Select(id => new { id, name = _threads.FirstOrDefault(t => t.Id == id)?.Name }) });
        if (key == _lastTabsKey) return;
        _lastTabsKey = key;
        _chatTabs.Children.Clear();
        foreach (var id in tabIds)
        {
            var thread = _threads.FirstOrDefault(t => t.Id == id);
            if (thread is null && id != _activeThreadId)
            {
                _openThreadIds.Remove(id);
                continue;
            }
            var name = string.IsNullOrWhiteSpace(thread?.Name) ? "New Chat" : thread!.Name;
            var on = id == _activeThreadId;
            var label = new TextBlock
            {
                Text = ClampTitle(name),
                FontSize = 12,
                Foreground = ThemeColors.Brush(on ? ThemeColors.Text : ThemeColors.Muted),
                VerticalAlignment = VerticalAlignment.Center,
            };
            var close = new TextBlock
            {
                Text = "\u2715",
                FontSize = 10,
                Foreground = ThemeColors.Brush(ThemeColors.Muted),
                Margin = new Thickness(Design.GapSm, 0, 0, 0),
                VerticalAlignment = VerticalAlignment.Center,
                Visibility = id == _activeThreadId ? Visibility.Collapsed : Visibility.Visible,
            };
            var idCopy = id;
            close.PointerPressed += (_, e) =>
            {
                e.Handled = true;
                _openThreadIds.Remove(idCopy);
                RefreshChatTabs();
            };
            var tab = new Border
            {
                Height = 26,
                Padding = new Thickness(Design.Gap, 0, Design.GapSm, 0),
                CornerRadius = Design.RadiusSm,
                Background = ThemeColors.Brush(on ? ThemeColors.Elevated : ThemeColors.Transparent),
                BorderBrush = ThemeColors.Brush(on ? ThemeColors.BorderSubtle : ThemeColors.Transparent),
                BorderThickness = new Thickness(1),
                Child = new StackPanel { Orientation = Orientation.Horizontal, Children = { label, close } },
            };
            tab.PointerPressed += (_, _) =>
            {
                if (idCopy.Length > 0 && idCopy != _activeThreadId)
                    OpenThreadRequested?.Invoke(this, idCopy);
            };
            _chatTabs.Children.Add(tab);
        }

    }

    private void RenderTranscript(IReadOnlyList<HistoryRow> history, string dayLabel)
    {
        var sameChat = _renderedThread == _activeThreadId && _renderedDay == dayLabel;
        var prefix = 0;
        if (sameChat)
            while (prefix < history.Count && prefix < _renderedHistory.Count && SameMessage(history[prefix], _renderedHistory[prefix])) prefix++;
        if (sameChat && prefix == history.Count && prefix == _renderedHistory.Count) return;
        var follow = _scroll.ScrollableHeight - _scroll.VerticalOffset < 48;
        if (!sameChat || _renderedHistory.Count == 0)
        {
            _transcript.Children.Clear();
            _renderedHistory.Clear();
            _renderedThread = _activeThreadId;
            _renderedDay = dayLabel;
            if (history.Count > 0) _transcript.Children.Add(DayGrouping.Chip(dayLabel));
        }
        if (history.Count == 0)
        {
            _transcript.Children.Clear();
            _renderedHistory.Clear();
            _transcript.Children.Add(_empty);
            return;
        }
        // Keep completed message controls and their selection/attachment state alive.
        while (_transcript.Children.Count > prefix + 1) _transcript.Children.RemoveAt(_transcript.Children.Count - 1);
        if (_renderedHistory.Count > prefix) _renderedHistory.RemoveRange(prefix, _renderedHistory.Count - prefix);
        for (var i = prefix; i < history.Count; i++)
        {
            var message = new StackPanel();
            if (i > 0 && history[i].User != history[i - 1].User)
                message.Children.Add(Design.Divider(new Thickness(0, Design.Gap, 0, Design.Gap)));
            else if (i > 0)
                message.Children.Add(new Border { Height = Design.GapSm });
            message.Children.Add(MessageBlock(history[i]));
            _transcript.Children.Add(message);
            _renderedHistory.Add(history[i]);
            MessageRenderCount++;
        }
        _scrollPending = follow || !sameChat;
    }

    private static bool SameMessage(HistoryRow a, HistoryRow b) =>
        a.User == b.User && a.Text == b.Text && a.Attachments.Count == b.Attachments.Count &&
        a.Attachments.Zip(b.Attachments).All(pair => pair.First.Path == pair.Second.Path &&
            pair.First.Label == pair.Second.Label && pair.First.Image == pair.Second.Image);

    private FrameworkElement MessageBlock(HistoryRow row)
    {
        var label = new TextBlock
        {
            Text = row.User ? "You" : "Scylla",
            FontSize = 11,
            FontWeight = FontWeights.SemiBold,
            CharacterSpacing = 40,
            Foreground = ThemeColors.Brush(row.User ? ThemeColors.Muted : ThemeColors.Secondary),
        };
        FrameworkElement body = row.User
            ? new TextBlock
            {
                Text = row.Text,
                Foreground = ThemeColors.Brush(ThemeColors.Text),
                TextWrapping = TextWrapping.WrapWholeWords,
                IsTextSelectionEnabled = true,
                LineHeight = 20,
            }
            : MarkdownView.Render(row.Text, openLink: target => OpenLinkRequested?.Invoke(this, target));
        var stack = new StackPanel { Spacing = Design.GapXs };
        if (row.User) stack.Children.Add(label);
        stack.Children.Add(body);
        if (row.Attachments.Count > 0)
        {
            var key = _activeThreadId + System.Text.Json.JsonSerializer.Serialize(row.Attachments);
            var thumbnails = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
            var expander = new Expander
            {
                Header = $"{row.Attachments.Count} Attachments",
                HorizontalAlignment = HorizontalAlignment.Stretch,
                IsExpanded = _expandedAttachments.Contains(key),
                Content = new ScrollViewer
                {
                    Content = thumbnails,
                    HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
                    VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
                },
            };
            void LoadThumbnails()
            {
                if (thumbnails.Children.Count > 0) return;
                foreach (var attachment in row.Attachments)
                    thumbnails.Children.Add(ChatAttachments.Thumbnail(attachment, this));
            }
            expander.Expanding += (_, _) => { _expandedAttachments.Add(key); LoadThumbnails(); };
            expander.Collapsed += (_, _) => _expandedAttachments.Remove(key);
            if (expander.IsExpanded) LoadThumbnails();
            stack.Children.Add(expander);
        }
        if (!row.User) return stack;
        var card = Design.Card(stack, new Thickness(Design.Gap, Design.GapSm, Design.Gap, Design.GapSm));
        card.Background = ThemeColors.Brush(ThemeColors.Elevated);
        return card;
    }

    private Border BuildComposer()
    {
        Design.MakeTransparent(_composer);
        _composerFooter.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _composerFooter.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        _composerFooter.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _composerFooter.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _composerFooter.Children.Add(new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 4,
            Children = { _mode },
        });
        Grid.SetColumn(_attach, 2);
        _attach.Margin = new Thickness(0, 0, Design.GapXs, 0);
        _composerFooter.Children.Add(_attach);
        Grid.SetColumn(_send, 3);
        _composerFooter.Children.Add(_send);
        ToolTipService.SetToolTip(_mode, "Execute: make changes. Plan: research and save a Knowledge plan. Ask: explore and answer without edits.");
        _mode.SelectionChanged += (_, _) => _planSuggestion.Visibility = Visibility.Collapsed;
        _planSuggestion.Children.Add(Design.Caption("This request covers several changes. Switch to Plan first?"));
        _planSuggestion.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = {
            Design.GhostButton("Switch to Plan", () => { _mode.SelectedItem = "Plan"; EmitSend(true); }),
            Design.GhostButton("Execute now", () => EmitSend(true)),
        } });

        var box = new Border
        {
            Background = ThemeColors.Brush(ThemeColors.Input),
            BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle),
            BorderThickness = new Thickness(1),
            CornerRadius = Design.RadiusLg,
            Padding = new Thickness(Design.GapXs, Design.GapXs, Design.GapXs, Design.GapXs + 4),
            Child = new StackPanel
            {
                Spacing = Design.GapXs,
                Children =
                {
                    new ScrollViewer
                    {
                        Content = _attachmentTray,
                        HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
                        VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
                    },
                    _pasteError,
                    _planSuggestion,
                    _openPlan,
                    _composer,
                    _composerFooter,
                },
            },
        };

        _composer.GotFocus += (_, _) => box.BorderBrush = ThemeColors.Brush(ThemeColors.AmberDim);
        _composer.LostFocus += (_, _) => box.BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle);
        _composer.TextChanged += (_, _) => {
            if (string.IsNullOrWhiteSpace(_composer.Text)) ClearComposerError();
            SyncSendState();
        };
        _pasteError.Visibility = Visibility.Collapsed;
        _composer.Paste += (_, e) =>
        {
            try
            {
                var clipboard = Windows.ApplicationModel.DataTransfer.Clipboard.GetContent();
                if (!clipboard.Contains(Windows.ApplicationModel.DataTransfer.StandardDataFormats.Bitmap)) return;
                e.Handled = true;
                _ = PasteImageAsync(clipboard);
            }
            catch (Exception ex)
            {
                _pasteError.Text = "Could not paste image: " + ex.Message;
                _pasteError.Visibility = Visibility.Visible;
            }
        };
        // Handle the shortcut before the multiline TextBox consumes Enter.
        _composer.PreviewKeyDown += (_, e) =>
        {
            var ctrl = Microsoft.UI.Input.InputKeyboardSource
                .GetKeyStateForCurrentThread(Windows.System.VirtualKey.Control)
                .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
            if (ctrl && e.Key == Windows.System.VirtualKey.V)
            {
                try
                {
                    var clipboard = Windows.ApplicationModel.DataTransfer.Clipboard.GetContent();
                    if (clipboard.Contains(Windows.ApplicationModel.DataTransfer.StandardDataFormats.Bitmap))
                    {
                        e.Handled = true;
                        _ = PasteImageAsync(clipboard);
                    }
                }
                catch (Exception ex)
                {
                    _pasteError.Text = "Could not paste image: " + ex.Message;
                    _pasteError.Visibility = Visibility.Visible;
                }
                return;
            }
            if (!ctrl && e.Key == Windows.System.VirtualKey.Tab && _composer.SelectionLength == 0) {
                var caret = _composer.SelectionStart;
                var text = _composer.Text ?? "";
                var start = caret;
                while (start > 0 && !char.IsWhiteSpace(text[start - 1])) --start;
                if (start < caret && text[start] == '@' && !text.Substring(start, caret - start).Contains('"')) {
                    e.Handled = true;
                    _ = ResolveFileMentionAsync(text, start, caret);
                }
                return;
            }
            if (e.Key != Windows.System.VirtualKey.Enter) return;
            if (!ctrl) return; // Enter inserts newline; Ctrl+Enter sends
            e.Handled = true;
            if (_generating) return;
            EmitSend();
        };

        SyncSendState();
        return box;
    }

    private async System.Threading.Tasks.Task ResolveFileMentionAsync(string text, int start, int caret)
    {
        try {
            var query = text.Substring(start + 1, caret - start - 1);
            var json = await System.Threading.Tasks.Task.Run(() => NativeCore.FileMentions(query));
            if (_composer.Text != text || _composer.SelectionStart != caret) return;
            using var data = System.Text.Json.JsonDocument.Parse(json ?? "[]");
            var paths = data.RootElement.EnumerateArray().Select(p => p.GetString() ?? "").Where(p => p.Length > 0).ToList();
            var exact = paths.Where(p => string.Equals(System.IO.Path.GetFileName(p), query, StringComparison.OrdinalIgnoreCase)).ToList();
            if (exact.Count > 0) paths = exact;
            void Insert(string path) {
                if (_composer.Text != text || _composer.SelectionStart != caret) return;
                _pasteError.Visibility = Visibility.Collapsed;
                var replacement = "@\"" + path + "\" ";
                _composer.Text = text.Substring(0, start) + replacement + text.Substring(caret);
                _composer.SelectionStart = start + replacement.Length;
                _composer.Focus(FocusState.Programmatic);
            }
            if (paths.Count == 1) Insert(paths[0]);
            else if (paths.Count > 1) {
                var choices = new MenuFlyout();
                foreach (var path in paths) {
                    var item = new MenuFlyoutItem { Text = path };
                    item.Click += (_, _) => Insert(path);
                    choices.Items.Add(item);
                }
                choices.ShowAt(_composer);
            } else {
                _pasteError.Text = "No matching file in the open projects or enabled Knowledge folders.";
                _pasteError.Visibility = Visibility.Visible;
            }
        } catch (Exception ex) {
            if (_composer.Text != text || _composer.SelectionStart != caret) return;
            _pasteError.Text = "Could not resolve file: " + ex.Message;
            _pasteError.Visibility = Visibility.Visible;
        }
    }

    private void ClearComposerError()
    {
        _pasteError.Text = "";
        _pasteError.Visibility = Visibility.Collapsed;
    }

    private Button BuildSendButton()
    {
        var b = new Button
        {
            Width = 28,
            Height = 28,
            Padding = new Thickness(0),
            CornerRadius = new CornerRadius(16),
            BorderThickness = new Thickness(0),
            VerticalAlignment = VerticalAlignment.Center,
            Content = _sendGlyph,
        };
        ToolTipService.SetToolTip(b, "Send  ·  Ctrl+Enter");
        b.Click += (_, _) =>
        {
            if (_generating) StopRequested?.Invoke(this, EventArgs.Empty);
            else EmitSend();
        };
        return b;
    }

    private void SyncSendState()
    {
        _mode.IsEnabled = !_generating;
        _attach.IsEnabled = !_generating && !_pasting;
        if (_generating)
        {
            _sendGlyph.Glyph = Design.Icon("stop").Glyph;
            _sendGlyph.Foreground = ThemeColors.Brush(ThemeColors.OnAmber);
            _send.Background = ThemeColors.Brush(ThemeColors.Amber);
            _send.IsEnabled = true;
            ToolTipService.SetToolTip(_send, "Stop generation");
            return;
        }

        _sendGlyph.Glyph = Design.Icon("arrow").Glyph;
        var ready = !_pasting && (!string.IsNullOrWhiteSpace(_composer.Text) || _attachments.Count > 0);
        var fill = ThemeColors.Brush(ready ? ThemeColors.Amber : ThemeColors.Elevated);
        _send.Background = fill;
        _sendGlyph.Foreground = ThemeColors.Brush(ready ? ThemeColors.OnAmber : ThemeColors.Muted);
        // White glyph on orange when armed — OnAmber is dark; use white when ready.
        if (ready) _sendGlyph.Foreground = new SolidColorBrush(Microsoft.UI.Colors.White);
        _send.IsEnabled = ready;
        ToolTipService.SetToolTip(_send, "Send  ·  Ctrl+Enter");
    }

    private void EmitSend(bool modeConfirmed = false)
    {
        if (_pasting || _generating) return;
        var text = _composer.Text?.Trim() ?? "";
        if (text.Length == 0 && _attachments.Count == 0) return;
        if (!modeConfirmed && Mode == "Execute" && text.Length > 600
            && text.Split('\n').Count(line => line.TrimStart().StartsWith('[')
                || System.Text.RegularExpressions.Regex.IsMatch(line, @"^\s*(?:\d+[.)]|[-*])\s")) >= 3)
        {
            _planSuggestion.Visibility = Visibility.Visible;
            return;
        }
        _planSuggestion.Visibility = Visibility.Collapsed;
        var request = new ChatSendRequest { Payload = ChatAttachments.Payload(text, _attachments), Mode = Mode };
        SendRequested?.Invoke(this, request);
        if (!request.Accepted) return;
        ClearComposerError();
        _composer.Text = "";
        _attachments.Clear();
        RefreshAttachments();
        SyncSendState();
    }

    private void RefreshAttachments()
    {
        _attachmentTray.Children.Clear();
        foreach (var attachment in _attachments)
        {
            var remove = Design.GhostButton("Remove", () =>
            {
                _attachments.Remove(attachment);
                RefreshAttachments();
                SyncSendState();
            });
            _attachmentTray.Children.Add(new StackPanel
            {
                Spacing = 2,
                Children = { ChatAttachments.Thumbnail(attachment, this), remove },
            });
        }
    }

    private async Task PasteImageAsync(Windows.ApplicationModel.DataTransfer.DataPackageView clipboard)
    {
        if (_pasting) return;
        _pasting = true;
        SyncSendState();
        try
        {
            _attachments.Add(await ChatAttachments.SaveBitmapAsync(clipboard));
            _pasteError.Visibility = Visibility.Collapsed;
            RefreshAttachments();
        }
        catch (Exception ex)
        {
            _pasteError.Text = "Could not paste image: " + ex.Message;
            _pasteError.Visibility = Visibility.Visible;
        }
        finally { _pasting = false; SyncSendState(); }
    }

    private async Task AttachFileAsync()
    {
        if (_pasting || _generating) return;
        _pasting = true;
        SyncSendState();
        try
        {
            var picker = new FileOpenPicker();
            InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(_window));
            picker.FileTypeFilter.Add("*");
            var file = await picker.PickSingleFileAsync();
            if (file == null) return;
            _attachments.Add(await ChatAttachments.SaveFileAsync(file));
            ClearComposerError();
            RefreshAttachments();
        }
        catch (Exception ex)
        {
            _pasteError.Text = "Could not attach file: " + ex.Message;
            _pasteError.Visibility = Visibility.Visible;
        }
        finally { _pasting = false; SyncSendState(); }
    }
}

/// <summary>Day bucketing shared by the transcript chip and the chat history list.</summary>
internal static class DayGrouping
{
    public static string Label(DateTime when)
    {
        if (when == DateTime.MinValue) return "Recent";
        var today = DateTime.Today;
        var day = when.Date;
        if (day == today) return "Today";
        if (day == today.AddDays(-1)) return "Yesterday";
        if (day > today.AddDays(-7)) return when.ToString("dddd");
        if (day.Year == today.Year) return when.ToString("MMMM d");
        return when.ToString("MMMM d, yyyy");
    }

    public static FrameworkElement Chip(string label)
    {
        var grid = new Grid { Margin = new Thickness(0, 0, 0, Design.Gap), ColumnSpacing = Design.Gap };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        var text = new TextBlock
        {
            Text = label,
            FontSize = 11,
            CharacterSpacing = 60,
            Foreground = ThemeColors.Brush(ThemeColors.Muted),
            VerticalAlignment = VerticalAlignment.Center,
        };
        var left = Design.Divider(new Thickness(0));
        var right = Design.Divider(new Thickness(0));
        left.VerticalAlignment = VerticalAlignment.Center;
        right.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(left, 0);
        Grid.SetColumn(text, 1);
        Grid.SetColumn(right, 2);
        grid.Children.Add(left);
        grid.Children.Add(text);
        grid.Children.Add(right);
        return grid;
    }

    public static FrameworkElement Header(string label) => new TextBlock
    {
        Text = label,
        FontSize = 11,
        FontWeight = FontWeights.SemiBold,
        CharacterSpacing = 60,
        Foreground = ThemeColors.Brush(ThemeColors.Muted),
        Margin = new Thickness(Design.GapXs, Design.Gap, 0, Design.GapXs),
    };
}
