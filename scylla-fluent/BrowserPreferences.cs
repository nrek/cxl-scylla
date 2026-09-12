using System.Text.Json;

namespace Scylla;

internal sealed class BrowserPreferences
{
    public List<string> HiddenFolders { get; set; } = new();
    public List<string> AddedFolders { get; set; } = new();
    public List<string> HiddenKnowledgePaths { get; set; } = new();
    public List<string> PinnedTabs { get; set; } = new();
    private static string FilePath => WindowInstance.StatePath("browser-folders.json");
    public static BrowserPreferences Load()
    {
        try { return JsonSerializer.Deserialize<BrowserPreferences>(File.ReadAllText(FilePath)) ?? new(); }
        catch (FileNotFoundException) { return new(); }
        catch (DirectoryNotFoundException) { return new(); }
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
