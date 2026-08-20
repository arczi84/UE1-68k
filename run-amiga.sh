#!/usr/bin/env bash
# Auto-run UE1 68k na WinUAE z WSL i odczyt logu.
# Wzorowane na mk3reboot/auto_uae_loop.sh.
set -uo pipefail

WINUAE="/mnt/d/Amiga/AmiKit X/WinUAE/winuae.exe"
CONFIG="/mnt/d/Amiga/AmiKit X/WinUAE/Configurations/AmiKit-XE.uae"
CONFIG_WIN="$(wslpath -w "$CONFIG")"
TASKKILL="/mnt/c/Windows/System32/taskkill.exe"
GAMEDIR="/mnt/d/dev/UE1/Game/Unreal68k/System"
LOG="$GAMEDIR/unreal-startup.log"
SECS="${SECS:-30}"

echo "[run] czyszczę stary log"
rm -f "$LOG"

echo "[run] ubijam istniejące WinUAE"
"$TASKKILL" /F /IM winuae.exe  >/dev/null 2>&1 || true
"$TASKKILL" /F /IM winuae64.exe >/dev/null 2>&1 || true
sleep 1

echo "[run] startuję WinUAE ($SECS s)..."
"$WINUAE" -f "$CONFIG_WIN" >/dev/null 2>&1 &
sleep "$SECS"

echo "[run] zatrzymuję WinUAE"
"$TASKKILL" /F /IM winuae.exe  >/dev/null 2>&1 || true
"$TASKKILL" /F /IM winuae64.exe >/dev/null 2>&1 || true
sleep 1

echo "===================== unreal-startup.log ====================="
if [ -f "$LOG" ]; then cat "$LOG"; else echo "(brak logu — Unreal nie wystartował lub inna ścieżka)"; fi
echo "=============================================================="
