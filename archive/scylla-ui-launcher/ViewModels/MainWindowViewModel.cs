using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Interop;
using System.Windows.Threading;
using Scylla.UI.Models;
using Scylla.UI.Services;

namespace Scylla.UI.ViewModels;

public sealed class MainWindowViewModel : INotifyPropertyChanged
{
    readonly ProfileService _profiles;
    readonly DialogService _dialogs;
    ScyllaCliService? _cli;
    IdentitySession? _identity;
    readonly DispatcherTimer _poll;
    bool _dirty;
    string _engineError = "";
    bool _settingSelection;
    string? _applicationId;
    string? _applicationKind;

    public MainWindowViewModel()
    {
        _profiles = new ProfileService();
        _dialogs = new DialogService();
        _poll = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(750) };
        _poll.Tick += async (_, _) => await PollStatusAsync();

        ApplicationsView = CollectionViewSource.GetDefaultView(Applications);
        AiApps = new ListCollectionView(Applications) { Filter = o => MatchesGroup(o, ai: true) };
        OtherApps = new ListCollectionView(Applications) { Filter = o => MatchesGroup(o, ai: false) };

        try
        {
            _cli = new ScyllaCliService();
        }
        catch (Exception ex)
        {
            _engineError = ex.Message;
        }

        BrowseCommand = new RelayCommand(Browse, () => !IsBusy);
        AddFolderCommand = new RelayCommand(AddFolder, () => !IsBusy);
        RemoveGrantCommand = new RelayCommand(_ => RemoveGrant(SelectedGrant), _ => !IsBusy && SelectedGrant != null);
        NewProfileCommand = new RelayCommand(NewProfile, () => !IsBusy);
        SaveProfileCommand = new RelayCommand(SaveProfile, () => !IsBusy);
        DeleteProfileCommand = new RelayCommand(DeleteProfile, () => !IsBusy && !string.IsNullOrWhiteSpace(SelectedProfileName));
        RunCommand = new RelayCommand(() => RunIsolatedUserAsync(), () => CanRun);
        LaunchAppCommand = new RelayCommand(LaunchFromIcon, _ => _cli != null);
        StopCommand = new RelayCommand(() => { _ = StopAsync(); }, () => CanStop);
        AuditCommand = new RelayCommand(() => { _ = AuditAsync(); }, () => !IsBusy && File.Exists(Executable));

        ReloadProfileList();
        if (string.IsNullOrEmpty(_engineError))
        {
            StatusLine = "READY";
        }
        else
        {
            StatusLine = "ERROR";
            Detail = _engineError;
        }

        if (_cli != null)
        {
            _ = LoadDiscoveryAsync();
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<string> ProfileNames { get; } = new();
    public ObservableCollection<FilesystemGrant> Grants { get; } = new();
    public ObservableCollection<DiscoveredApplication> Applications { get; } = new();
    public ICollectionView ApplicationsView { get; }
    public ICollectionView AiApps { get; }
    public ICollectionView OtherApps { get; }

    string _appFilter = "";
    public string AppFilter
    {
        get => _appFilter;
        set
        {
            var next = value ?? "";
            if (_appFilter == next) return;
            _appFilter = next;
            OnPropertyChanged();
            RefreshAppViews();
        }
    }

    DiscoveredApplication? _selectedApplication;
    public DiscoveredApplication? SelectedApplication
    {
        get => _selectedApplication;
        set
        {
            if (ReferenceEquals(_selectedApplication, value)) return;
            _selectedApplication = value;
            OnPropertyChanged();
            if (_settingSelection) return;
            if (value != null)
            {
                _applicationId = value.Id;
                _applicationKind = value.Kind;
                if (!string.IsNullOrWhiteSpace(value.Executable))
                {
                    _executable = value.Executable;
                    OnPropertyChanged(nameof(Executable));
                }
                IsolationMode = string.Equals(value.Compatibility, "cannot_contain", StringComparison.OrdinalIgnoreCase)
                    ? "Isolated User"
                    : "AppContainer";
                _dirty = true;
                RaiseCommands();
            }
        }
    }

    public bool CanEditApp => !SessionActive;

    string _selectedProfileName = "";
    public string SelectedProfileName
    {
        get => _selectedProfileName;
        set
        {
            if (_selectedProfileName == value) return;
            if (_dirty && !ConfirmDiscard())
            {
                OnPropertyChanged();
                return;
            }
            _selectedProfileName = value;
            OnPropertyChanged();
            if (!string.IsNullOrWhiteSpace(value) && ProfileNames.Contains(value))
            {
                ApplyProfile(_profiles.Load(value));
            }
            RaiseCommands();
        }
    }

    string _profileName = "New profile";
    public string ProfileName
    {
        get => _profileName;
        set { _profileName = value; _dirty = true; OnPropertyChanged(); }
    }

    string _executable = "";
    public string Executable
    {
        get => _executable;
        set { _executable = value; _dirty = true; OnPropertyChanged(); RaiseCommands(); }
    }

    public IReadOnlyList<string> IsolationModes { get; } = new[] { "AppContainer", "Isolated User" };

    string _isolationMode = "AppContainer";
    public string IsolationMode
    {
        get => _isolationMode;
        set
        {
            if (_isolationMode == value) return;
            _isolationMode = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(ShowFilesystemGrants));
            OnPropertyChanged(nameof(GrantsVisibility));
            RaiseCommands();
        }
    }

