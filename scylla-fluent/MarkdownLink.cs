using System.Text.RegularExpressions;

namespace Scylla;

internal static class MarkdownLink
{
    // Resolve only file paths and web URLs; never launch arbitrary URI schemes.
    public static (string? File, Uri? Web, int Line) Resolve(string target, string baseDirectory)
    {
        target = target.Trim();
        if (Uri.TryCreate(target, UriKind.Absolute, out var uri) &&
            (uri.Scheme == "https" || uri.Scheme == "http")) return (null, uri, 0);
        if (uri?.IsFile == true) target = uri.LocalPath;
        else if (Regex.IsMatch(target, @"^[a-zA-Z][a-zA-Z0-9+.-]*:") &&
                 !Regex.IsMatch(target, @"^[a-zA-Z]:[\\/]")) return (null, null, 0);
        var line = Regex.Match(target, @"(?::|#L)(\d+)(?::\d+)?$");
        var number = line.Success && int.TryParse(line.Groups[1].Value, out var n) ? n : 0;
        if (line.Success) target = target[..line.Index];
        try
        {
            target = Uri.UnescapeDataString(target);
            // Agent links sometimes use /D:/path for Markdown file destinations.
            if (Regex.IsMatch(target, @"^/[a-zA-Z]:/")) target = target[1..];
            var path = Path.GetFullPath(target, baseDirectory);
            return (System.IO.File.Exists(path) ? path : null, null, number);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or IOException)
        { return (null, null, 0); }
    }
}
