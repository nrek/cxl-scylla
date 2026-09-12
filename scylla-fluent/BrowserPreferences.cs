using System.Text.Json;

namespace Scylla;

internal sealed class BrowserPreferences
{
    public List<string> HiddenFolders { get; set; } = new();
    public List<string> AddedFolders { get; set; } = new();
    public List<string> HiddenKnowledgePaths { get; set; } = new();
    public List<string> PinnedTabs { get; set; } = new();
    private static string FilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ScyllaGPT", "browser-folders.json");
    public static BrowserPreferences Load()
    {
        try { return JsonSerializer.Deserialize<BrowserPreferences>(File.ReadAllText(FilePath)) ?? new(); }
        catch (FileNotFoundException) { return new(); }
        catch (DirectoryNotFoundException) { return new(); }
    }
    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        File.WriteAllText(FilePath + ".tmp", JsonSerializer.Serialize(this));
        File.Move(FilePath + ".tmp", FilePath, true);
    }
}
