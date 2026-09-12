Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
$p = Get-Process scylla
$h = $p.MainWindowHandle
[W]::ShowWindow($h, 6) | Out-Null
Start-Sleep -Milliseconds 600
[W]::ShowWindow($h, 9) | Out-Null
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 1200
"fg-match: $([W]::GetForegroundWindow() -eq $h)"
$r = New-Object W+RECT
[W]::GetWindowRect($h, [ref]$r) | Out-Null
# open File menu
[W]::SetCursorPos($r.L + 22, $r.T + 37) | Out-Null
Start-Sleep -Milliseconds 250
[W]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); [W]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 900
# full desktop capture so the flyout is visible
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.X, $b.Y, 0, 0, $b.Size)
$bmp.Save("D:\projects\cxl-scylla\.tmpcl\ui-menu.png", [System.Drawing.Imaging.ImageFormat]::Png)
"win: $($r.L),$($r.T) $($b.Width)x$($b.Height)"
