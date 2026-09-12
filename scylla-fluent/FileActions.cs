using System.Diagnostics;
using System.IO;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.ApplicationModel.DataTransfer;

namespace Scylla;

internal static class FileActions
{
    public static MenuFlyout Menu(FrameworkElement owner, DirEntry entry, Action<string> open, Action refresh, Action<string, string?> changed)
    {
        var menu = new MenuFlyout();
        void Add(string label, Func<Task> action)
        {
            var item = new MenuFlyoutItem { Text = label };
            item.Click += async (_, _) =>
            {
                try { await action(); }
                catch (Exception ex) { await Message(owner, "File operation failed", ex.Message); }
            };
            menu.Items.Add(item);
        }
        var folder = entry.IsDir ? entry.Path : Path.GetDirectoryName(entry.Path)!;
        Add(entry.IsDir ? "Open Folder" : "Open", () => { open(entry.Path); return Task.CompletedTask; });
        Add("Find in Folder", () => Find(owner, folder, open));
        menu.Items.Add(new MenuFlyoutSeparator());
        Add("New File…", () => Create(owner, folder, false, refresh));
        Add("New Folder…", () => Create(owner, folder, true, refresh));
        Add("Rename…", async () =>
        {
            var name = await Name(owner, "Rename", Path.GetFileName(entry.Path));
            if (name is null) return;
            var parent = Path.GetDirectoryName(Path.GetFullPath(entry.Path)) ?? throw new IOException("Cannot rename a drive root.");
            var target = Path.Combine(parent, name);
            if (entry.IsDir) Directory.Move(entry.Path, target); else File.Move(entry.Path, target);
            changed(entry.Path, target);
            refresh();
        });
        Add("Copy Path", () =>
        {
            var data = new DataPackage(); data.SetText(entry.Path); Clipboard.SetContent(data);
            return Task.CompletedTask;
        });
        Add("Reveal in Explorer", () =>
        {
            RevealInExplorer(entry.Path);
            return Task.CompletedTask;
        });
        Add("Open in Browser", () =>
        {
            OpenInBrowser(entry.Path);
            return Task.CompletedTask;
        });
        menu.Items.Add(new MenuFlyoutSeparator());
        Add("Delete…", async () =>
        {
            var dialog = new ContentDialog {
                XamlRoot = owner.XamlRoot, Title = "Delete " + entry.Name + "?",
                Content = "Move this " + (entry.IsDir ? "folder and its contents" : "file") + " to the Recycle Bin?\n\n" + entry.Path,
                PrimaryButtonText = "Delete", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close
            };
            if (await dialog.ShowAsync() != ContentDialogResult.Primary) return;
            if (Path.GetPathRoot(Path.GetFullPath(entry.Path)) == Path.GetFullPath(entry.Path))
                throw new IOException("Cannot delete a drive root.");
            if (entry.IsDir) Microsoft.VisualBasic.FileIO.FileSystem.DeleteDirectory(entry.Path,
                Microsoft.VisualBasic.FileIO.UIOption.OnlyErrorDialogs, Microsoft.VisualBasic.FileIO.RecycleOption.SendToRecycleBin);
            else Microsoft.VisualBasic.FileIO.FileSystem.DeleteFile(entry.Path,
                Microsoft.VisualBasic.FileIO.UIOption.OnlyErrorDialogs, Microsoft.VisualBasic.FileIO.RecycleOption.SendToRecycleBin);
            changed(entry.Path, null);
            refresh();
        });
        return menu;
    }

    public static void RevealInExplorer(string path)
    {
        Process.Start(new ProcessStartInfo("explorer.exe", "/select,\"" + Path.GetFullPath(path) + "\"") { UseShellExecute = true });
    }

    public static void OpenInBrowser(string path)
    {
        var browser = new[] {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft/Edge/Application/msedge.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Google/Chrome/Application/chrome.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Mozilla Firefox/firefox.exe")
        }.FirstOrDefault(File.Exists) ?? throw new IOException("No supported browser found (Edge, Chrome, or Firefox).");
        var start = new ProcessStartInfo(browser) { UseShellExecute = false };
        start.ArgumentList.Add(new Uri(Path.GetFullPath(path)).AbsoluteUri);
        Process.Start(start);
    }

