using System.Text.Json.Serialization;

namespace Scylla;

internal sealed class ChatAttachment
{
    [JsonPropertyName("path")] public string Path { get; init; } = "";
    [JsonPropertyName("label")] public string Label { get; init; } = "";
    [JsonPropertyName("image")] public bool Image { get; init; } = true;
}
