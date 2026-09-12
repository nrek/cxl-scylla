Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
function Fg($h) { [W]::ShowWindow($h,6)|Out-Null; Start-Sleep -Milliseconds 500; [W]::ShowWindow($h,9)|Out-Null; [W]::SetForegroundWindow($h)|Out-Null; Start-Sleep -Milliseconds 1200 }
function ClickAbs($x,$y) { [W]::SetCursorPos($x,$y)|Out-Null; Start-Sleep -Milliseconds 350; [W]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); [W]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 800 }
function Shot($h,$path) {
  $r = New-Object W+RECT; [W]::GetWindowRect($h,[ref]$r)|Out-Null
  $w=$r.R-$r.L; $ht=$r.B-$r.T
  $bmp = New-Object System.Drawing.Bitmap $w,$ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc=$g.GetHdc(); [W]::PrintWindow($h,$hdc,2)|Out-Null; $g.ReleaseHdc($hdc)
  $bmp.Save($path,[System.Drawing.Imaging.ImageFormat]::Png)
}
Start-Process 'D:\projects\cxl-scylla\scylla-fluent\bin\x64\Release\net8.0-windows10.0.19041.0\scylla.exe'
Start-Sleep -Seconds 5
$p = Get-Process scylla
$h = $p.MainWindowHandle
Fg $h
$r = New-Object W+RECT; [W]::GetWindowRect($h,[ref]$r)|Out-Null
ClickAbs ($r.L+22) ($r.T+37)
ClickAbs ($r.L+65) ($r.T+186)
Start-Sleep -Milliseconds 1000
Shot $h "D:\projects\cxl-scylla\.tmpcl\ui-settings.png"
"win $($r.L),$($r.T)"
