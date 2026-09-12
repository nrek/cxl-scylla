using System.Text.Json;

namespace Scylla;

internal enum TerminalMouseBehavior { HighlightCopy, RightClickCopy, RightClickMenu }

internal sealed class PanelPreferences
{
    public TerminalMouseBehavior TerminalMouse { get; set; } = TerminalMouseBehavior.HighlightCopy;
    public bool PayloadColorCoding { get; set; }
    public static PanelPreferences Current { get; } = Load();
    private static string FilePath => Path.Combine(Environment.GetFolderPath(
        Environment.SpecialFolder.LocalApplicationData), "ScyllaGPT", "panel-preferences.json");

    private static PanelPreferences Load()
    {
        try
        {
            var preferences = JsonSerializer.Deserialize<PanelPreferences>(File.ReadAllText(FilePath)) ?? new();
            if (!Enum.IsDefined(preferences.TerminalMouse)) preferences.TerminalMouse = TerminalMouseBehavior.HighlightCopy;
            return preferences;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        { return new(); }
    }

    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        var temporary = FilePath + "." + Environment.ProcessId + "." + Guid.NewGuid().ToString("N") + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(this));
        try { File.Move(temporary, FilePath, true); }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
