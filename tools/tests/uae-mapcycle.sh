#!/usr/bin/env bash
# usage: uae-mapcycle.sh <exe> <label> <map,list>
# Runs the -MAPCYCLE test in the UE1 fixture and stops on done/Critical/no progress (6 min).
T=/mnt/d/dev/UE1-MiniGL/tools/tests; FX=/mnt/d/dev/uae-tests/ue1-rc17-hang.470qbt; C=$FX/claude-runs; F=$FX/System
L="/mnt/c/Users/Public/Documents/Amiga Files/WinUAE/winuaelog.txt"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w $T/uae-fixture-kill.ps1)" >/dev/null
PID=$(python3 $T/uae-fixture-start.py "$1" "-MAPCYCLE=$3") || { echo "start failed: $PID"; exit 1; }
echo "started $PID"
last=0; idle=0; res=TIMEOUT
while true; do
  sleep 20
  n=$(stat -c %s $F/Unreal.log)
  if grep -a -q 'MapCycle: done' $F/Unreal.log; then res=DONE; break; fi
  if grep -a -q 'Critical:' $F/Unreal.log; then res=CRITICAL; break; fi
  if [ "$n" = "$last" ]; then idle=$((idle+20)); else idle=0; last=$n; fi
  [ $idle -ge 360 ] && { res=HANG; break; }
done
P=$(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w $T/uae-fixture-pid.ps1)" | tr -d '\r')
[ -n "$P" ] && powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w $T/uae-shot.ps1)" $P "$(wslpath -w $C/$2-end.png)" >/dev/null
cp $F/Unreal.log $C/$2-Unreal.log; cp "$L" $C/$2-winuaelog.txt
echo "RESULT $res"
grep -a -E 'MapCycle|Bringing Level|Missing import|Failed import|Critical|Uncaught' $C/$2-Unreal.log | grep -v 'Unreal.MyLevel\|Entry.MyLevel'
echo "enforcer hits: $(grep -a -c 'Enforcer Hit' $C/$2-winuaelog.txt)"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w $T/uae-fixture-kill.ps1)" >/dev/null
