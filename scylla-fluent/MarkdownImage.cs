using System.Text.RegularExpressions;

namespace Scylla;

internal sealed record MarkdownImage(int Index, int Length, string Alt, string Target, string Title)
{
    // Code spans are consumed first so image examples remain literal. Balancing
    // groups allow parentheses in destinations such as images/shot(2).png.
    private static readonly Regex Tokens = new(
        "(?<code>`+)[\\s\\S]*?\\k<code>(?!`)|" +
        "(?<image>!\\[(?<alt>(?:\\\\.|[^\\]\\\\])*)\\]\\(\\s*" +
        "(?<target><[^>\\r\\n]*>|(?:\\\\.|[^\\s()\\\\]|\\((?<depth>)|\\)(?<-depth>))+)(?(depth)(?!))" +
        "(?:\\s+(?:\"(?<title>[^\"]*)\"|'(?<title>[^']*)'))?\\s*\\))",
        RegexOptions.Compiled, TimeSpan.FromSeconds(1));

    private static string Unescape(string text) => Regex.Replace(text, @"\\([!""#$%&'()*+,\-./:;<=>?@\[\]\\^_`{|}~])", "$1");

    public static IEnumerable<MarkdownImage> Find(string text)
    {
        foreach (Match match in Tokens.Matches(text))
        {
            if (!match.Groups["image"].Success) continue;
            var escapes = 0;
            for (var i = match.Index - 1; i >= 0 && text[i] == '\\'; i--) escapes++;
            if (escapes % 2 != 0) continue;
            var target = match.Groups["target"].Value;
            if (target.StartsWith('<')) target = target[1..^1];
            yield return new(match.Index, match.Length, Unescape(match.Groups["alt"].Value),
                Unescape(target), Unescape(match.Groups["title"].Value));
        }
    }
}
