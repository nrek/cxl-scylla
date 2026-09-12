using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace Scylla;

internal sealed class FilesPane : UserControl
{
    public event EventHandler<string>? FileActivated;
    public event Action<string, string?>? PathChanged;
    public event EventHandler? ManageKnowledgeClicked;
    public event EventHandler? ManageFilesClicked;
    public event EventHandler? StrataClicked;

    private readonly TreeView _tree = new() { SelectionMode = TreeViewSelectionMode.Single };
    private readonly TreeView _knowledgeTree = new()
    {
        SelectionMode = TreeViewSelectionMode.Single,
        MaxHeight = 320,
    };
    private readonly TextBox _filter = Design.Field("Filter");
    private readonly Border _knowledgeBody;
    private readonly Button _knowledgeToggle;
    private readonly Grid _browser = new() { Padding = Design.PanePad, RowSpacing = Design.Gap };
    private readonly RowDefinition _filesRow = new() { Height = new GridLength(1, GridUnitType.Star) };
    private readonly RowDefinition _knowledgeRow = new() { Height = GridLength.Auto };
    private readonly Button _filesHeader;
    private readonly Button _knowledgeHeader;
    private string _root = "";
    private List<string> _roots = new();

    public FilesPane()
    {
        Background = ThemeColors.Brush(ThemeColors.Panel);

        var root = _browser;
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(_filesRow);
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(_knowledgeRow);

        _filesHeader = Design.GhostButton("▾ FILES", () => FocusKnowledge(false));
        _knowledgeHeader = Design.GhostButton("▸ KNOWLEDGE", () => FocusKnowledge(true));
        var head = Header(_filesHeader, Design.IconButton("cog", "", "Manage files", () => ManageFilesClicked?.Invoke(this, EventArgs.Empty)));
        Grid.SetRow(head, 0);

        _tree.Margin = new Thickness(-Design.GapSm, 0, 0, 0);
        var filesBody = new Grid { RowSpacing = Design.GapSm };
        filesBody.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        filesBody.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        filesBody.Children.Add(Design.IconField(_filter, "filter"));
        Grid.SetRow(_tree, 1);
        filesBody.Children.Add(_tree);
        Grid.SetRow(filesBody, 1);

        _knowledgeToggle = Design.IconButton("cog", "", "Manage knowledge", () => ManageKnowledgeClicked?.Invoke(this, EventArgs.Empty));
        _knowledgeBody = new Border
        {
            Background = ThemeColors.Brush(ThemeColors.Transparent),
            BorderThickness = new Thickness(0),
            Padding = new Thickness(0, Design.GapSm, 0, Design.GapSm),
            Child = Design.Caption("No knowledge sources yet. Add folders with Manage."),
        };

        var knowledge = Header(_knowledgeHeader, _knowledgeToggle);
        knowledge.BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle);
        knowledge.BorderThickness = new Thickness(0, 1, 0, 0);
        Grid.SetRow(knowledge, 2);
        Grid.SetRow(_knowledgeBody, 3);

        _tree.RightTapped += OnTreeRightTapped;
        _knowledgeTree.RightTapped += OnTreeRightTapped;
        _tree.ItemInvoked += OnItemInvoked;
        _knowledgeTree.ItemInvoked += OnItemInvoked;
        _tree.Expanding += OnExpanding;
        _knowledgeTree.Expanding += OnExpanding;
        _filter.TextChanged += (_, _) => ReloadVisible();

