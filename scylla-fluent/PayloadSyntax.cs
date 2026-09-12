using System.Text.RegularExpressions;

namespace Scylla;

internal enum PayloadTokenKind { Plain, Timestamp, Key, String, Number, Literal, Punctuation, Direction }
internal readonly record struct PayloadToken(string Text, PayloadTokenKind Kind);

internal static class PayloadSyntax
{
    // Works on partial/truncated JSON embedded in log lines; never reformats the data.
    private static readonly Regex Tokens = new(
        "(?<Timestamp>\\b(?:\\d{4}-\\d{2}-\\d{2}[T ])?\\d{2}:\\d{2}:\\d{2}(?:\\.\\d+)?Z?)|" +
        "(?<Key>\"(?:\\\\.|[^\"\\\\])*\"(?=\\s*:))|" +
        "(?<String>\"(?:\\\\.|[^\"\\\\])*\")|" +
        "(?<Number>(?<![\\w])-?\\b\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?\\b)|" +
        "(?<Literal>\\b(?:true|false|null)\\b)|" +
        "(?<Direction>\\b(?:IN|OUT|SEND|RECV|ERROR)\\b)|" +
        "(?<Punctuation>[{}\\[\\]:,])",
        RegexOptions.CultureInvariant | RegexOptions.Compiled, TimeSpan.FromMilliseconds(100));

    public static IReadOnlyList<PayloadToken> Highlight(string text)
    {
        var result = new List<PayloadToken>();
        int offset = 0;
        try
        {
            for (var match = Tokens.Match(text); match.Success; match = match.NextMatch())
            {
                // Bound XAML inline creation when logs contain many tiny tokens.
                if (result.Count >= 6000) break;
                if (match.Index > offset) result.Add(new(text[offset..match.Index], PayloadTokenKind.Plain));
                var kind = Enum.GetValues<PayloadTokenKind>().First(k =>
                    k != PayloadTokenKind.Plain && match.Groups[k.ToString()].Success);
                result.Add(new(match.Value, kind));
                offset = match.Index + match.Length;
            }
        }
        catch (RegexMatchTimeoutException) { /* Render the remaining text without color. */ }
        if (offset < text.Length) result.Add(new(text[offset..], PayloadTokenKind.Plain));
        return result;
    }
}
