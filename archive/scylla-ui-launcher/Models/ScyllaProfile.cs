namespace Scylla.UI.Models;

public sealed class ScyllaProfile
{
    public string Name { get; set; } = "New profile";
    public ApplicationRef Application { get; set; } = new();
    public string? WorkingDirectory { get; set; }
    public List<GrantDto> Filesystem { get; set; } = new();
    public NetworkPolicy Network { get; set; } = new();
    public StrictSettings Strict { get; set; } = new();
}

public sealed class ApplicationRef
{
    public string? Id { get; set; }
    public string? Profile { get; set; }
    public string Executable { get; set; } = "";
    public string? Kind { get; set; }
}

public sealed class GrantDto
{
    public string Path { get; set; } = "";
    public string Mode { get; set; } = "ro";
}

public sealed class NetworkPolicy
{
    public bool Internet { get; set; } = true;
    public bool PrivateLan { get; set; }
}

public sealed class StrictSettings
{
    public bool Enabled { get; set; } = true;
    public bool DetectExternalRelatedProcesses { get; set; } = true;
    public bool KillContainedTreeOnExit { get; set; } = true;
    public bool KillRelatedProcessesOnLaunch { get; set; } = true;
}
