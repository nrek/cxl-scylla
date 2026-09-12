using Scylla;

var cases = new (string Input, string Expected)[]
{
    ("A \u00e2\u20ac\u201d B", "A — B"),
    ("A \u00e2\u0080\u0094 B", "A — B"),
    ("‘Hello’ — café – 中文 🙂", "‘Hello’ — café – 中文 🙂"),
    ("a\nb\r\nc", "a\nb\r\nc"),
};
foreach (var (input, expected) in cases)
{
    var result = PreviewText.Normalize(input);
    if (result != expected || PreviewText.Normalize(result) != expected)
        throw new Exception("Preview encoding regression");
}
Console.WriteLine("Preview encoding tests passed.");

var folder = Path.Combine(Path.GetTempPath(), "scylla-links-" + Guid.NewGuid());
Directory.CreateDirectory(folder);
try
{
    var file = Path.Combine(folder, "review notes.md");
    File.WriteAllText(file, "# Review");
    foreach (var target in new[] { file, "review notes.md", new Uri(file).AbsoluteUri, file.Replace(" ", "%20") })
        if (MarkdownLink.Resolve(target, folder).File != file) throw new Exception("File link failed: " + target);
    var located = MarkdownLink.Resolve(file + ":12", folder);
    if (located.File != file || located.Line != 12) throw new Exception("Line location lost");
    var web = MarkdownLink.Resolve("https://example.com/docs?q=test#section", folder);
    if (web.Web?.Scheme != "https" || web.File != null) throw new Exception("Web routing failed");
    foreach (var target in new[] { "javascript:alert(1)", "shell:AppsFolder", "missing.md" })
    {
        var result = MarkdownLink.Resolve(target, folder);
        if (result.Web != null || result.File != null) throw new Exception("Invalid link accepted");
    }
    Console.WriteLine("Markdown file, line, web and unsupported link tests passed.");
}
finally { File.Delete(Path.Combine(folder, "review notes.md")); Directory.Delete(folder); }

var imageCases = new (string Markdown, string Alt, string Target, string Title)[] {
    ("![Logo](images/logo.png)", "Logo", "images/logo.png", ""),
    ("Before ![Shot](images/shot(2).png) after", "Shot", "images/shot(2).png", ""),
    ("![Nested](images/a(b(c)).png)", "Nested", "images/a(b(c)).png", ""),
    ("![Space](<images/my logo.png>)", "Space", "images/my logo.png", ""),
    ("![Logo](images/logo.png \"Brand\")", "Logo", "images/logo.png", "Brand"),
    ("![](https://example.com/image.png?size=2 'Remote')", "", "https://example.com/image.png?size=2", "Remote"),
    (@"![An \] alt](images/shot\(2\).png)", "An ] alt", "images/shot(2).png", ""),
};
foreach (var (markdown, alt, target, title) in imageCases) {
    var image = MarkdownImage.Find(markdown).Single();
    if (image.Alt != alt || image.Target != target || image.Title != title ||
        !markdown.Substring(image.Index, image.Length).StartsWith("!["))
        throw new Exception("Image parsing failed: " + markdown);
}
foreach (var literal in new[] { "`![code](image.png)`", "``![code](image.png)``", @"\![escaped](image.png)", "![missing](", "[ordinary](image.png)" })
    if (MarkdownImage.Find(literal).Any()) throw new Exception("Literal parsed as image: " + literal);
if (MarkdownImage.Find("![one](a.png) and ![two](b.svg)").Count() != 2)
    throw new Exception("Multiple images lost");
if (MarkdownImage.Find("`![code](a.png)` ![real](b.png)").Single().Target != "b.png")
    throw new Exception("Image after code span lost");
Console.WriteLine("Markdown image syntax and code-span tests passed.");

var imageFolder = Path.Combine(Path.GetTempPath(), "scylla-images-" + Guid.NewGuid());
Directory.CreateDirectory(imageFolder);
try {
    var imagePath = Path.Combine(imageFolder, "my shot(2).png");
    File.WriteAllBytes(imagePath, Array.Empty<byte>()); // Resolution only; no decoder involved.
    foreach (var target in new[] { "my%20shot(2).png", imagePath, new Uri(imagePath).AbsoluteUri })
        if (MarkdownLink.Resolve(target, imageFolder).File != imagePath)
            throw new Exception("Image resolution failed: " + target);
    if (MarkdownLink.Resolve("https://example.com/image.svg", imageFolder).Web == null)
        throw new Exception("Remote SVG resolution failed");
    foreach (var target in new[] { "missing.png", "javascript:alert(1)", "data:image/png;base64,AA==" }) {
        var result = MarkdownLink.Resolve(target, imageFolder);
        if (result.File != null || result.Web != null) throw new Exception("Unsupported image source accepted");
    }
    Console.WriteLine("Markdown image path and unsupported-source tests passed.");
} finally { File.Delete(Path.Combine(imageFolder, "my shot(2).png")); Directory.Delete(imageFolder); }
