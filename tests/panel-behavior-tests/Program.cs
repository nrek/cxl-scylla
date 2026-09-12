using Scylla;

static void Check(bool condition, string name)
{
    if (!condition) throw new Exception(name);
    Console.WriteLine("PASS: " + name);
}

var sample = "2026-09-12T10:30:45.123Z IN {\"message\":\"escaped \\\"quotes\\\" and 🌊\",\"count\":-2.5e3,\"ok\":true,\"value\":null}\r\n";
var tokens = PayloadSyntax.Highlight(sample);
Check(string.Concat(tokens.Select(t => t.Text)) == sample, "color coding preserves original log bytes/text");
foreach (var kind in new[] { PayloadTokenKind.Key, PayloadTokenKind.String, PayloadTokenKind.Number,
    PayloadTokenKind.Literal, PayloadTokenKind.Timestamp, PayloadTokenKind.Punctuation, PayloadTokenKind.Direction })
    Check(tokens.Any(t => t.Kind == kind), "highlights " + kind);
Check(tokens.Any(t => t.Kind == PayloadTokenKind.Number && t.Text == "-2.5e3"), "negative exponent stays one number");
Check(PayloadSyntax.Highlight("10:30:45.123Z OUT {}").First().Kind == PayloadTokenKind.Timestamp,
    "native log time-only timestamp is highlighted");
foreach (var text in new[] { "", "plain text", "{\"partial\":\"unfinished\\", new string('"', 20000),
    string.Concat(Enumerable.Repeat("{\"a\":1}\n", 10000)) })
{
    var result = PayloadSyntax.Highlight(text);
    Check(string.Concat(result.Select(t => t.Text)) == text, "partial/large payload remains intact (" + text.Length + ")");
    Check(result.Count <= 6002, "inline count stays bounded");
}
