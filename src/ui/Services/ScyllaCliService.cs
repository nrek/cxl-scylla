using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Scylla.UI.Models;

namespace Scylla.UI.Services;

public sealed class ScyllaCliService
{
    public string EnginePath { get; }

    public ScyllaCliService()
    {
        EnginePath = FindEngine() ?? throw new InvalidOperationException("SCYLLA: APPLICATION NOT READY — scylla.exe not found next to Scylla.UI.exe (build Release native engine first).");
    }

    public static string? FindEngine()
    {
        var env = Environment.GetEnvironmentVariable("SCYLLA_ENGINE");
        if (!string.IsNullOrWhiteSpace(env) && File.Exists(env))
        {
            return env;
        }
        var dir = AppContext.BaseDirectory;
        var next = Path.Combine(dir, "scylla.exe");
        if (File.Exists(next))
        {
            return next;
        }
        var release = Path.GetFullPath(Path.Combine(dir, "..", "..", "..", "..", "..", "build", "Release", "scylla.exe"));
        return File.Exists(release) ? release : null;
    }

    public async Task<EngineEvent> StatusAsync(CancellationToken ct = default)
    {
        return await RunOnceAsync(new[] { "strict", "status", "--json" }, ct);
    }

    public async Task<EngineEvent> StopAsync(CancellationToken ct = default)
    {
        return await RunOnceAsync(new[] { "strict", "stop", "--json" }, ct);
    }

    public async Task<EngineEvent> AuditAsync(string exe, CancellationToken ct = default)
    {
        return await RunOnceAsync(new[] { "audit", "--app", exe, "--json" }, ct);
    }

