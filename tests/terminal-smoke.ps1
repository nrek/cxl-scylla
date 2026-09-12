param([string]$NativeDirectory = 'D:\projects\cxl-scylla\build\Release')
$ErrorActionPreference = 'Stop'
# Exercise the real C ABI with a hidden parent, without touching terminal settings.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TerminalSmoke {
    [DllImport("kernel32", CharSet=CharSet.Unicode)] public static extern bool SetDllDirectory(string path);
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr CreateWindowEx(int ex, string cls, string text, int style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr data);
    [DllImport("user32")] public static extern bool DestroyWindow(IntPtr hwnd);
    [DllImport("user32")] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
    [DllImport("user32")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32")] public static extern int GetWindowLong(IntPtr hwnd, int index);
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("scylla-core", CallingConvention=CallingConvention.Cdecl)] public static extern void scylla_terminal_set_visible(IntPtr terminal, int visible);
    [DllImport("scylla-core", CallingConvention=CallingConvention.Cdecl)] public static extern IntPtr scylla_terminal_create_profile(IntPtr parent, IntPtr notify, int id, [MarshalAs(UnmanagedType.LPUTF8Str)] string cwd, [MarshalAs(UnmanagedType.LPUTF8Str)] string profile, byte[] error, int size);
    [DllImport("scylla-core", CallingConvention=CallingConvention.Cdecl)] public static extern int scylla_core_terminal_profiles_json(byte[] buffer, int size);
    [DllImport("scylla-core", CallingConvention=CallingConvention.Cdecl)] public static extern int scylla_terminal_poll(IntPtr terminal, byte[] buffer, int size);
    [DllImport("scylla-core", CallingConvention=CallingConvention.Cdecl)] public static extern void scylla_terminal_destroy(IntPtr terminal);
    [DllImport("scylla-core", CallingConvention=CallingConvention.Cdecl)] public static extern int scylla_terminal_running(IntPtr terminal);
    [DllImport("scylla-core", CallingConvention=CallingConvention.Cdecl)] public static extern int scylla_terminal_write_utf8(IntPtr terminal, byte[] input);
}
'@
[void][TerminalSmoke]::SetDllDirectory($NativeDirectory)
$parent = [TerminalSmoke]::CreateWindowEx(0, 'STATIC', 'Terminal smoke test', 0, 0, 0, 640, 320, 0, 0, 0, 0)
if ($parent -eq 0) { throw 'Hidden test parent creation failed' }
$handles = [System.Collections.Generic.List[IntPtr]]::new()
try {
    $buffer = [byte[]]::new(65536)
    [void][TerminalSmoke]::scylla_core_terminal_profiles_json($buffer, $buffer.Length)
    $profiles = [Text.Encoding]::UTF8.GetString($buffer).TrimEnd([char]0) | ConvertFrom-Json
    $enabled = @($profiles.profiles | Where-Object enabled)
    $preferred = @($enabled | Where-Object id -EQ $profiles.default_id)
    $selected = if ($preferred.Count) { $preferred[0] } else { $enabled[0] }
    Write-Output "Resolved default: $($selected.name)"
    Write-Output "Profile id: $($selected.id); arguments: $($selected.args)"
    $errorBytes = [byte[]]::new(2048)
    $invalid = [TerminalSmoke]::scylla_terminal_create_profile($parent, $parent, 9300, $PWD.Path, '__missing_profile__', $errorBytes, $errorBytes.Length)
    if ($invalid -ne 0) { [TerminalSmoke]::scylla_terminal_destroy($invalid); throw 'Unknown profile unexpectedly launched a fallback' }
    Write-Output 'PASS: unknown explicit profile rejected'
    foreach ($profileId in @('', $selected.id)) {
        $terminal = [TerminalSmoke]::scylla_terminal_create_profile($parent, $parent, (9301 + $handles.Count), $PWD.Path, $profileId, $errorBytes, $errorBytes.Length)
        if ($terminal -eq 0) { throw [Text.Encoding]::UTF8.GetString($errorBytes).TrimEnd([char]0) }
        $handles.Add($terminal)
        $terminalWindow = [TerminalSmoke]::GetDlgItem($parent, 9300 + $handles.Count)
        if ($terminalWindow -eq 0) { throw 'Terminal child HWND missing' }
        $sibling = [TerminalSmoke]::CreateWindowEx(0, 'STATIC', 'Opaque content sibling', 0x50000000, 0, 0, 640, 320, $parent, 0, 0, 0)
        if ($sibling -eq 0) { throw 'Content sibling creation failed' }
        try {
            [TerminalSmoke]::scylla_terminal_set_visible($terminal, 0)
            [void][TerminalSmoke]::SetWindowPos($sibling, 0, 0, 0, 0, 0, 0x13)
            [TerminalSmoke]::scylla_terminal_set_visible($terminal, 1)
            if ([TerminalSmoke]::GetWindow($parent, 5) -ne $terminalWindow) { throw 'Terminal is obscured by its content sibling' }
            if (([TerminalSmoke]::GetWindowLong($terminalWindow, -16) -band 0x10000000) -eq 0) { throw 'Terminal is still hidden' }
            [TerminalSmoke]::scylla_terminal_set_visible($terminal, 0)
            if (([TerminalSmoke]::GetWindowLong($terminalWindow, -16) -band 0x10000000) -ne 0) { throw 'Terminal failed to hide for overlays' }
            [TerminalSmoke]::scylla_terminal_set_visible($terminal, 1)
            Write-Output 'PASS: terminal rises above content sibling and hides for overlays'
        }
        finally { [void][TerminalSmoke]::DestroyWindow($sibling) }
        $output = ''
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while ([DateTime]::UtcNow -lt $deadline -and ($output.Length -eq 0 -or [TerminalSmoke]::SendMessage($terminalWindow, 0x000E, 0, 0).ToInt64() -le 0)) {
            [Array]::Clear($buffer, 0, $buffer.Length)
            [void][TerminalSmoke]::scylla_terminal_poll($terminal, $buffer, $buffer.Length)
            $output += [Text.Encoding]::UTF8.GetString($buffer).TrimEnd([char]0)
            Start-Sleep -Milliseconds 50
        }
        if (!$output.Length -or ![TerminalSmoke]::scylla_terminal_running($terminal)) { throw "Default terminal has no startup output or exited: $output" }
        if ([TerminalSmoke]::SendMessage($terminalWindow, 0x000E, 0, 0).ToInt64() -le 0) { throw 'Shell output was not rendered into the terminal control' }
        Write-Output 'PASS: shell starts and poll drains startup output'
    }
    if ($selected.id -like 'wsl:*') {
        $inputWindow = [TerminalSmoke]::GetDlgItem($parent, 9301)
        foreach ($character in "printf 'SCYLLA_%s\n' 'TERMINAL_OK'".ToCharArray()) {
            [void][TerminalSmoke]::SendMessage($inputWindow, 0x0102, [int]$character, 0)
        }
        [void][TerminalSmoke]::SendMessage($inputWindow, 0x0100, 13, 0)
        $output = ''
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while ([DateTime]::UtcNow -lt $deadline -and $output -notmatch 'SCYLLA_TERMINAL_OK') {
            [Array]::Clear($buffer, 0, $buffer.Length)
            [void][TerminalSmoke]::scylla_terminal_poll($handles[0], $buffer, $buffer.Length)
            $output += [Text.Encoding]::UTF8.GetString($buffer).TrimEnd([char]0)
            Start-Sleep -Milliseconds 50
        }
        if ($output -notmatch 'SCYLLA_TERMINAL_OK') { throw 'WSL command output was not received' }
        Write-Output 'PASS: default WSL shell executes terminal control keystrokes and returns output'
    }
    foreach ($handle in $handles) {
        if (![TerminalSmoke]::scylla_terminal_running($handle)) { throw 'A concurrent terminal exited unexpectedly' }
    }
    Write-Output 'PASS: two independent terminals remain alive'
}
finally {
    foreach ($handle in $handles) { [TerminalSmoke]::scylla_terminal_destroy($handle) }
    [void][TerminalSmoke]::DestroyWindow($parent)
}