        root.Children.Add(head);
        root.Children.Add(filesBody);
        root.Children.Add(knowledge);
        root.Children.Add(_knowledgeBody);
        Content = root;
        RefreshKnowledge();
    }

    private static Grid Header(Button title, Button manage)
    {
        var bar = new Grid();
        bar.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        bar.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        title.BorderThickness = new Thickness(0);
        title.HorizontalAlignment = HorizontalAlignment.Left;
        Grid.SetColumn(manage, 1);
        bar.Children.Add(title);
        bar.Children.Add(manage);
        return bar;
    }

    private void FocusKnowledge(bool knowledge)
    {
        _filesHeader.Content = knowledge ? "▸ FILES" : "▾ FILES";
        _knowledgeHeader.Content = knowledge ? "▾ KNOWLEDGE" : "▸ KNOWLEDGE";
        _filesRow.Height = knowledge ? new GridLength(0) : new GridLength(1, GridUnitType.Star);
        _knowledgeRow.Height = knowledge ? new GridLength(1, GridUnitType.Star) : GridLength.Auto;
        _browser.Children[1].Visibility = knowledge ? Visibility.Collapsed : Visibility.Visible;
        _knowledgeTree.MaxHeight = knowledge ? double.PositiveInfinity : 320;
    }

    public void LoadProject(string? path)
    {
        _root = path?.Trim() ?? "";
        ReloadRoots();
        ReloadVisible();
        RefreshKnowledge();
    }

    public void ReloadRoots()
    {
        _roots.Clear();
        try
        {
            foreach (var r in ProjectRootRow.Parse(NativeCore.ProjectRootsJson()))
            {
                if (!string.IsNullOrWhiteSpace(r.Path)) _roots.Add(r.Path);
            }
        }
        catch { /* ignore */ }
        if (_roots.Count == 0 && !string.IsNullOrWhiteSpace(_root))
            _roots.Add(_root);
        _roots = _roots.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
    }

    public void RefreshKnowledge()
    {
        try
        {
            var prefs = BrowserPreferences.Load();
            var rows = KnowledgeRow.ParseList(NativeCore.KnowledgeJson())
                .Where(k => !string.IsNullOrWhiteSpace(k.Path)
                    && !prefs.HiddenKnowledgePaths.Contains(k.Path, StringComparer.OrdinalIgnoreCase))
                .ToList();
            _knowledgeTree.RootNodes.Clear();
            // TreeViewNode string content is intentional: arbitrary XAML content is coerced to
            // its CLR type name by this WinUI TreeView template ("Microsoft.UI.Xaml...").
            var strataNode = new TreeViewNode { Content = "Strata" };
            strataNode.SetValue(FrameworkElement.TagProperty, "strata://app");
            _knowledgeTree.RootNodes.Add(strataNode);
            foreach (var k in rows)
            {
                _knowledgeTree.RootNodes.Add(MakeNode(new DirEntry
                {
                    Name = string.IsNullOrWhiteSpace(k.Label) ? k.Path : k.Label,
                    Path = k.Path,
                    IsDir = true,
                }));
            }
            _knowledgeBody.Child = _knowledgeTree;
        }
        catch (Exception ex)
        {
            _knowledgeBody.Child = Design.Caption(ex.Message);
        }
    }

    private void ReloadVisible()
    {
        _tree.RootNodes.Clear();
        ReloadRoots();
        if (_roots.Count == 0)
        {
            _tree.RootNodes.Add(new TreeViewNode { Content = "Open a project folder" });
            return;
        }
        try
        {
            var filter = _filter.Text?.Trim() ?? "";
            foreach (var rootPath in _roots)
            {
                var container = new TreeViewNode
                {
                    Content = System.IO.Path.GetFileName(rootPath.TrimEnd('\\', '/')) + "  ·  root",
                    IsExpanded = true,
                };
                container.SetValue(FrameworkElement.TagProperty, new DirEntry { Name = rootPath, Path = rootPath, IsDir = true });
                _tree.RootNodes.Add(container);

                foreach (var e in DirEntry.ParseList(NativeCore.ListDir(rootPath)))
                {
                    if (filter.Length > 0 && e.Name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) < 0)
                        continue;
                    var node = MakeNode(e);
                    container.Children.Add(node);
                }
            }
        }
        catch (Exception ex)
        {
            _tree.RootNodes.Add(new TreeViewNode { Content = ex.Message });
        }
    }

    private static TreeViewNode MakeNode(DirEntry e)
    {
        var node = new TreeViewNode { Content = e.Name, HasUnrealizedChildren = e.IsDir };
        node.SetValue(FrameworkElement.TagProperty, e);
        return node;
    }

    private void ExpandLazy(TreeViewNode node, string path)
    {
        node.Children.Clear();
        try
        {
            foreach (var e in DirEntry.ParseList(NativeCore.ListDir(path)))
                node.Children.Add(MakeNode(e));
            node.HasUnrealizedChildren = false;
        }
        catch
        {
            // unreadable directory — leave the node collapsed
        }
    }

    private void OnExpanding(TreeView sender, TreeViewExpandingEventArgs args)
    {
        if (args.Node.HasUnrealizedChildren
            && args.Node.GetValue(FrameworkElement.TagProperty) is DirEntry e)
            ExpandLazy(args.Node, e.Path);
    }

    private void OnTreeRightTapped(object sender, RightTappedRoutedEventArgs args)
    {
        if (sender is not TreeView tree) return;
        DependencyObject? target = args.OriginalSource as DependencyObject;
        while (target is not null && target is not TreeViewItem)
            target = VisualTreeHelper.GetParent(target);
        var node = target is TreeViewItem item ? tree.NodeFromContainer(item) : null;
        var entry = node?.GetValue(FrameworkElement.TagProperty) as DirEntry;
        if (entry is null && tree == _tree && _roots.Count == 1)
            entry = new DirEntry { Name = _roots[0], Path = _roots[0], IsDir = true };
        if (entry is null && tree != _tree) return;
        args.Handled = true;
        var menu = entry is null ? new MenuFlyout() : FileActions.Menu(this, entry, path => {
            if (System.IO.Directory.Exists(path) && node != null) {
                ExpandLazy(node, path); node.IsExpanded = true;
            } else if (System.IO.File.Exists(path)) FileActivated?.Invoke(this, path);
        }, () => { ReloadVisible(); RefreshKnowledge(); }, (oldPath, newPath) => PathChanged?.Invoke(oldPath, newPath));
        if (tree == _tree)
        {
            var manage = new MenuFlyoutItem { Text = "Manage Files" };
            manage.Click += (_, _) => ManageFilesClicked?.Invoke(this, EventArgs.Empty);
            if (menu.Items.Count > 0) menu.Items.Insert(0, new MenuFlyoutSeparator());
            menu.Items.Insert(0, manage);
        }
        menu.ShowAt(tree, args.GetPosition(tree));
    }
    private void OnItemInvoked(TreeView sender, TreeViewItemInvokedEventArgs args)
    {
        if (args.InvokedItem is not TreeViewNode node) return;
        if (node.GetValue(FrameworkElement.TagProperty) is string special && special == "strata://app")
        {
            StrataClicked?.Invoke(this, EventArgs.Empty);
            return;
        }
        if (node.GetValue(FrameworkElement.TagProperty) is not DirEntry e) return;
        if (e.IsDir)
        {
            if (node.HasUnrealizedChildren) ExpandLazy(node, e.Path);
            node.IsExpanded = true;
            return;
        }
        FileActivated?.Invoke(this, e.Path);
    }
}

