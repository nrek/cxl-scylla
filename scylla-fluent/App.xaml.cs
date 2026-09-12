using System.Runtime.ExceptionServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Scylla;

public partial class App : Application
{
    internal static bool UiStressMode => Environment.GetCommandLineArgs().Contains("--ui-stress-test");
    private Window? _window;
    private static string LogPath => UiStressMode ? Path.Combine(AppContext.BaseDirectory, "ui-stress.log") :
        WindowInstance.StatePath("fluent-launch.log");

    public App()
    {
        try { WindowInstance.InitializeCurrentProcess(Environment.GetCommandLineArgs()); }
        catch (Exception ex) { Log($"secondary state initialization: {ex}"); }
        InitializeComponent();
        UnhandledException += (_, e) =>
        {
            Log($"UnhandledException handled={e.Handled}: {e.Message}\n{e.Exception}");
            // Do not keep a broken composition tree alive behind a black window.
            if (UiStressMode) Environment.Exit(3);
        };
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            Log($"AppDomain: {e.ExceptionObject}");
        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            Log($"UnobservedTask: {e.Exception}");
            e.SetObserved();
        };
    }

    internal static void Log(string msg)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
            File.AppendAllText(LogPath,
                $"{DateTime.UtcNow:O} {msg}\n", Encoding.UTF8);
        }
        catch { /* ignore */ }
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Log("OnLaunched begin");
        try
        {
            ApplyInstallerConfiguration();
            if (UiStressMode)
            {
                _window = new WorkbenchWindow(validation: true);
                _window.Activate();
                UiStressTest.Run((WorkbenchWindow)_window);
                return;
            }
            try
            {
                if (!WindowInstance.IsSecondary)
                {
                    var acquired = NativeCore.TryAcquireInstance();
                    Log($"TryAcquireInstance={acquired}");
                    if (acquired == 0)
                    {
                        NativeCore.ActivateExisting();
                        Log("ActivateExisting + Exit");
                        Exit();
                        return;
                    }
                }
                else Log($"Secondary window id={WindowInstance.CurrentId}");
            }
            catch (DllNotFoundException ex)
            {
                Log($"DllNotFoundException: {ex.Message}");
            }
            catch (Exception ex)
            {
                Log($"instance acquire: {ex}");
            }

            Log("new WorkbenchWindow");
            _window = new WorkbenchWindow();
            Log("Activate");
            ApplyWindowIcon(_window);
            _window.Activate();
            Log("OnLaunched done");
        }
        catch (Exception ex)
        {
            Log($"OnLaunched FATAL: {ex}");
            // Last-resort visible window so launch isn't a silent flash-exit.
            var fallback = new Window { Title = "Scylla — launch error" };
            fallback.Content = new TextBlock
            {
                Text = ex.ToString(),
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(16),
            };
            _window = fallback;
            ApplyWindowIcon(fallback);
            fallback.Activate();
        }
    }

    private static void ApplyInstallerConfiguration()
    {
        const string keyPath = @"Software\CXL\Scylla\InstallerConfiguration";
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(keyPath, writable: true);
            if (key?.GetValue("Pending") is not int pending || pending != 1) return;

            var enabled = string.Equals(key.GetValue("StrataEnabled")?.ToString(), "1", StringComparison.Ordinal);
            var mode = key.GetValue("StrataMode")?.ToString() == "team" ? "team" : "solo";
            var endpoint = key.GetValue("StrataEndpoint")?.ToString()?.Trim() ?? "http://127.0.0.1:8765";
            if (mode == "team" && (!Uri.TryCreate(endpoint, UriKind.Absolute, out var uri) ||
                                   uri.Scheme != Uri.UriSchemeHttps))
            {
                mode = "solo";
                endpoint = "http://127.0.0.1:8765";
            }

            var appData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "ScyllaGPT");
            Directory.CreateDirectory(appData);
            var path = Path.Combine(appData, "strata.json");
            if (!File.Exists(path))
            {
                var value = new { version = 2, enabled, mode, endpoint, bindings = Array.Empty<object>() };
                File.WriteAllText(path, JsonSerializer.Serialize(value, new JsonSerializerOptions { WriteIndented = true }),
                    new UTF8Encoding(false));
            }
            key.DeleteValue("Pending", throwOnMissingValue: false);
        }
        catch (Exception ex)
        {
            Log($"installer configuration: {ex.Message}");
        }
    }

    private static void ApplyWindowIcon(Window window)
    {
        try
        {
            window.AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "scylla.ico"));
        }
        catch (Exception ex)
        {
            Log($"Set window icon: {ex}");
        }
    }
}
