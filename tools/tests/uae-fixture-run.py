#!/usr/bin/env python3
"""Start one UE1 executable in the isolated WinUAE fixture and wait for it.

usage: uae-fixture-run.py <exe> <label> [bench]

Without 'bench': OK once music has played for 15 s after startup.
With 'bench': waits for the AutoTimedemo result in System/fps_logs.
WinUAE sometimes does not start the game at all: no music within 60 s and no
'Critical:' counts as 'did not start' and is retried (up to 3 attempts); only
'Critical:' in Unreal.log counts as a failure of the executable.
Enforcer hits in winuaelog.txt are always reported (hits=N) and the log is
saved as claude-runs/<label>-winuaelog.txt; any hit makes the result HITS.
UE_UAE_CONFIG=test-slow.uae uses the throttled CPU config (~PiStorm speed).
UE_SCREEN=primary|secondary|minimized keeps the window on that monitor (primary = 144 Hz,
needed for real FPS; secondary = 60 Hz caps FPS). The display refresh WinUAE
reported is printed as vfreq=.
UE_ARGS adds command line arguments. UE_SHOT=<png> saves a window shot
after the fps file (and UE_POST). UE_POST=<s> waits extra seconds after the fps file (e.g. for gmon.out).
"""
import os, pathlib, re, shutil, subprocess, sys, time

fx = pathlib.Path('/mnt/d/dev/uae-tests/ue1-rc17-hang.470qbt')
exe, label = sys.argv[1], sys.argv[2]
bench = len(sys.argv) > 3 and sys.argv[3] == 'bench'
settle = 15
fpsdir = fx / 'System/fps_logs'
out = fx / 'claude-runs'
out.mkdir(exist_ok=True)
log = fx / 'System/Unreal.log'
uaelog = pathlib.Path('/mnt/c/Users/Public/Documents/Amiga Files/WinUAE/winuaelog.txt')
hits = 0
vfreq = '?'
shutil.copyfile(exe, fx / 'System/Unreal-buffer')
start = (fx / 'Start-64').read_text()
if os.environ.get('UE_ARGS'):
    start = start.replace('-MGLFILEBUFFER=64', '-MGLFILEBUFFER=64 ' + os.environ['UE_ARGS'])
(fx / 'Start-Test').write_text(start)

def ps(script):
    return subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', script], text=True).strip()

res, text, fpsfile = 'TIMEOUT', '', None

def count_hits():
    global hits
    # winuaelog.txt is reset on each WinUAE start: check it before the next attempt.
    t = uaelog.read_text(errors='replace') if uaelog.exists() else ''
    global vfreq
    f = re.findall(r'VFreq=[0-9/]+=([0-9.]+)Hz', t)
    vfreq = f[-1] if f else '?'
    n = t.count('Enforcer Hit')
    if n:
        hits += n
        shutil.copyfile(uaelog, out / f'{label}-winuaelog.txt')

def attempt():
    global res, text, fpsfile
    fps_before = set(fpsdir.glob('*.txt'))
    old = log.stat().st_mtime_ns if log.exists() else None
    style = ' -WindowStyle Minimized' if os.environ.get('UE_SCREEN') == 'minimized' else ''
    pid = int(ps('$p=Start-Process' + style + ' -FilePath "D:\\Amiga\\AmiKit X\\WinUAE\\winuae.exe" '
                 '-WorkingDirectory "D:\\Amiga\\AmiKit X\\WinUAE" '
                 '-ArgumentList @("-f","D:\\dev\\uae-tests\\ue1-rc17-hang.470qbt\\' + os.environ.get('UE_UAE_CONFIG', 'test.uae') + '") -PassThru; $p.Id'))
    if os.environ.get('UE_SCREEN'):
        mover = subprocess.check_output(['wslpath', '-w', str(pathlib.Path(__file__).with_name('uae-move-window.ps1'))], text=True).strip()
        subprocess.Popen(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden',
                          '-File', mover, '-UaeProcessId', str(pid), '-Target', os.environ['UE_SCREEN']],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.monotonic()
    res, text, music_t = 'TIMEOUT', '', None
    try:
        while time.monotonic() - t0 < (400 if bench else 150):
            time.sleep(0.5)
            if music_t is None and time.monotonic() - t0 > 60:
                break
            if not log.exists() or log.stat().st_mtime_ns == old:
                continue
            text = log.read_text(errors='replace')
            if 'Critical:' in text:
                res = 'CRASH'
                break
            if 'Music started:' in text and music_t is None:
                music_t = time.monotonic()
            if bench:
                new = set(fpsdir.glob('*.txt')) - fps_before
                if new:
                    time.sleep(float(os.environ.get('UE_POST', '1')))
                    fpsfile = sorted(new)[0]
                    if os.environ.get('UE_SHOT'):
                        script = subprocess.check_output(['wslpath', '-w', str(pathlib.Path(__file__).with_name('uae-shot.ps1'))], text=True).strip()
                        target = subprocess.check_output(['wslpath', '-w', os.environ['UE_SHOT']], text=True).strip()
                        subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, str(pid), target])
                    res = 'OK ' + ' / '.join(fpsfile.read_text().splitlines()[1:3])
                    break
            elif music_t and time.monotonic() - music_t > settle:
                res = 'OK'
                break
    finally:
        ps(f'$p=Get-Process -Id {pid} -ErrorAction SilentlyContinue; if($p){{Stop-Process -Id {pid} -Force}}; exit 0')
        time.sleep(1)
        count_hits()

for n in range(1, 4):
    attempt()
    if res != 'TIMEOUT':
        break
    print(label, f'attempt {n}: game did not start, retrying', flush=True)

text = log.read_text(errors='replace') if log.exists() else ''
(out / f'{label}.log').write_text(text)
if fpsfile:
    shutil.copyfile(fpsfile, out / f'{label}.fps.txt')
m = re.search(r'FIRST: MainLoop initial ticks=(\d+)', text)
crit = [l for l in text.splitlines() if l.startswith('Critical:')]
if hits and not res.startswith('CRASH'):
    res = 'HITS ' + res
print(label, res, f'attempts={n}', f'hits={hits}', f'vfreq={vfreq}', 'startup_ms=' + (m[1] if m else '?'), ' | '.join(crit[:2]), flush=True)
