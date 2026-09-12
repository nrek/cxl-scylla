namespace Scylla;

internal static class PreviewText
{
    // Display-only recovery for common UTF-8 punctuation previously decoded as
    // Windows-1252/Latin-1. Never rewrite the editor buffer or code blocks.
    public static string Normalize(string text) => text
        .Replace("\u00e2\u20ac\u201d", "\u2014")
        .Replace("\u00e2\u0080\u0094", "\u2014")
        .Replace("\u00e2\u20ac\u201c", "\u2013")
        .Replace("\u00e2\u0080\u0093", "\u2013")
        .Replace("\u00e2\u20ac\u2122", "\u2019")
        .Replace("\u00e2\u20ac\u02dc", "\u2018")
        .Replace("\u00e2\u20ac\u0153", "\u201c")
        .Replace("\u00e2\u20ac\u009d", "\u201d")
        .Replace("\u00e2\u20ac\u00a6", "\u2026");
}
