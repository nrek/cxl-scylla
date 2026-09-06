using System.Runtime.InteropServices;

namespace Scylla.UI.Services;

static class NativeLogon
{
    public const uint LogonWithProfile = 1;
    public const uint CreateUnicodeEnvironment = 0x00000400;
    public const uint CreateNoWindow = 0x08000000;
    public const uint StillActive = 259;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct StartupInfo
    {
        public int Cb;
        public string? Reserved;
        public string? Desktop;
        public string? Title;
        public int X, Y, XSize, YSize, XCountChars, YCountChars, FillAttribute, Flags;
        public short ShowWindow, Reserved2;
        public IntPtr Reserved3, StdInput, StdOutput, StdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ProcessInformation
    {
        public IntPtr Process;
        public IntPtr Thread;
        public int ProcessId;
        public int ThreadId;
    }

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool CreateProcessWithLogonW(
        string userName,
        string domain,
        string password,
        uint logonFlags,
        string? applicationName,
        string commandLine,
        uint creationFlags,
        IntPtr environment,
        string? currentDirectory,
        ref StartupInfo startupInfo,
        out ProcessInformation processInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetExitCodeProcess(IntPtr hProcess, out uint lpExitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool TerminateProcess(IntPtr hProcess, uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint WaitForSingleObject(IntPtr hHandle, uint milliseconds);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr ExtractIcon(IntPtr hInst, string lpszExe, int nIconIndex);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool DestroyIcon(IntPtr hIcon);

    [DllImport("wtsapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool WTSEnumerateSessions(IntPtr hServer, int reserved, int version, out IntPtr ppSessionInfo, out int count);

    [DllImport("wtsapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool WTSQuerySessionInformation(IntPtr hServer, int sessionId, int wtsInfoClass, out IntPtr ppBuffer, out int bytesReturned);

    [DllImport("wtsapi32.dll")]
    static extern void WTSFreeMemory(IntPtr memory);

    const int WtsUserName = 5;

    [StructLayout(LayoutKind.Sequential)]
    struct WtsSessionInfo
    {
        public int SessionId;
        public IntPtr WinStationName;
        public int State;
    }

    public static int? SessionIdForUser(string user)
    {
        if (string.IsNullOrWhiteSpace(user)) return null;
        if (!WTSEnumerateSessions(IntPtr.Zero, 0, 1, out var list, out var count) || list == IntPtr.Zero)
        {
            return null;
        }
        try
        {
            var stride = Marshal.SizeOf<WtsSessionInfo>();
            for (var i = 0; i < count; i++)
            {
                var info = Marshal.PtrToStructure<WtsSessionInfo>(list + (i * stride));
                if (!WTSQuerySessionInformation(IntPtr.Zero, info.SessionId, WtsUserName, out var namePtr, out _))
                {
                    continue;
                }
                var name = Marshal.PtrToStringUni(namePtr);
                WTSFreeMemory(namePtr);
                if (string.Equals(name, user, StringComparison.OrdinalIgnoreCase))
                {
                    return info.SessionId;
                }
            }
        }
        finally
        {
            WTSFreeMemory(list);
        }
        return null;
    }

    public const uint WaitObject0 = 0;
}

public sealed class IdentitySession : IDisposable
{
    public IntPtr ProcessHandle { get; }
    public IntPtr ExplorerHandle { get; }
    public int Pid { get; }

    public IdentitySession(IntPtr processHandle, int pid, IntPtr explorerHandle = default)
    {
        ProcessHandle = processHandle;
        Pid = pid;
        ExplorerHandle = explorerHandle;
    }

    public bool HasExited()
    {
        return NativeLogon.WaitForSingleObject(ProcessHandle, 0) == NativeLogon.WaitObject0;
    }

    public uint? ExitCode()
    {
        if (!NativeLogon.GetExitCodeProcess(ProcessHandle, out var code) || code == NativeLogon.StillActive)
        {
            return null;
        }
        return code;
    }

    public void Kill()
    {
        if (ProcessHandle != IntPtr.Zero)
        {
            NativeLogon.TerminateProcess(ProcessHandle, 1);
        }
        if (ExplorerHandle != IntPtr.Zero)
        {
            NativeLogon.TerminateProcess(ExplorerHandle, 1);
        }
    }

    public void Dispose()
    {
        if (ProcessHandle != IntPtr.Zero)
        {
            NativeLogon.CloseHandle(ProcessHandle);
        }
        if (ExplorerHandle != IntPtr.Zero)
        {
            NativeLogon.CloseHandle(ExplorerHandle);
        }
    }
}