    public bool ShowFilesystemGrants =>
        !string.Equals(IsolationMode, "Isolated User", StringComparison.Ordinal);

    public Visibility GrantsVisibility =>
        ShowFilesystemGrants ? Visibility.Visible : Visibility.Collapsed;

    bool _internet = true;
    public bool Internet
    {
        get => _internet;
        set { _internet = value; _dirty = true; OnPropertyChanged(); }
    }

    bool _privateLan;
    public bool PrivateLan
    {
        get => _privateLan;
        set { _privateLan = value; _dirty = true; OnPropertyChanged(); }
    }

    bool _detectExternal = true;
    public bool DetectExternal
    {
        get => _detectExternal;
        set { _detectExternal = value; _dirty = true; OnPropertyChanged(); }
    }

    bool _killTree = true;
    public bool KillTree
    {
        get => _killTree;
        set { _killTree = value; _dirty = true; OnPropertyChanged(); }
    }

    bool _killRelated = true;
    public bool KillRelated
    {
        get => _killRelated;
        set { _killRelated = value; _dirty = true; OnPropertyChanged(); }
    }

    FilesystemGrant? _selectedGrant;
    public FilesystemGrant? SelectedGrant
    {
        get => _selectedGrant;
        set { _selectedGrant = value; OnPropertyChanged(); RaiseCommands(); }
    }

    string _statusLine = "READY";
    public string StatusLine
    {
        get => _statusLine;
        set { _statusLine = value; OnPropertyChanged(); OnPropertyChanged(nameof(StatusBrushKey)); }
    }

    public string StatusBrushKey => StatusLine switch
    {
        "DEGRADED" or "REFUSED" => "Warn",
        "CLEANUP REQUIRED" or "ERROR" => "Bad",
        "RUNNING" or "CONTAINED" or "CLEAN" => "Accent",
        _ => "Muted",
    };

    string _sessionId = "";
    public string SessionId { get => _sessionId; set { _sessionId = value; OnPropertyChanged(); } }

    int _contained;
    public int Contained { get => _contained; set { _contained = value; OnPropertyChanged(); } }

    int _external;
    public int External { get => _external; set { _external = value; OnPropertyChanged(); } }

    string _monitor = "";
    public string MonitorLine
    {
        get => _monitor;
        set { _monitor = value; OnPropertyChanged(); }
    }

    string _detail = "";
    public string Detail { get => _detail; set { _detail = value; OnPropertyChanged(); } }

