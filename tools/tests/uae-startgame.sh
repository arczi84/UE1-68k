#!/usr/bin/env bash
# usage: uae-startgame.sh <exe> <label> : Esc, Enter (Game), shot, Enter (Start Game), wait 40 s, shot + log
set -u
T=/mnt/d/dev/UE1-MiniGL/tools/tests; C=/mnt/d/dev/uae-tests/ue1-rc17-hang.470qbt/claude-runs
K=$(wslpath -w $T/uae-keys.ps1); SH=$(wslpath -w $T/uae-shot.ps1)
PID=$(python3 $T/uae-fixture-start.py "$1") || { echo "$2: start failed: $PID"; exit 1; }
key() { powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$K" $PID "$1" >/dev/null; }
shot() { powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$SH" $PID "$(wslpath -w $C/$1)" >/dev/null; }
key "{ESC}"; sleep 2; key "{ENTER}"; sleep 3; shot "$2-menu.png"
key "{ENTER}"; sleep 40; shot "$2-game.png"
cp /mnt/d/dev/uae-tests/ue1-rc17-hang.470qbt/System/Unreal.log $C/$2-game.log
echo "$2: $(grep -a -c ': Destroying' $C/$2-game.log) Destroying lines; $(grep -a -E 'LoadMap|Critical|Game class' $C/$2-game.log | tail -3 | tr '\n' '|')"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w $T/uae-fixture-kill.ps1)" >/dev/null
