Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
$p = Get-Process scylla
$h = $p.MainWindowHandle
[W]::ShowWindow($h,6)|Out-Null; Start-Sleep -Milliseconds 500
[W]::ShowWindow($h,9)|Out-Null; [W]::SetForegroundWindow($h)|Out-Null; Start-Sleep -Milliseconds 1500
$r = New-Object W+RECT; [W]::GetWindowRect($h,[ref]$r)|Out-Null
$w=$r.R-$r.L; $ht=$r.B-$r.T
$bmp = New-Object System.Drawing.Bitmap $w,$ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size $w,$ht))
$bmp.Save("D:\projects\cxl-scylla\.tmpcl\ui-live.png",[System.Drawing.Imaging.ImageFormat]::Png)
"rect $($r.L),$($r.T) ${w}x${ht}  fg=$([W]::GetForegroundWindow() -eq $h)"