    bool _sessionActive;
    public bool SessionActive
    {
        get => _sessionActive;
        set { _sessionActive = value; OnPropertyChanged(); RaiseCommands(); OnPropertyChanged(nameof(CanRun)); OnPropertyChanged(nameof(CanStop)); OnPropertyChanged(nameof(CanEditApp)); }
    }

    public bool IsBusy => SessionActive;
    public bool CanRun => !SessionActive && _cli != null && HasLaunchTarget;
    public bool CanStop => SessionActive;

    bool HasLaunchTarget =>
        string.Equals(SelectedApplication?.Id, "chatgpt", StringComparison.OrdinalIgnoreCase)
        || !string.IsNullOrWhiteSpace(SelectedApplication?.Aumid)
        || File.Exists(Executable);

    public ICommand BrowseCommand { get; }
    public ICommand AddFolderCommand { get; }
    public ICommand RemoveGrantCommand { get; }
    public ICommand NewProfileCommand { get; }
    public ICommand SaveProfileCommand { get; }
    public ICommand DeleteProfileCommand { get; }
    public ICommand LaunchAppCommand { get; }
    public ICommand RunCommand { get; }
    public ICommand StopCommand { get; }
    public ICommand AuditCommand { get; }

    void Browse()
    {
        var path = _dialogs.PickExecutable();
        if (path == null) return;
        var manual = new DiscoveredApplication
        {
            DisplayName = Path.GetFileName(path),
            Publisher = "Manual",
            Kind = "manual",
            Available = true,
            Status = "installed",
            Compatibility = "discovered",
            Executable = path,
            Group = "other",
            DiscoverySource = "browse",
        };
        var previous = Applications.Where(a => a.Kind == "manual").ToList();
        foreach (var old in previous)
        {
            Applications.Remove(old);
        }
        Applications.Add(manual);
        _applicationId = null;
        _applicationKind = "manual";
        _settingSelection = true;
        SelectedApplication = manual;
        _settingSelection = false;
        Executable = path;
    }

    void AddFolder()
    {
        var folders = _dialogs.PickFolders();
        if (folders.Count == 0) return;
        var accepted = new List<string>();
        foreach (var folder in folders)
        {
            var conflict = GrantConflict(folder, accepted);
            if (conflict != null)
            {
                _dialogs.Error("Grant conflict", conflict);
                continue;
            }
            accepted.Add(folder);
        }
        if (accepted.Count == 0) return;
        var broad = accepted.Where(IsBroad).ToList();
        if (broad.Count > 0 && !_dialogs.Confirm("Broad filesystem grant",
                $"This application will be able to modify every file under:\n\n{string.Join("\n", broad)}\n\nGrant anyway?"))
        {
            return;
        }
        var mode = _dialogs.AskGrantMode(accepted);
        if (mode == null) return;
        foreach (var folder in accepted)
        {
            Grants.Add(new FilesystemGrant
            {
                Path = folder,
                Mode = mode.ReadWrite ? GrantMode.ReadWrite : GrantMode.ReadOnly,
            });
        }
        _dirty = true;
        RaiseCommands();
    }

    void RemoveGrant(FilesystemGrant? g)
    {
        g ??= SelectedGrant;
        if (g == null) return;
        Grants.Remove(g);
        SelectedGrant = null;
        _dirty = true;
        RaiseCommands();
    }

    void NewProfile()
    {
        if (_dirty && !ConfirmDiscard()) return;
        ProfileName = "New profile";
        Executable = "";
        _applicationId = null;
        _applicationKind = null;
        _settingSelection = true;
        SelectedApplication = null;
        _settingSelection = false;
        Grants.Clear();
        Internet = true;
        PrivateLan = false;
        DetectExternal = true;
        KillTree = true;
        KillRelated = true;
        _selectedProfileName = "";
        _dirty = false;
        OnPropertyChanged(nameof(SelectedProfileName));
        StatusLine = _cli == null ? "ERROR" : "READY";
        Detail = _engineError;
    }

