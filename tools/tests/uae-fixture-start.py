#!/usr/bin/env python3
"""Start an executable in the UE1 WinUAE fixture and leave it running.

usage: uae-fixture-start.py <exe> [extra args]
Prints the WinUAE PID once music plays (for uae-keys.ps1 / uae-shot.ps1).
Retries when the game does not start within 60 s (known WinUAE flakiness).
"""
import pathlib, shutil, subprocess, sys, time
fx = pathlib.Path('/mnt/d/dev/uae-tests/ue1-rc17-hang.470qbt')
log = fx / 'System/Unreal.log'
shutil.copyfile(sys.argv[1], fx / 'System/Unreal-buffer')
start = (fx / 'Start-64').read_text()
if len(sys.argv) > 2:
    start = start.replace('-MGLFILEBUFFER=64', '-MGLFILEBUFFER=64 ' + ' '.join(sys.argv[2:]))
(fx / 'Start-Test').write_text(start)
def ps(s): return subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', s], text=True).strip()
for attempt in range(3):
    old = log.stat().st_mtime_ns if log.exists() else None
    pid = int(ps('$p=Start-Process -FilePath "D:\\Amiga\\AmiKit X\\WinUAE\\winuae.exe" -WorkingDirectory "D:\\Amiga\\AmiKit X\\WinUAE" -ArgumentList @("-f","D:\\dev\\uae-tests\\ue1-rc17-hang.470qbt\\test.uae") -PassThru; $p.Id'))
    t0 = time.monotonic()
    while time.monotonic() - t0 < 60:
        time.sleep(0.5)
        if log.exists() and log.stat().st_mtime_ns != old:
            t = log.read_text(errors='replace')
            if 'Critical:' in t: print('CRASH'); sys.exit(1)
            if 'Music started:' in t:
                time.sleep(3); print(pid); sys.exit(0)
    ps(f'Stop-Process -Id {pid} -Force -ErrorAction SilentlyContinue; exit 0')
print('did not start'); sys.exit(2)
