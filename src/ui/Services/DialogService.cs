using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace Scylla.UI.Services;

public sealed class DialogService
{
    public string? PickExecutable()
    {
        var dlg = new OpenFileDialog
        {
            Filter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*",
            Title = "Select application",
        };
        return dlg.ShowDialog() == true ? dlg.FileName : null;
    }

    public IReadOnlyList<string> PickFolders()
    {
        var dlg = new OpenFolderDialog
        {
            Title = "Select folder grants",
            Multiselect = true,
        };
        if (dlg.ShowDialog() != true)
        {
            return Array.Empty<string>();
        }
        return dlg.FolderNames.Where(s => !string.IsNullOrWhiteSpace(s)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
    }

    public bool Confirm(string title, string body)
    {
        return MessageBox.Show(body, title, MessageBoxButton.OKCancel, MessageBoxImage.Warning) == MessageBoxResult.OK;
    }

    public void Info(string title, string body)
    {
        MessageBox.Show(body, title, MessageBoxButton.OK, MessageBoxImage.Information);
    }

    public void Error(string title, string body)
    {
        MessageBox.Show(body, title, MessageBoxButton.OK, MessageBoxImage.Warning);
    }

    public string? AskPassword(string title, string body)
    {
        string? result = null;
        var box = new PasswordBox
        {
            Margin = new Thickness(0, 8, 0, 0),
            Padding = new Thickness(6, 4, 6, 4),
        };
        var ok = new Button { Content = "RUN", Width = 88, IsDefault = true, Margin = new Thickness(8, 0, 0, 0) };
        var cancel = new Button { Content = "Cancel", Width = 88, IsCancel = true };
        var win = new Window
        {
            Title = title,
            Width = 420,
            SizeToContent = SizeToContent.Height,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Owner = System.Windows.Application.Current.MainWindow,
            ResizeMode = ResizeMode.NoResize,
            Topmost = true,
            ShowInTaskbar = true,
            Background = (System.Windows.Media.Brush)System.Windows.Application.Current.Resources["Bg"],
            Foreground = (System.Windows.Media.Brush)System.Windows.Application.Current.Resources["Fg"],
        };
        ok.Click += (_, _) =>
        {
            result = box.Password;
            win.DialogResult = true;
        };
        win.Content = new StackPanel
        {
            Margin = new Thickness(16),
            Children =
            {
                new TextBlock { Text = body, TextWrapping = TextWrapping.Wrap },
                box,
                new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    HorizontalAlignment = HorizontalAlignment.Right,
                    Margin = new Thickness(0, 16, 0, 0),
                    Children = { cancel, ok },
                },
            },
        };
        win.Loaded += (_, _) => box.Focus();
        return win.ShowDialog() == true ? result : null;
    }

    public GrantModeResult? AskGrantMode(IReadOnlyList<string> folders)
    {
        if (folders.Count == 0) return null;
        var dlg = new GrantModeWindow(folders);
        return dlg.ShowDialog() == true ? dlg.Result : null;
    }
}

public sealed class GrantModeResult
{
    public bool ReadWrite { get; init; }
}

public sealed class GrantModeWindow : Window
{
    public GrantModeResult? Result { get; private set; }

    public GrantModeWindow(IReadOnlyList<string> folders)
    {
        Title = "Folder Access";
        Width = 460;
        Height = folders.Count > 1 ? 280 : 220;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = (System.Windows.Media.Brush)System.Windows.Application.Current.Resources["Bg"];
        Foreground = (System.Windows.Media.Brush)System.Windows.Application.Current.Resources["Fg"];
        var ro = new RadioButton { Content = "Read Only", IsChecked = true, Foreground = Foreground, Margin = new Thickness(0, 6, 0, 0) };
        var rw = new RadioButton { Content = "Read + Write", Foreground = Foreground, Margin = new Thickness(0, 6, 0, 0) };
        var add = new Button { Content = "Add", Width = 80, IsDefault = true };
        var cancel = new Button { Content = "Cancel", Width = 80, IsCancel = true };
        add.Click += (_, _) =>
        {
            Result = new GrantModeResult { ReadWrite = rw.IsChecked == true };
            DialogResult = true;
        };
        Content = new StackPanel
        {
            Margin = new Thickness(16),
            Children =
            {
                new TextBlock
                {
                    Text = folders.Count == 1 ? folders[0] : string.Join("\n", folders),
                    TextWrapping = TextWrapping.Wrap,
                    MaxHeight = 90,
                    TextTrimming = TextTrimming.CharacterEllipsis,
                    Margin = new Thickness(0, 0, 0, 12),
                },
                ro, rw,
                new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    HorizontalAlignment = HorizontalAlignment.Right,
                    Margin = new Thickness(0, 16, 0, 0),
                    Children = { cancel, add },
                },
            },
        };
    }
}
