using System.Text.Json.Serialization;

namespace Scylla.UI.Models;

public sealed class IdentityStatusFile
{
    [JsonPropertyName("ok")]
    public bool Ok { get; set; }

    [JsonPropertyName("user")]
    public string? User { get; set; }

    [JsonPropertyName("state")]
    public string? State { get; set; }

    [JsonPropertyName("ramBytes")]
    public long RamBytes { get; set; }

    [JsonPropertyName("tcp")]
    public int Tcp { get; set; }

    [JsonPropertyName("pids")]
    public List<int> Pids { get; set; } = new();

    [JsonPropertyName("error")]
    public string? Error { get; set; }
}
