using System.Text.Json.Serialization;

namespace Scylla.UI.Models;

public enum ScyllaSessionState
{
    Ready,
    Starting,
    Running,
    Degraded,
    Stopping,
    Clean,
    Refused,
    CleanupRequired,
    Error
}

public sealed class EngineEvent
{
    [JsonPropertyName("ok")]
    public bool Ok { get; set; }

    [JsonPropertyName("event")]
    public string? Event { get; set; }

    [JsonPropertyName("state")]
    public string State { get; set; } = "READY";

    [JsonPropertyName("code")]
    public string? Code { get; set; }

    [JsonPropertyName("message")]
    public string? Message { get; set; }

    [JsonPropertyName("sessionId")]
    public string? SessionId { get; set; }

    [JsonPropertyName("pid")]
    public int Pid { get; set; }

    [JsonPropertyName("containedProcessCount")]
    public int ContainedProcessCount { get; set; }

    [JsonPropertyName("externalRelatedProcessCount")]
    public int ExternalRelatedProcessCount { get; set; }

    [JsonPropertyName("details")]
    public List<RelatedDetail> Details { get; set; } = new();

    [JsonPropertyName("relatedRunning")]
    public List<RelatedDetail> RelatedRunning { get; set; } = new();
}

public sealed class RelatedDetail
{
    [JsonPropertyName("pid")]
    public int Pid { get; set; }

    [JsonPropertyName("image")]
    public string? Image { get; set; }

    [JsonPropertyName("path")]
    public string? Path { get; set; }

    [JsonPropertyName("contained")]
    public bool Contained { get; set; }
}
