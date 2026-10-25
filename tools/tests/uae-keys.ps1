param([int]$UaeProcessId,[string]$Keys)
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class KeyWin { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); }
"@
$h=(Get-Process -Id $UaeProcessId).MainWindowHandle
[void][KeyWin]::SetForegroundWindow($h); Start-Sleep -Milliseconds 400
foreach($k in $Keys.Split(',')){ [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 250 }