    void SaveProfile()
    {
        var p = ToProfile();
        _profiles.Save(p);
        _dirty = false;
        ReloadProfileList();
        _selectedProfileName = p.Name;
        OnPropertyChanged(nameof(SelectedProfileName));
        Detail = "Profile saved to %LOCALAPPDATA%\\Scylla\\profiles\\";
    }

    void DeleteProfile()
    {
        if (string.IsNullOrWhiteSpace(SelectedProfileName)) return;
        if (!_dialogs.Confirm("Delete profile", $"Delete {SelectedProfileName}?")) return;
        _profiles.Delete(SelectedProfileName);
        ReloadProfileList();
        NewProfile();
    }

    bool MatchesAppFilter(object obj)
    {
        if (obj is not DiscoveredApplication a) return false;
        if (string.IsNullOrWhiteSpace(_appFilter)) return true;
        var q = _appFilter.Trim();
        return (a.DisplayName?.Contains(q, StringComparison.OrdinalIgnoreCase) ?? false)
            || (a.Id?.Contains(q, StringComparison.OrdinalIgnoreCase) ?? false)
            || (a.Publisher?.Contains(q, StringComparison.OrdinalIgnoreCase) ?? false);
    }

    bool MatchesGroup(object obj, bool ai)
    {
        if (!MatchesAppFilter(obj)) return false;
        if (obj is not DiscoveredApplication a) return false;
        var isAi = string.Equals(a.Group, "ai", StringComparison.OrdinalIgnoreCase);
        return ai ? isAi : !isAi;
    }

    void RefreshAppViews()
    {
        ApplicationsView.Refresh();
        AiApps.Refresh();
        OtherApps.Refresh();
    }

    public void LaunchFromIcon(object? o)
    {
        if (o is not DiscoveredApplication a)
        {
            Detail = "Click did not resolve an application.";
            return;
        }
        if (_cli == null)
        {
            _dialogs.Error("SCYLLA: REFUSED", string.IsNullOrWhiteSpace(_engineError) ? "scylla.exe not found." : _engineError);
            return;
        }
        if (SessionActive)
        {
            _dialogs.Info("Session active", "STOP APP first, then launch again.");
            return;
        }
        SelectedApplication = a;
        RunIsolatedUserAsync();
    }

    void RunIsolatedUserAsync()
    {
        if (_cli == null) return;
        if (SessionActive)
        {
            _dialogs.Info("Session active", "STOP APP first, then launch again.");
            return;
        }
        var appArg = string.Equals(SelectedApplication?.Id, "chatgpt", StringComparison.OrdinalIgnoreCase)
            ? "chatgpt"
            : Executable;
        var aumid = SelectedApplication?.Aumid;
        if (string.IsNullOrWhiteSpace(aumid) && string.Equals(appArg, "chatgpt", StringComparison.OrdinalIgnoreCase))
        {
            aumid = "OpenAI.Codex_2p2nqsd0c76g0!App";
        }
        var storeApp = !string.IsNullOrWhiteSpace(aumid)
            || string.Equals(SelectedApplication?.Kind, "msix", StringComparison.OrdinalIgnoreCase)
            || string.Equals(SelectedApplication?.Compatibility, "cannot_contain", StringComparison.OrdinalIgnoreCase);
        if (storeApp)
        {
            var scyllaSession = NativeLogon.SessionIdForUser("ScyllaUser");
            if (scyllaSession is null)
            {
                StatusLine = "REFUSED";
                Detail = "Store ChatGPT cannot put a window on this desktop. VLC and other Win32 apps can. Switch user to ScyllaUser, stay signed in, then click ChatGPT — the window opens on that desktop.";
                _dialogs.Error("SCYLLA: REFUSED", Detail);
                return;
            }
            if (!_dialogs.Confirm("Store app desktop",
                    $"ChatGPT will open on the ScyllaUser desktop (session {scyllaSession}), not this one. Switch user to see it. Continue?"))
            {
                return;
            }
        }
        var password = _dialogs.AskPassword(
            "Launch as ScyllaUser",
            "Password for ScyllaUser. The app runs as that account. Closing Scylla or STOP kills its process tree.");
        if (password == null)
        {
            return;
        }
        if (string.IsNullOrEmpty(password))
        {
            _dialogs.Error("SCYLLA: REFUSED", "Empty password.");
            return;
        }
        SessionActive = true;
        StatusLine = "STARTING";
        Detail = "Launching as ScyllaUser…";
        MonitorLine = "";
        RaiseCommands();
        try
        {
            _identity = _cli.StartIdentitySupervise("ScyllaUser", password, appArg, aumid);
            StatusLine = "STARTING";
            Detail = string.IsNullOrWhiteSpace(aumid)
                ? $"Supervisor PID {_identity.Pid}"
                : $"Started explorer AppsFolder as ScyllaUser. Supervisor PID {_identity.Pid}";
            _poll.Start();
        }
        catch (Exception ex)
        {
            SessionActive = false;
            StatusLine = "ERROR";
            Detail = ex.Message;
            _dialogs.Error("SCYLLA: REFUSED", ex.Message);
            RaiseCommands();
        }
    }