    public async Task<DiscoverResult> DiscoverAsync(CancellationToken ct = default)
    {
        var psi = new ProcessStartInfo(EnginePath)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
        };
        psi.ArgumentList.Add("discover");
        psi.ArgumentList.Add("--json");
        using var p = new Process { StartInfo = psi };
        p.Start();
        var stdout = await p.StandardOutput.ReadToEndAsync(ct);
        await p.WaitForExitAsync(ct);
        var trimmed = stdout.Trim();
        if (string.IsNullOrWhiteSpace(trimmed))
        {
            return new DiscoverResult { Ok = false };
        }
        try
        {
            return JsonSerializer.Deserialize<DiscoverResult>(trimmed) ?? new DiscoverResult { Ok = false };
        }
        catch (JsonException)
        {
            return new DiscoverResult { Ok = false };
        }
    }

    public Process StartLaunch(string exe, IEnumerable<FilesystemGrant> grants, bool internet, string? extraArgs, bool killRelated = false)
    {
        var args = new List<string> { "strict", "launch", "--app", exe, "--json" };
        if (!internet)
        {
            args.Add("--no-internet");
        }
        if (killRelated)
        {
            args.Add("--kill-related");
        }
        foreach (var g in grants)
        {
            args.Add(g.Mode == GrantMode.ReadWrite ? "--allow-rw" : "--allow-ro");
            args.Add(g.Path);
        }
        if (!string.IsNullOrWhiteSpace(extraArgs))
        {
            args.Add("--args");
            args.Add(extraArgs);
        }
        var psi = new ProcessStartInfo(EnginePath)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };
        foreach (var a in args)
        {
            psi.ArgumentList.Add(a);
        }
        var p = new Process { StartInfo = psi, EnableRaisingEvents = true };
        if (!p.Start())
        {
            throw new InvalidOperationException("SCYLLA: APPLICATION NOT READY — failed to start scylla.exe");
        }
        return p;
    }

    public async Task<(int ExitCode, string Output)> IdentityLaunchAsync(string app, string? aumid, string password, CancellationToken ct = default)
    {
        var psi = new ProcessStartInfo(EnginePath)
        {
            UseShellExecute = false,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };
        psi.ArgumentList.Add("identity");
        psi.ArgumentList.Add("launch");
        psi.ArgumentList.Add("--app");
        psi.ArgumentList.Add(app);
        if (!string.IsNullOrWhiteSpace(aumid))
        {
            psi.ArgumentList.Add("--aumid");
            psi.ArgumentList.Add(aumid);
        }
        using var p = new Process { StartInfo = psi };
        if (!p.Start())
        {
            throw new InvalidOperationException("SCYLLA: APPLICATION NOT READY — failed to start scylla.exe");
        }
        await p.StandardInput.WriteLineAsync(password);
        p.StandardInput.Close();
        var stdout = await p.StandardOutput.ReadToEndAsync(ct);
        var stderr = await p.StandardError.ReadToEndAsync(ct);
        await p.WaitForExitAsync(ct);
        var text = (stdout + "\n" + stderr).Trim();
        return (p.ExitCode, text);
    }

    public IdentitySession StartIdentitySupervise(string user, string password, string app, string? aumid)
    {
        var engine = EnginePath;
        var usersRoot = Directory.GetParent(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile))?.FullName
            ?? @"C:\Users";
        var cwd = Path.Combine(usersRoot, user);
        var workDir = Directory.Exists(cwd) ? cwd : null;
        var explorerHandle = IntPtr.Zero;
        if (!string.IsNullOrWhiteSpace(aumid))
        {
            var explorer = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe");
            var ecmd = "\"" + explorer + "\" shell:AppsFolder\\" + aumid;
            var esi = new NativeLogon.StartupInfo { Cb = Marshal.SizeOf<NativeLogon.StartupInfo>() };
            if (!NativeLogon.CreateProcessWithLogonW(
                    user, ".", password, NativeLogon.LogonWithProfile, explorer, ecmd,
                    NativeLogon.CreateUnicodeEnvironment,
                    IntPtr.Zero, workDir, ref esi, out var epi))
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),
                    "CreateProcessWithLogonW(explorer AppsFolder) failed");
            }
            NativeLogon.CloseHandle(epi.Thread);
            explorerHandle = epi.Process;
        }
        var cmd = "\"" + engine + "\" identity supervise --user " + user;
        if (!string.IsNullOrWhiteSpace(aumid))
        {
            cmd += " --aumid " + aumid + " --harvest-only";
        }
        if (!string.IsNullOrWhiteSpace(app))
        {
            cmd += " --app \"" + app + "\"";
        }
        var si = new NativeLogon.StartupInfo { Cb = Marshal.SizeOf<NativeLogon.StartupInfo>() };
        if (!NativeLogon.CreateProcessWithLogonW(
                user, ".", password, NativeLogon.LogonWithProfile, engine, cmd,
                NativeLogon.CreateUnicodeEnvironment | NativeLogon.CreateNoWindow,
                IntPtr.Zero, workDir, ref si, out var pi))
        {
            if (explorerHandle != IntPtr.Zero)
            {
                NativeLogon.TerminateProcess(explorerHandle, 1);
                NativeLogon.CloseHandle(explorerHandle);
            }
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),
                "CreateProcessWithLogonW failed");
        }
        NativeLogon.CloseHandle(pi.Thread);
        return new IdentitySession(pi.Process, pi.ProcessId, explorerHandle);
    }

    public static string IdentityStopPath()
    {
        var pub = Environment.GetEnvironmentVariable("PUBLIC") ?? @"C:\Users\Public";
        return Path.Combine(pub, "Scylla", "identity-stop");
    }

    public static string IdentityStatusPath()
    {
        var pub = Environment.GetEnvironmentVariable("PUBLIC") ?? @"C:\Users\Public";
        return Path.Combine(pub, "Scylla", "identity-status.json");
    }

    public static EngineEvent? ParseLine(string? line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return null;
        }
        var t = line.Trim();
        if (!t.StartsWith('{'))
        {
            return null;
        }
        try
        {
            return JsonSerializer.Deserialize<EngineEvent>(t);
        }
        catch (JsonException)
        {
            return null;
        }
    }

    async Task<EngineEvent> RunOnceAsync(string[] args, CancellationToken ct)
    {
        var psi = new ProcessStartInfo(EnginePath)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
        };
        foreach (var a in args)
        {
            psi.ArgumentList.Add(a);
        }
        using var p = new Process { StartInfo = psi };
        p.Start();
        var stdout = await p.StandardOutput.ReadToEndAsync(ct);
        await p.WaitForExitAsync(ct);
        EngineEvent? last = null;
        foreach (var line in stdout.Split('\n'))
        {
            var ev = ParseLine(line);
            if (ev != null)
            {
                last = ev;
            }
        }
        return last ?? new EngineEvent
        {
            Ok = false,
            State = "ERROR",
            Code = "ENGINE_UNAVAILABLE",
            Message = string.IsNullOrWhiteSpace(stdout) ? "scylla.exe produced no JSON" : stdout.Trim(),
        };
    }
}
