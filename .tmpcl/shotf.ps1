Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
Start-Process 'D:\projects\cxl-scylla\scylla-fluent\bin\x64\Release\net8.0-windows10.0.19041.0\scylla.exe'
Start-Sleep -Seconds 6
$p = Get-Process scylla
$h = $p.MainWindowHandle
[W]::ShowWindow($h,6)|Out-Null; Start-Sleep -Milliseconds 500
[W]::ShowWindow($h,9)|Out-Null; [W]::SetForegroundWindow($h)|Out-Null; Start-Sleep -Milliseconds 1500
$r = New-Object W+RECT; [W]::GetWindowRect($h,[ref]$r)|Out-Null
$w=$r.R-$r.L; $ht=$r.B-$r.T
$bmp = New-Object System.Drawing.Bitmap $w,$ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size $w,$ht))
$bmp.Save("D:\projects\cxl-scylla\.tmpcl\ui-final.png",[System.Drawing.Imaging.ImageFormat]::Png)
"ok"
