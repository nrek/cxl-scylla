using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Web.WebView2.Core;

namespace Scylla;

internal static class MermaidPreview
{
    public static FrameworkElement Render(string markdown, Action<string>? openLink = null, string? imageBaseDirectory = null)
    {
        var panel = new StackPanel { Spacing = 12 };
        var prose = new StringBuilder();
        var lines = markdown.Replace("\r\n", "\n").Split('\n');
        for (var i = 0; i < lines.Length; i++)
        {
            var fence = Regex.Match(lines[i], @"^ {0,3}(`{3,}|~{3,})(.*)$");
            if (!fence.Success) { prose.AppendLine(lines[i]); continue; }
            var marker = fence.Groups[1].Value;
            var end = i + 1;
            while (end < lines.Length && !Regex.IsMatch(lines[end],
                @"^ {0,3}" + Regex.Escape(marker[0].ToString()) + "{" + marker.Length + @",}\s*$")) end++;
            if (!string.Equals(fence.Groups[2].Value.Trim(), "mermaid", StringComparison.OrdinalIgnoreCase)
                || end == lines.Length)
            {
                // Keep other fenced blocks intact, including Mermaid examples inside them.
                for (; i < Math.Min(end + 1, lines.Length); i++) prose.AppendLine(lines[i]);
                i--;
                continue;
            }
            if (prose.Length > 0) { panel.Children.Add(MarkdownView.Render(prose.ToString(), repairEncoding: true, openLink: openLink, imageBaseDirectory: imageBaseDirectory)); prose.Clear(); }
            panel.Children.Add(Diagram(string.Join("\n", lines[(i + 1)..end])));
            i = end;
        }
        if (prose.Length > 0) panel.Children.Add(MarkdownView.Render(prose.ToString(), repairEncoding: true, openLink: openLink, imageBaseDirectory: imageBaseDirectory));
        return panel;
    }

    private static FrameworkElement Diagram(string source)
    {
        var host = new StackPanel { Spacing = 8 };
        var fallback = new TextBlock
        {
            Text = source, IsTextSelectionEnabled = true,
            TextWrapping = TextWrapping.Wrap,
            Foreground = ThemeColors.Brush(ThemeColors.Secondary),
        };
        var web = new WebView2 { Height = 300 };
        host.Children.Add(web);
        var started = false;
        web.Loaded += async (_, _) =>
        {
            if (started) return;
            started = true;
            try
            {
                await web.EnsureCoreWebView2Async();
                var core = web.CoreWebView2;
                core.Settings.AreHostObjectsAllowed = false;
                core.Settings.AreDefaultScriptDialogsEnabled = false;
                core.SetVirtualHostNameToFolderMapping("mermaid.scylla.invalid",
                    System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "mermaid"),
                    CoreWebView2HostResourceAccessKind.Deny);
                const string page = "https://mermaid.scylla.invalid/preview.html";
                core.NavigationStarting += (_, e) => { if (e.Uri != page) e.Cancel = true; };
                core.NewWindowRequested += (_, e) => e.Handled = true;
                core.WebMessageReceived += (_, e) =>
                {
                    using var message = JsonDocument.Parse(e.WebMessageAsJson);
                    if (message.RootElement.TryGetProperty("height", out var height))
                        web.Height = Math.Clamp(height.GetDouble(), 100, 1600);
                };
                core.NavigationCompleted += (_, e) =>
                {
                    if (e.IsSuccess) core.PostWebMessageAsJson(JsonSerializer.Serialize(source));
                    else { web.Visibility = Visibility.Collapsed; if (!host.Children.Contains(fallback)) host.Children.Add(fallback); }
                };
                core.Navigate(page);
            }
            catch (Exception ex)
            {
                web.Visibility = Visibility.Collapsed;
                host.Children.Add(Design.Caption("Diagram preview unavailable: " + ex.Message));
                if (!host.Children.Contains(fallback)) host.Children.Add(fallback);
            }
        };
        host.Unloaded += (_, _) => web.Close();
        return host;
    }
}
