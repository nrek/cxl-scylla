using System.Text.Json.Serialization;
using System.Windows.Media;

namespace Scylla.UI.Models;

public sealed class DiscoverResult
{
    [JsonPropertyName("ok")]
    public bool Ok { get; set; }

    [JsonPropertyName("applications")]
    public List<DiscoveredApplication> Applications { get; set; } = new();
}

public sealed class DiscoveredApplication
{
    [JsonPropertyName("id")]
    public string? Id { get; set; }

    [JsonPropertyName("displayName")]
    public string DisplayName { get; set; } = "";

    [JsonPropertyName("publisher")]
    public string? Publisher { get; set; }

    [JsonPropertyName("kind")]
    public string Kind { get; set; } = "win32";

    [JsonPropertyName("available")]
    public bool Available { get; set; }

    [JsonPropertyName("status")]
    public string Status { get; set; } = "not_found";

    [JsonPropertyName("compatibility")]
    public string Compatibility { get; set; } = "unknown";

    [JsonPropertyName("executable")]
    public string? Executable { get; set; }

    [JsonPropertyName("packageFamilyName")]
    public string? PackageFamilyName { get; set; }

    [JsonPropertyName("appId")]
    public string? AppId { get; set; }

    [JsonPropertyName("aumid")]
    public string? Aumid { get; set; }

    [JsonPropertyName("installRoot")]
    public string? InstallRoot { get; set; }

    [JsonPropertyName("discoverySource")]
    public string? DiscoverySource { get; set; }

    [JsonPropertyName("group")]
    public string Group { get; set; } = "other";

    [JsonIgnore]
    public ImageSource? Icon { get; set; }

    public string GroupHeader => Group == "ai" ? "AI / Development" : "Other Installed Applications";

    public string Label
    {
        get
        {
            var kind = Kind.ToUpperInvariant();
            var pub = string.IsNullOrWhiteSpace(Publisher) ? "" : $"  ·  {Publisher}";
            var extra = Compatibility == "cannot_contain" ? "    isolated user" : "";
            return $"{DisplayName}    {kind}{pub}{extra}";
        }
    }
}
