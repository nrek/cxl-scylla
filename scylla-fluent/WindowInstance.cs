using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Scylla;

internal static class WindowInstance
{
    internal const string ArgumentPrefix = "--new-window=";
    internal const string EnvironmentVariable = "SCYLLA_WINDOW_ID";

    internal static string? CurrentId { get; private set; }
    internal static bool IsSecondary => CurrentId is not null;

    internal static void InitializeCurrentProcess(IEnumerable<string> args)
    {
        CurrentId = ParseId(args);
        if (CurrentId is null)
        {
            // The command-line token is authoritative; do not inherit another window's state
            // when Scylla is launched from one of its terminals.
            Environment.SetEnvironmentVariable(EnvironmentVariable, null, EnvironmentVariableTarget.Process);
            return;
        }

        var appData = SharedDataRoot();
        var windowData = WindowDataRoot(appData, CurrentId);
        Environment.SetEnvironmentVariable(EnvironmentVariable, CurrentId, EnvironmentVariableTarget.Process);
        Environment.SetEnvironmentVariable("WEBVIEW2_USER_DATA_FOLDER", Path.Combine(windowData, "webview2"),
            EnvironmentVariableTarget.Process);

        Directory.CreateDirectory(windowData);
        SeedSettings(appData, windowData);
        SeedCodexAuthentication(appData, windowData);
    }

    internal static string? ParseId(IEnumerable<string> args)
    {
        foreach (var arg in args)
        {
            if (!arg.StartsWith(ArgumentPrefix, StringComparison.OrdinalIgnoreCase)) continue;
            var raw = arg[ArgumentPrefix.Length..];
            return Guid.TryParseExact(raw, "N", out var id) ? id.ToString("N") : null;
        }
        return null;
    }

    internal static string WindowDataRoot(string appData, string id) =>
        Path.Combine(appData, "windows", id);

    internal static string SharedDataRoot() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ScyllaGPT");

    internal static string StatePath(string name) => Path.Combine(
        CurrentId is null ? SharedDataRoot() : WindowDataRoot(SharedDataRoot(), CurrentId), name);

    internal static ProcessStartInfo CreateStartInfo(string executable, string id)
    {
        var start = new ProcessStartInfo
        {
            FileName = executable,
            WorkingDirectory = Path.GetDirectoryName(executable) ?? AppContext.BaseDirectory,
            UseShellExecute = false,
        };
        start.ArgumentList.Add(ArgumentPrefix + id);
        start.Environment[EnvironmentVariable] = id;
        return start;
    }

    internal static bool StartNew(out string error)
    {
        error = "";
        try
        {
            var executable = Path.Combine(AppContext.BaseDirectory, "scylla.exe");
            if (!File.Exists(executable)) executable = Environment.ProcessPath ?? executable;
            if (!File.Exists(executable)) throw new FileNotFoundException("Scylla executable was not found.", executable);

            var id = Guid.NewGuid().ToString("N");
            if (Process.Start(CreateStartInfo(executable, id)) is null)
                throw new InvalidOperationException("Windows did not start the new Scylla process.");
            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return false;
        }
    }

    private static void SeedSettings(string appData, string windowData)
    {
        var target = Path.Combine(windowData, "settings.json");
        if (File.Exists(target)) return;
        var source = Path.Combine(appData, "settings.json");
        if (!File.Exists(source)) return;

        try
        {
            var root = JsonNode.Parse(File.ReadAllText(source))?.AsObject();
            if (root is null) return;
            root["project_folder"] = "";
            root["last_thread_id"] = "";
            root["pinned_tabs"] = new JsonArray();
            root["drafts"] = new JsonObject();
            WriteNewFile(target, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (JsonException)
        {
            // A malformed primary settings file must not prevent a clean secondary window.
        }
    }

    private static void SeedCodexAuthentication(string appData, string windowData)
    {
        var source = Path.Combine(appData, "codex-home", "auth.json");
        var target = Path.Combine(windowData, "codex-home", "auth.json");
        if (!File.Exists(source) || File.Exists(target)) return;
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.Copy(source, target, overwrite: false);
    }

    private static void WriteNewFile(string path, string body)
    {
        var temporary = path + "." + Environment.ProcessId + ".tmp";
        File.WriteAllText(temporary, body);
        try { File.Move(temporary, path, overwrite: false); }
        catch (IOException) when (File.Exists(path)) { File.Delete(temporary); }
    }
}
