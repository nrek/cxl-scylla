using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;

namespace Scylla;

internal static class MarkdownView
{
    public static FrameworkElement Render(string text, bool repairEncoding = false, Action<string>? openLink = null,
        string? imageBaseDirectory = null)
    {
        var panel = new StackPanel { Spacing = 4 };
        var lines = text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
        var prose = new StringBuilder();
        void Flush()
        {
            if (prose.Length == 0) return;
            panel.Children.Add(RenderInline(prose.ToString().TrimEnd(), repairEncoding, openLink, imageBaseDirectory));
            prose.Clear();
        }
        for (var i = 0; i < lines.Length; i++)
        {
            var fence = Regex.Match(lines[i], @"^ {0,3}(`{3,}|~{3,})(.*)$");
            if (fence.Success)
            {
                Flush();
                var marker = fence.Groups[1].Value;
                var code = new StringBuilder();
                var firstLine = true;
                while (++i < lines.Length && !Regex.IsMatch(lines[i],
                    @"^ {0,3}" + Regex.Escape(marker[0].ToString()) + "{" + marker.Length + @",}\s*$"))
                { if (!firstLine) code.Append('\n'); code.Append(lines[i]); firstLine = false; }
                var content = new StackPanel { Spacing = 6 };
                var language = fence.Groups[2].Value.Trim();
                if (language.Length > 0) content.Children.Add(Design.Caption(language));
                content.Children.Add(new ScrollViewer {
                    HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
                    VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
                    Content = CodeBlock(code.ToString())
                });
                panel.Children.Add(Design.Card(content, new Thickness(12)));
                continue;
            }
            if (i + 1 < lines.Length && lines[i].Contains('|') &&
                Regex.IsMatch(lines[i + 1], @"^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$"))
            {
                Flush();
                var table = new Grid();
                var headers = Cells(lines[i]);
                for (var c = 0; c < headers.Length; c++) table.ColumnDefinitions.Add(new ColumnDefinition());
                void Row(string[] cells, bool header)
                {
                    var row = table.RowDefinitions.Count;
                    table.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                    for (var c = 0; c < headers.Length; c++)
                    {
                        var cell = new Border { Padding = new Thickness(10, 7, 10, 7),
                            BorderThickness = new Thickness(0, 0, 0, 1), BorderBrush = ThemeColors.Brush(ThemeColors.BorderSubtle),
                            Background = ThemeColors.Brush(header ? ThemeColors.Elevated : ThemeColors.Surface),
                            Child = RenderInline(c < cells.Length ? cells[c] : "", repairEncoding, openLink, imageBaseDirectory) };
                        Grid.SetRow(cell, row); Grid.SetColumn(cell, c); table.Children.Add(cell);
                    }
                }
                Row(headers, true); i++;
                while (i + 1 < lines.Length && lines[i + 1].Contains('|') && !string.IsNullOrWhiteSpace(lines[i + 1]))
                    Row(Cells(lines[++i]), false);
                panel.Children.Add(table);
                continue;
            }
            if (Regex.IsMatch(lines[i], @"^\s{0,3}([-*_])(?:\s*\1){2,}\s*$"))
            { Flush(); panel.Children.Add(Design.Divider(new Thickness(0, 6, 0, 6))); continue; }
            if (lines[i].StartsWith("> "))
            {
                Flush();
                panel.Children.Add(new Border { BorderThickness = new Thickness(3, 0, 0, 0),
                    BorderBrush = new SolidColorBrush(Windows.UI.Color.FromArgb(255, 77, 190, 180)),
                    Padding = new Thickness(12, 4, 0, 4), Child = RenderInline(lines[i][2..], repairEncoding, openLink, imageBaseDirectory) });
                continue;
            }
            var heading = Regex.Match(lines[i], @"^ {0,3}#{1,6}\s+");
            if (heading.Success)
            {
                Flush();
                var title = RenderInline(lines[i], repairEncoding, openLink, imageBaseDirectory);
                title.Margin = new Thickness(0, panel.Children.Count == 0 ? 0 : 20, 0, 13.6);
                panel.Children.Add(title);
                continue;
            }
            if (string.IsNullOrWhiteSpace(lines[i])) { Flush(); continue; }
            prose.AppendLine(lines[i]);
        }
        Flush();
        return panel;
    }

    private static string[] Cells(string line) => Regex.Split(line.Trim().Trim('|'), @"(?<!\\)\|")
        .Select(cell => cell.Trim().Replace(@"\|", "|")).ToArray();

    private static FrameworkElement CodeBlock(string code)
    {
        var block = new RichTextBlock { IsTextSelectionEnabled = true, TextWrapping = TextWrapping.NoWrap,
            FontFamily = new FontFamily("Cascadia Mono"), FontSize = 13,
            Foreground = new SolidColorBrush(Windows.UI.Color.FromArgb(255, 230, 148, 5)) };
        var paragraph = new Paragraph();
        // Lightweight lexical accents; never interpret or execute source content.
        var tokens = Regex.Matches(code,
            "(?<string>\"(?:\\\\.|[^\"\\\\])*\"|'(?:\\\\.|[^'\\\\])*')|(?<comment>(?m:^\\s*(?:#|//)[^\\n]*))|(?<number>\\b\\d+(?:\\.\\d+)?\\b)");
        var offset = 0;
        foreach (Match token in tokens)
        {
            if (token.Index > offset) paragraph.Inlines.Add(new Run { Text = code[offset..token.Index] });
            var color = token.Groups["string"].Success ? Windows.UI.Color.FromArgb(255, 77, 190, 180)
                : token.Groups["comment"].Success ? ThemeColors.Muted : Windows.UI.Color.FromArgb(255, 190, 160, 230);
            paragraph.Inlines.Add(new Run { Text = token.Value, Foreground = new SolidColorBrush(color) });
            offset = token.Index + token.Length;
        }
        if (offset < code.Length) paragraph.Inlines.Add(new Run { Text = code[offset..] });
        block.Blocks.Add(paragraph);
        return block;
    }