    public void ShutdownSession()
    {
        _poll.Stop();
        try
        {
            var stop = ScyllaCliService.IdentityStopPath();
            Directory.CreateDirectory(Path.GetDirectoryName(stop)!);
            File.WriteAllText(stop, "stop");
            if (_identity != null && _identity.ProcessHandle != IntPtr.Zero)
            {
                NativeLogon.WaitForSingleObject(_identity.ProcessHandle, 2000);
            }
        }
        catch
        {
            // best-effort cooperative stop
        }
        _identity?.Kill();
        _identity?.Dispose();
        _identity = null;
        SessionActive = false;
        StatusLine = "READY";
        MonitorLine = "";
        RaiseCommands();
    }

    async Task RunAsync()
    {
        RunIsolatedUserAsync();
        await Task.CompletedTask;
    }

    async Task StopAsync()
    {
        ShutdownSession();
        StatusLine = "READY";
        Detail = "Process tree stopped.";
        await Task.CompletedTask;
    }

    async Task PollStatusAsync()
    {
        if (_identity != null)
        {
            if (_identity.HasExited())
            {
                var code = _identity.ExitCode();
                var err = "";
                try
                {
                    var path = ScyllaCliService.IdentityStatusPath();
                    if (File.Exists(path))
                    {
                        var json = await File.ReadAllTextAsync(path);
                        var st = JsonSerializer.Deserialize<IdentityStatusFile>(json);
                        if (st != null && !string.IsNullOrWhiteSpace(st.Error))
                        {
                            err = st.Error;
                        }
                    }
                }
                catch
                {
                    // status file is best-effort
                }
                _identity.Dispose();
                _identity = null;
                SessionActive = false;
                _poll.Stop();
                MonitorLine = "";
                if (code is > 0 || !string.IsNullOrWhiteSpace(err))
                {
                    StatusLine = "REFUSED";
                    Detail = string.IsNullOrWhiteSpace(err)
                        ? $"Session ended (exit {code})."
                        : err;
                }
                else
                {
                    StatusLine = "READY";
                    Detail = "Session ended.";
                }
                RaiseCommands();
                return;
            }
            try
            {
                var path = ScyllaCliService.IdentityStatusPath();
                if (File.Exists(path))
                {
                    var json = await File.ReadAllTextAsync(path);
                    var st = JsonSerializer.Deserialize<IdentityStatusFile>(json);
                    if (st != null)
                    {
                        Contained = st.Pids.Count;
                        var ramMb = st.RamBytes / (1024.0 * 1024.0);
                        MonitorLine = $"RAM {ramMb:0.0} MB    TCP conns {st.Tcp}    PIDs {st.Pids.Count}";
                        if (!string.IsNullOrWhiteSpace(st.State))
                        {
                            StatusLine = st.State == "RUNNING" ? "RUNNING — ISOLATED USER" : st.State;
                        }
                    }
                }
            }
            catch
            {
                // next tick
            }
            return;
        }
        if (_cli == null || !SessionActive) return;
        try
        {
            var ev = await _cli.StatusAsync();
            ApplyEngine(ev);
            if (ev.State is "READY" or "CLEAN" or "CLEANUP_REQUIRED")
            {
                SessionActive = false;
                _poll.Stop();
            }
        }
        catch
        {
            // keep last known state; next tick retries
        }
    }

