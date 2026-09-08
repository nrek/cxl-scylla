using System.Text.Json;
using Scylla.UI.Models;
using System.IO;

namespace Scylla.UI.Services;

public sealed class ProfileService
{
    public string DirectoryPath { get; }

    public ProfileService()
    {
        var root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        DirectoryPath = Path.Combine(root, "Scylla", "profiles");
        Directory.CreateDirectory(DirectoryPath);
    }

    static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    public IReadOnlyList<string> ListNames()
    {
        return Directory.GetFiles(DirectoryPath, "*.json")
            .Select(Path.GetFileNameWithoutExtension)
            .Where(n => !string.IsNullOrWhiteSpace(n))
            .Select(n => n!)
            .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    public ScyllaProfile Load(string name)
    {
        var path = FilePath(name);
        var json = File.ReadAllText(path);
        return JsonSerializer.Deserialize<ScyllaProfile>(json, JsonOpts) ?? new ScyllaProfile { Name = name };
    }

    public void Save(ScyllaProfile profile)
    {
        if (string.IsNullOrWhiteSpace(profile.Name))
        {
            throw new InvalidOperationException("Profile name is required.");
        }
        File.WriteAllText(FilePath(profile.Name), JsonSerializer.Serialize(profile, JsonOpts));
    }

    public void Delete(string name)
    {
        var path = FilePath(name);
        if (File.Exists(path))
        {
            File.Delete(path);
        }
    }

    string FilePath(string name)
    {
        foreach (var c in Path.GetInvalidFileNameChars())
        {
            name = name.Replace(c, '_');
        }
        return Path.Combine(DirectoryPath, name + ".json");
    }
}
