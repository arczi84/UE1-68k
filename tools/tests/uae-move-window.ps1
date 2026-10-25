# Keeps a WinUAE window on the chosen monitor without activating it.
# -Target primary   : the 144 Hz primary (use for FPS measurements)
# -Target secondary : the 60 Hz second monitor (default for plain tests)
# -Target minimized : keep it minimized without activating it, so it never
#                     takes the user's focus or mouse (needs equal WinUAE
#                     active/inactive/iconified priorities in the config)
param([int]$UaeProcessId, [string]$Target = 'secondary')
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class UeWin {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
if ($Target -eq 'minimized') {
  for ($i = 0; $i -lt 6000; $i++) {
    $p = Get-Process -Id $UaeProcessId -ErrorAction SilentlyContinue
    if (-not $p) { exit }
    $h = $p.MainWindowHandle
    if ($h -ne [IntPtr]::Zero -and -not [UeWin]::IsIconic($h)) {
      [void][UeWin]::ShowWindow($h, 7)  # SW_SHOWMINNOACTIVE
    }
    Start-Sleep -Milliseconds 100
  }
  exit
}
$want = ($Target -eq 'primary')
$screen = [System.Windows.Forms.Screen]::AllScreens | Where-Object { $_.Primary -eq $want } | Select-Object -First 1
if (-not $screen) { exit }
$x = $screen.Bounds.X + 40; $y = $screen.Bounds.Y + 40
# WinUAE replaces its window when the emulation starts: keep it there.
for ($i = 0; $i -lt 480; $i++) {
  $p = Get-Process -Id $UaeProcessId -ErrorAction SilentlyContinue
  if (-not $p) { exit }
  $h = $p.MainWindowHandle
  if ($h -ne [IntPtr]::Zero) {
    $r = New-Object UeWin+RECT
    [void][UeWin]::GetWindowRect($h, [ref]$r)
    if ($r.Left -lt $screen.Bounds.X -or $r.Left -ge $screen.Bounds.Right -or $r.Top -lt $screen.Bounds.Y -or $r.Top -ge $screen.Bounds.Bottom) {
      [void][UeWin]::SetWindowPos($h, [IntPtr]::Zero, $x, $y, 0, 0, 0x0015)  # NOSIZE|NOZORDER|NOACTIVATE
    }
  }
  Start-Sleep -Milliseconds 250
}