    async Task AuditAsync()
    {
        if (_cli == null) return;
        var ev = await _cli.AuditAsync(Executable);
        var extra = ev.Details.Count == 0
            ? "No related processes running."
            : string.Join("\n", ev.Details.Select(d => $"{d.Image} PID {d.Pid}"));
        _dialogs.Info("SCYLLA APPLICATION AUDIT",
            $"{ev.State}\n{ev.Message}\nStartup/tasks/services: UNVERIFIED\n\n{extra}");
    }

    void ApplyEngine(EngineEvent ev)
    {
        SessionId = ev.SessionId ?? SessionId;
        Contained = ev.ContainedProcessCount;
        External = ev.ExternalRelatedProcessCount;
        var state = (ev.State ?? "").ToUpperInvariant();
        StatusLine = state switch
        {
            "RUNNING" => "CONTAINED",
            "CLEANUP_REQUIRED" => "CLEANUP REQUIRED",
            "" => StatusLine,
            _ => state,
        };
        if (ev.Details.Count > 0)
        {
            Detail = string.Join("\n", ev.Details.Select(d =>
                $"{d.Image} PID {d.Pid}  contained:{(d.Contained ? "YES" : "NO")}"));
        }
        else if (!string.IsNullOrWhiteSpace(ev.Message))
        {
            Detail = ev.Message ?? "";
        }
        if (state == "REFUSED")
        {
            SessionActive = false;
            _poll.Stop();
            _dialogs.Error("SCYLLA: STRICT PREFLIGHT REFUSED", ev.Message ?? "Related processes already running.");
        }
        if (state == "CLEANUP_REQUIRED")
        {
            _dialogs.Error("SCYLLA: CLEANUP REQUIRED", ev.Message ?? "One or more temporary security grants could not be removed.");
        }
        RaiseCommands();
    }

    ScyllaProfile ToProfile() => new()
    {
        Name = ProfileName,
        Application = new ApplicationRef
        {
            Id = _applicationId,
            Executable = Executable,
            Kind = _applicationKind,
        },
        WorkingDirectory = Grants.FirstOrDefault(g => g.Mode == GrantMode.ReadWrite)?.Path,
        Filesystem = Grants.Select(g => new GrantDto
        {
            Path = g.Path,
            Mode = g.Mode == GrantMode.ReadWrite ? "rw" : "ro",
        }).ToList(),
        Network = new NetworkPolicy { Internet = Internet, PrivateLan = PrivateLan },
        Strict = new StrictSettings
        {
            Enabled = true,
            DetectExternalRelatedProcesses = DetectExternal,
            KillContainedTreeOnExit = KillTree,
            KillRelatedProcessesOnLaunch = KillRelated,
        },
    };

    void ApplyProfile(ScyllaProfile p)
    {
        ProfileName = p.Name;
        _applicationId = p.Application.Id;
        _applicationKind = p.Application.Kind;
        Internet = p.Network.Internet;
        PrivateLan = p.Network.PrivateLan;
        DetectExternal = p.Strict.DetectExternalRelatedProcesses;
        KillTree = p.Strict.KillContainedTreeOnExit;
        KillRelated = p.Strict.KillRelatedProcessesOnLaunch;
        Grants.Clear();
        foreach (var g in p.Filesystem)
        {
            Grants.Add(new FilesystemGrant
            {
                Path = g.Path,
                Mode = g.Mode.Equals("rw", StringComparison.OrdinalIgnoreCase) ? GrantMode.ReadWrite : GrantMode.ReadOnly,
            });
        }
        ResolveApplication(p.Application.Executable);
        _dirty = false;
        RaiseCommands();
    }