    private static FrameworkElement RenderInline(string text, bool repairEncoding, Action<string>? openLink,
        string? imageBaseDirectory = null)
    {
        if (imageBaseDirectory != null)
        {
            var images = MarkdownImage.Find(text).ToArray();
            if (images.Length > 0)
            {
                var content = new StackPanel { Spacing = 4 };
                var offset = 0;
                foreach (var image in images)
                {
                    if (image.Index > offset)
                        content.Children.Add(RenderInline(text[offset..image.Index], repairEncoding, openLink));
                    content.Children.Add(MarkdownImageView.Create(image, imageBaseDirectory));
                    offset = image.Index + image.Length;
                }
                if (offset < text.Length) content.Children.Add(RenderInline(text[offset..], repairEncoding, openLink));
                return content;
            }
        }
        if (string.IsNullOrEmpty(text))
            return new TextBlock { Text = "", Foreground = ThemeColors.Brush(ThemeColors.Text) };

        string? runsJson = null;
        try { runsJson = NativeCore.MarkdownRunsJson(text); }
        catch { /* fall back to plain */ }

        if (string.IsNullOrWhiteSpace(runsJson))
        {
            return new TextBlock
            {
                Text = text,
                Foreground = ThemeColors.Brush(ThemeColors.Text),
                TextWrapping = TextWrapping.WrapWholeWords,
                IsTextSelectionEnabled = true,
                LineHeight = 17.6,
                LineStackingStrategy = LineStackingStrategy.BlockLineHeight,
            };
        }

        var block = new RichTextBlock
        {
            IsTextSelectionEnabled = true,
            TextWrapping = TextWrapping.WrapWholeWords,
            Foreground = ThemeColors.Brush(ThemeColors.Text),
            LineHeight = 17.6,
                LineStackingStrategy = LineStackingStrategy.BlockLineHeight,
        };
        var paragraph = new Paragraph();
        try
        {
            using var doc = JsonDocument.Parse(runsJson);
            foreach (var item in doc.RootElement.EnumerateArray())
            {
                var runText = item.TryGetProperty("text", out var t) ? t.GetString() ?? "" : "";
                if (runText.Length == 0) continue;
                var run = new Run { Text = runText };
                var bold = item.TryGetProperty("bold", out var b) && b.ValueKind == JsonValueKind.True;
                var italic = item.TryGetProperty("italic", out var i) && i.ValueKind == JsonValueKind.True;
                var code = item.TryGetProperty("code", out var c) && c.ValueKind == JsonValueKind.True;
                if (repairEncoding && !code) run.Text = PreviewText.Normalize(runText);
                var strike = item.TryGetProperty("strike", out var s) && s.ValueKind == JsonValueKind.True;
                var heading = item.TryGetProperty("heading", out var h) && h.TryGetInt32(out var hv) ? hv : 0;
                if (bold || heading > 0) run.FontWeight = FontWeights.SemiBold;
                if (italic) run.FontStyle = Windows.UI.Text.FontStyle.Italic;
                if (strike) run.TextDecorations = Windows.UI.Text.TextDecorations.Strikethrough;
                if (code)
                {
                    run.FontFamily = new FontFamily("Cascadia Mono");
                    run.Foreground = new SolidColorBrush(Windows.UI.Color.FromArgb(255, 230, 148, 5));
                }
                if (heading > 0)
                {
                    block.LineStackingStrategy = LineStackingStrategy.MaxHeight;
                    run.FontSize = heading switch { 1 => 20, 2 => 17, 3 => 15, _ => 14 };

                }
                var destination = item.TryGetProperty("link", out var linkValue) ? linkValue.GetString() : null;
                if (!string.IsNullOrEmpty(destination) && openLink != null)
                {
                    var link = new Hyperlink { Foreground = new SolidColorBrush(Windows.UI.Color.FromArgb(255, 77, 190, 180)) };
                    link.Inlines.Add(run);
                    link.Click += (_, _) => openLink(destination);
                    paragraph.Inlines.Add(link);
                }
                else paragraph.Inlines.Add(run);

            }
        }
        catch
        {
            return new TextBlock
            {
                Text = text,
                Foreground = ThemeColors.Brush(ThemeColors.Text),
                TextWrapping = TextWrapping.WrapWholeWords,
                IsTextSelectionEnabled = true,
            };
        }
        if (paragraph.Inlines.Count > 0) block.Blocks.Add(paragraph);
        if (block.Blocks.Count == 0)
            block.Blocks.Add(new Paragraph { Inlines = { new Run { Text = text } } });
        return block;
    }
}

internal static class Win32Hwnd
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr childAfter, string? className, string? windowName);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint flags);

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

    public static IntPtr FindXamlIsland(IntPtr windowHwnd)
    {
        if (windowHwnd == IntPtr.Zero) return IntPtr.Zero;
        foreach (var cls in new[]
                 {
                     "Microsoft.UI.Content.DesktopChildSiteBridge",
                     "Windows.UI.Composition.DesktopWindowContentBridge",
                     "InputSiteWindowClass",
                 })
        {
            var child = FindWindowEx(windowHwnd, IntPtr.Zero, cls, null);
            if (child != IntPtr.Zero) return child;
        }
        return windowHwnd;
    }

    public static void ApplyDarkTitleBar(IntPtr hwnd)
    {
        if (hwnd == IntPtr.Zero) return;
        const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
        var useDark = 1;
        _ = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref useDark, sizeof(int));
    }
}