internal sealed class HistoryPane : UserControl
{
    private readonly ChatHistoryState _projects = new();
    private readonly StackPanel _projectList = new() { Spacing = 2 };
    private readonly ScrollViewer _drawer = new() { MaxHeight = 240, Visibility = Visibility.Collapsed,
        HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
    private readonly Button _drawerToggle;
    public event EventHandler<string>? ThreadSelected;
    public event EventHandler<string>? DeleteThreadRequested;
    public event EventHandler? NewChatRequested;

    private readonly TextBox _search = Design.Field("Search");
    private readonly ScrollViewer _scroll = new()
    {
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
    };
    private readonly StackPanel _list = new() { Spacing = 2 };
    private readonly Dictionary<string, Border> _rows = new();

    private List<ThreadRow> _all = new();
    private string _selectedId = "";
    private Border? _hoveredRow;
    private List<(string Id, string Name, string Preview, string Day, string Projects, string Status)>? _renderedRows;
    private bool _renderedSearchEmpty;

    public HistoryPane()
    {
        Background = ThemeColors.Brush(ThemeColors.Panel);

        var root = new Grid { Padding = Design.PanePad, RowSpacing = Design.Gap };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });

        var header = new Grid { Height = Design.RowH, ColumnSpacing = Design.GapSm };
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _drawerToggle = Design.GhostButton("▸ CHATS", () =>
        {
            _drawer.Visibility = _drawer.Visibility == Visibility.Collapsed ? Visibility.Visible : Visibility.Collapsed;
            UpdateHeading();
        });
        _drawerToggle.BorderThickness = new Thickness(0);
        var title = _drawerToggle;
        title.VerticalAlignment = VerticalAlignment.Center;
        var newChat = Design.IconButton("plus", "Chat", "New chat", () => NewChatRequested?.Invoke(this, EventArgs.Empty));
        newChat.HorizontalAlignment = HorizontalAlignment.Right;
        newChat.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(newChat, 1);
        header.Children.Add(title);
        header.Children.Add(newChat);
        _drawer.Content = _projectList;
        var head = new StackPanel { Spacing = Design.GapSm, Children = { header, _drawer, Design.IconField(_search, "search") } };
        Grid.SetRow(head, 0);