    void ResolveApplication(string fallbackExe)
    {
        DiscoveredApplication? match = null;
        if (!string.IsNullOrWhiteSpace(_applicationId))
        {
            match = Applications.FirstOrDefault(a =>
                string.Equals(a.Id, _applicationId, StringComparison.OrdinalIgnoreCase));
            if (match == null && Applications.Count > 0)
            {
                _dialogs.Error("SCYLLA: APPLICATION NOT FOUND",
                    $"{_applicationId} is not installed or could not be resolved.");
            }
        }
        match ??= Applications.FirstOrDefault(a =>
            !string.IsNullOrWhiteSpace(fallbackExe) &&
            string.Equals(a.Executable, fallbackExe, StringComparison.OrdinalIgnoreCase));
        _settingSelection = true;
        SelectedApplication = match;
        _settingSelection = false;
        Executable = !string.IsNullOrWhiteSpace(match?.Executable) ? match.Executable! : fallbackExe;
    }

    async Task LoadDiscoveryAsync()
    {
        if (_cli == null) return;
        try
        {
            var result = await _cli.DiscoverAsync();
            Applications.Clear();
            foreach (var a in result.Applications.Where(x => x.Available))
            {
                a.Icon = IconFor(a.Executable);
                Applications.Add(a);
            }
            RefreshAppViews();
            ResolveApplication(Executable);
        }
        catch (Exception ex)
        {
            Detail = "Discovery failed: " + ex.Message;
        }
    }

    void ReloadProfileList()
    {
        ProfileNames.Clear();
        foreach (var n in _profiles.ListNames())
        {
            ProfileNames.Add(n);
        }
    }

    bool ConfirmDiscard() => !_dirty || _dialogs.Confirm("Unsaved changes", "Discard unsaved profile changes?");

    string? GrantConflict(string folder, IEnumerable<string>? pending = null)
    {
        var full = Path.GetFullPath(folder).TrimEnd('\\') + "\\";
        foreach (var g in Grants)
        {
            var existing = Path.GetFullPath(g.Path).TrimEnd('\\') + "\\";
            if (full.StartsWith(existing, StringComparison.OrdinalIgnoreCase) ||
                existing.StartsWith(full, StringComparison.OrdinalIgnoreCase))
            {
                return $"{folder} overlaps an existing grant for {g.Path}.\nThe broader grant controls access.";
            }
        }
        if (pending != null)
        {
            foreach (var p in pending)
            {
                var existing = Path.GetFullPath(p).TrimEnd('\\') + "\\";
                if (full.StartsWith(existing, StringComparison.OrdinalIgnoreCase) ||
                    existing.StartsWith(full, StringComparison.OrdinalIgnoreCase))
                {
                    return $"{folder} overlaps another selected folder {p}.\nThe broader grant controls access.";
                }
            }
        }
        return null;
    }

    static bool IsBroad(string folder)
    {
        var p = Path.GetFullPath(folder).TrimEnd('\\');
        var parts = p.Split(new[] { '\\' }, StringSplitOptions.RemoveEmptyEntries);
        return parts.Length <= 2;
    }

    static ImageSource? IconFor(string? exe)
    {
        if (string.IsNullOrWhiteSpace(exe) || !File.Exists(exe))
        {
            return null;
        }
        var h = NativeLogon.ExtractIcon(IntPtr.Zero, exe, 0);
        if (h == IntPtr.Zero)
        {
            return null;
        }
        try
        {
            var src = Imaging.CreateBitmapSourceFromHIcon(h, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
            src.Freeze();
            return src;
        }
        catch
        {
            return null;
        }
        finally
        {
            NativeLogon.DestroyIcon(h);
        }
    }

    void RaiseCommands()
    {
        OnPropertyChanged(nameof(CanRun));
        OnPropertyChanged(nameof(CanStop));
        OnPropertyChanged(nameof(IsBusy));
        (BrowseCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (AddFolderCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (RemoveGrantCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (LaunchAppCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (RunCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (StopCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (AuditCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (NewProfileCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (SaveProfileCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (DeleteProfileCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
