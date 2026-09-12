Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
$p = Get-Process scylla
$h = $p.MainWindowHandle
[W]::ShowWindow($h,6)|Out-Null; Start-Sleep -Milliseconds 400
[W]::ShowWindow($h,9)|Out-Null; [W]::SetForegroundWindow($h)|Out-Null; Start-Sleep -Milliseconds 1000
$r = New-Object W+RECT; [W]::GetWindowRect($h,[ref]$r)|Out-Null
# Click Sign in near top-right of strip (~ model is left of Sign in)
$x = $r.R - 55
$y = $r.T + 78
[W]::SetCursorPos($x,$y)|Out-Null; Start-Sleep -Milliseconds 300
[W]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); [W]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 2500
$w=$r.R-$r.L; $ht=$r.B-$r.T
$bmp = New-Object System.Drawing.Bitmap $w,$ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size $w,$ht))
$bmp.Save("D:\projects\cxl-scylla\.tmpcl\ui-providers-auth.png",[System.Drawing.Imaging.ImageFormat]::Png)
"clicked $x,$y"