        _scroll.Content = _list;
        Grid.SetRow(_scroll, 1);

        _search.TextChanged += (_, _) => Rebuild();
        root.Children.Add(head);
        root.Children.Add(_scroll);
        Content = root;
    }

    public void ApplySnapshot(SessionSnapshot snap)
    {
        if (_projects.SetRoots(snap.ProjectRoots)) RebuildProjects();
        _projects.Observe(snap.Threads, snap.ActiveThreadId);
        _all = snap.Threads.ToList();
        _selectedId = snap.ActiveThreadId;
        Rebuild();
    }

    private void UpdateHeading()
    {
        _drawerToggle.Content = (_drawer.Visibility == Visibility.Visible ? "▾ " : "▸ ") + _projects.Heading;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(_drawerToggle,
            _projects.Heading + (_drawer.Visibility == Visibility.Visible ? ", expanded" : ", collapsed"));
    }

    private FrameworkElement ProjectDot(string path)
    {
        var color = _projects.Color(path);
        var dot = new Microsoft.UI.Xaml.Shapes.Ellipse { Width = 8, Height = 8,
            VerticalAlignment = VerticalAlignment.Center,
            Fill = new SolidColorBrush(Windows.UI.Color.FromArgb(255, (byte)(color >> 16), (byte)(color >> 8), (byte)color)) };
        ToolTipService.SetToolTip(dot, path);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(dot, System.IO.Path.GetFileName(path.TrimEnd('\\', '/')));
        return dot;
    }

    private void RebuildProjects()
    {
        _renderedRows = null;
        _projectList.Children.Clear();
        foreach (var path in _projects.Roots)
        {
            var label = new TextBlock { Text = System.IO.Path.GetFileName(path.TrimEnd('\\', '/')),
                TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center };
            var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8,
                Children = { ProjectDot(path), label } };
            var check = new CheckBox { Content = content, IsChecked = _projects.Selected.Contains(path), MinHeight = 30 };
            ToolTipService.SetToolTip(check, path);
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(check, label.Text);
            void Changed() { if (check.IsChecked == true) _projects.Selected.Add(path); else _projects.Selected.Remove(path); _renderedRows = null; UpdateHeading(); Rebuild(); }
            check.Checked += (_, _) => Changed();
            check.Unchecked += (_, _) => Changed();
            _projectList.Children.Add(check);
        }
        if (_projects.Roots.Count == 0) _projectList.Children.Add(Design.Caption("Add project folders in Files."));
        UpdateHeading();
    }

    private void Rebuild()
    {
        var q = _search.Text?.Trim() ?? "";
        var scoped = _all.Where(t => _projects.Matches(t).Any()).ToList();
        var filtered = q.Length == 0
            ? scoped
            : scoped.Where(t =>
                t.Name.Contains(q, StringComparison.OrdinalIgnoreCase) ||
                t.Preview.Contains(q, StringComparison.OrdinalIgnoreCase)).ToList();

        // Polling runs every 250 ms. Keep existing controls (and pointer state)
        // when the visible rows and date groups have not changed.
        var ordered = filtered.OrderByDescending(t => t.UpdatedAt).ToList();
        var renderedRows = ordered.Select(t => (t.Id, t.Name, t.Preview, DayGrouping.Label(t.LocalUpdated),
            string.Join("|", _projects.Matches(t)), _projects.Status(t))).ToList();
        if (_renderedRows is not null && _renderedRows.SequenceEqual(renderedRows)
            && _renderedSearchEmpty == (q.Length == 0))
        {
            Highlight(_selectedId);
            return;
        }
        _renderedRows = renderedRows;
        _renderedSearchEmpty = q.Length == 0;
        _hoveredRow = null;
        _list.Children.Clear();
        _rows.Clear();

        if (filtered.Count == 0)
        {
            _list.Children.Add(Design.Caption(_projects.Selected.Count == 0 ? "Select project folders in CHATS to see their history."
                : q.Length == 0 ? "No chats for the selected projects." : "No chats match that search."));
            return;
        }

        // Newest first, bucketed into Today / Yesterday / weekday / date.
        var currentDay = "";
        foreach (var t in ordered)
        {
            var day = DayGrouping.Label(t.LocalUpdated);
            if (day != currentDay)
            {
                currentDay = day;
                _list.Children.Add(DayGrouping.Header(day));
            }
            var row = ThreadRowView(t);
            _rows[t.Id] = row;
            _list.Children.Add(row);
        }

        Highlight(_selectedId);
    }

    private Border ThreadRowView(ThreadRow t)
    {
        var nameText = string.IsNullOrWhiteSpace(t.Name) ? "Untitled chat" : t.Name.Trim();
        var previewText = (t.Preview ?? "").Trim();
        if (string.Equals(previewText, nameText, StringComparison.OrdinalIgnoreCase))
            previewText = "";
        var name = new TextBlock
        {
            Text = nameText,
            FontSize = 13,
            FontWeight = FontWeights.SemiBold,
            Foreground = ThemeColors.Brush(ThemeColors.Text),
            TextTrimming = TextTrimming.CharacterEllipsis,
            MaxLines = 1,
        };
        var titleRow = new Grid { ColumnSpacing = 8 };
        titleRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        titleRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        titleRow.Children.Add(name);
        var dots = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, VerticalAlignment = VerticalAlignment.Center };
        foreach (var path in _projects.Matches(t)) dots.Children.Add(ProjectDot(path));
        Grid.SetColumn(dots, 1);
        titleRow.Children.Add(dots);
        var children = new List<UIElement> { titleRow };
        var status = _projects.Status(t);
        if (status.Length > 0)
        {
            var badge = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
            if (t.Busy) badge.Children.Add(new ProgressRing { IsActive = true, Width = 12, Height = 12, MinWidth = 12, MinHeight = 12 });
            badge.Children.Add(new TextBlock { Text = (t.Busy ? "" : status == "Completed" ? "✓ " : "! ") + status,
                FontSize = 11, Foreground = ThemeColors.Brush(ThemeColors.Text) });
            children.Add(badge);
        }
        if (previewText.Length > 0)
        {
            children.Add(new TextBlock
            {
                Text = previewText,
                FontSize = 12,
                Foreground = ThemeColors.Brush(ThemeColors.Muted),
                TextTrimming = TextTrimming.CharacterEllipsis,
                TextWrapping = TextWrapping.WrapWholeWords,
                MaxLines = 2,
            });
        }

        var row = new Border
        {
            CornerRadius = Design.RadiusSm,
            Padding = new Thickness(Design.GapSm, Design.GapSm, Design.GapSm, Design.GapSm),
            Background = ThemeColors.Brush(ThemeColors.Transparent),
            Child = new StackPanel { Spacing = 2, Children = { } },
            Tag = t.Id,
        };
        if (row.Child is StackPanel sp)
        {
            foreach (var c in children) sp.Children.Add(c);
        }
        row.PointerEntered += (_, _) =>
        {
            _hoveredRow = row;
            if (t.Id != _selectedId) row.Background = ThemeColors.Brush(ThemeColors.Surface);
        };
        row.PointerExited += (_, _) =>
        {
            if (_hoveredRow == row) _hoveredRow = null;
            if (t.Id != _selectedId) row.Background = ThemeColors.Brush(ThemeColors.Transparent);
        };
        row.PointerPressed += (_, args) =>
        {
            if (!args.GetCurrentPoint(row).Properties.IsLeftButtonPressed) return;
            _selectedId = t.Id;
            _projects.Acknowledge(t.Id);
            Highlight(t.Id);
            ThreadSelected?.Invoke(this, t.Id);
            Rebuild();
        };
        var menu = new MenuFlyout();
        var delete = new MenuFlyoutItem { Text = "Delete Chat…" };
        delete.Click += async (_, _) =>
        {
            var dialog = new ContentDialog { XamlRoot = XamlRoot, Title = "Delete chat?",
                Content = $"Remove “{nameText}” from local chat history?",
                PrimaryButtonText = "Delete", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close };
            if (await dialog.ShowAsync() == ContentDialogResult.Primary)
                DeleteThreadRequested?.Invoke(this, t.Id);
        };
        menu.Items.Add(delete);
        row.ContextFlyout = menu;
        return row;
    }

    private void Highlight(string id)
    {
        foreach (var (key, row) in _rows)
        {
            var on = key == id && !string.IsNullOrEmpty(id);
            row.Background = ThemeColors.Brush(on ? ThemeColors.Elevated
                : row == _hoveredRow ? ThemeColors.Surface : ThemeColors.Transparent);
            row.BorderBrush = ThemeColors.Brush(on ? ThemeColors.BorderSubtle : ThemeColors.Transparent);
            row.BorderThickness = new Thickness(1);
        }
    }
}
