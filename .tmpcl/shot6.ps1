Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
function ClickAbs($x,$y) { [W]::SetCursorPos($x,$y)|Out-Null; Start-Sleep -Milliseconds 300; [W]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); [W]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 600 }
$p = Get-Process scylla
$h = $p.MainWindowHandle
$r = New-Object W+RECT; [W]::GetWindowRect($h,[ref]$r)|Out-Null
# back to editor, then focus composer and type
ClickAbs ($r.L+294) ($r.T+139)
ClickAbs ($r.L+640) ($r.T+560)
[System.Windows.Forms.SendKeys]::SendWait("Draft a migration plan for the stones backfill")
Start-Sleep -Milliseconds 900
$w=$r.R-$r.L; $ht=$r.B-$r.T
$bmp = New-Object System.Drawing.Bitmap $w,$ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc=$g.GetHdc(); [W]::PrintWindow($h,$hdc,2)|Out-Null; $g.ReleaseHdc($hdc)
$bmp.Save("D:\projects\cxl-scylla\.tmpcl\ui-composer.png",[System.Drawing.Imaging.ImageFormat]::Png)
"ok"