    private static async Task<string?> Name(FrameworkElement owner, string title, string initial = "")
    {
        var input = Design.Field("Name"); input.Text = initial;
        var dialog = new ContentDialog { XamlRoot = owner.XamlRoot, Title = title, Content = input,
            PrimaryButtonText = "Save", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Primary };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary) return null;
        var name = input.Text.Trim();
        if (name.Length == 0 || name is "." or ".." || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0
            || name.EndsWith('.') || name.EndsWith(' '))
            throw new IOException("Enter a valid file or folder name.");
        return name;
    }

    private static async Task Create(FrameworkElement owner, string folder, bool directory, Action refresh)
    {
        var name = await Name(owner, directory ? "New Folder" : "New File");
        if (name is null) return;
        var target = Path.Combine(folder, name);
        if (File.Exists(target) || Directory.Exists(target)) throw new IOException("That name already exists.");
        if (directory) Directory.CreateDirectory(target);
        else { using var file = new FileStream(target, FileMode.CreateNew); }
        refresh();
    }

    private static async Task Find(FrameworkElement owner, string folder, Action<string> open)
    {
        var query = Design.Field("Find text or filename");
        var dialog = new ContentDialog { XamlRoot = owner.XamlRoot, Title = "Find in Folder", Content = query,
            PrimaryButtonText = "Find", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Primary };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary || string.IsNullOrWhiteSpace(query.Text)) return;
        var term = query.Text.Trim();
        using var cancel = new CancellationTokenSource();
        var progress = new ContentDialog { XamlRoot = owner.XamlRoot, Title = "Finding files…",
            Content = new ProgressRing { IsActive = true }, CloseButtonText = "Cancel" };
        progress.CloseButtonClick += (_, _) => cancel.Cancel();
        var showing = progress.ShowAsync();
        List<string> matches;
        try {
            matches = await Task.Run(() => Directory.EnumerateFiles(folder, "*", new EnumerationOptions {
                RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = FileAttributes.ReparsePoint
            }).TakeWhile(_ => !cancel.IsCancellationRequested).Where(p => Matches(p, term)).Take(200).ToList());
        } finally { progress.Hide(); await showing; }
        if (cancel.IsCancellationRequested) return;
        var list = new ListView { MaxHeight = 420, SelectionMode = ListViewSelectionMode.Single };
        foreach (var path in matches) list.Items.Add(new ListViewItem { Content = Path.GetRelativePath(folder, path), Tag = path });
        var results = new ContentDialog { XamlRoot = owner.XamlRoot, Title = $"Files matching “{term}” (up to 200)",
            Content = matches.Count == 0 ? Design.Caption("No matching files.") : list,
            PrimaryButtonText = "Open", CloseButtonText = "Close", IsPrimaryButtonEnabled = false };
        list.SelectionChanged += (_, _) => results.IsPrimaryButtonEnabled = list.SelectedItem is ListViewItem;
        if (await results.ShowAsync() == ContentDialogResult.Primary && list.SelectedItem is ListViewItem item)
            open((string)item.Tag);
    }

    private static bool Matches(string path, string term)
    {
        if (Path.GetFileName(path).Contains(term, StringComparison.OrdinalIgnoreCase)) return true;
        try {
            if (new FileInfo(path).Length > 2 * 1024 * 1024) return false;
            using var reader = new StreamReader(path);
            var buffer = new char[4096];
            string tail = "";
            int count;
            while ((count = reader.Read(buffer, 0, buffer.Length)) > 0) {
                var text = tail + new string(buffer, 0, count);
                if (text.Contains('\0')) return false;
                if (text.Contains(term, StringComparison.OrdinalIgnoreCase)) return true;
                tail = text.Length >= term.Length ? text[^(term.Length - 1)..] : text;
            }
        } catch (IOException) { } catch (UnauthorizedAccessException) { }
        return false;
    }

    public static async Task Message(FrameworkElement owner, string title, string text)
    {
        await new ContentDialog { XamlRoot = owner.XamlRoot, Title = title, Content = text, CloseButtonText = "OK" }.ShowAsync();
    }
}
