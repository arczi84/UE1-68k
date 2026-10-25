param([int]$UaeProcessId,[string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ShotWin {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$h=(Get-Process -Id $UaeProcessId).MainWindowHandle
$r=New-Object ShotWin+RECT; [void][ShotWin]::GetWindowRect($h,[ref]$r)
$w=$r.Right-$r.Left; $hh=$r.Bottom-$r.Top
$b=New-Object System.Drawing.Bitmap $w,$hh
$g=[System.Drawing.Graphics]::FromImage($b); $dc=$g.GetHdc()
[void][ShotWin]::PrintWindow($h,$dc,2); $g.ReleaseHdc($dc)
$b.Save($Out,[System.Drawing.Imaging.ImageFormat]::Png); "${w}x$hh"
