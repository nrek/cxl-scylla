namespace Scylla.UI.Models;

public enum GrantMode
{
    ReadOnly,
    ReadWrite
}

public sealed class FilesystemGrant
{
    public string Path { get; set; } = "";
    public GrantMode Mode { get; set; } = GrantMode.ReadOnly;

    public string ModeLabel => Mode == GrantMode.ReadWrite ? "RW" : "RO";
}
