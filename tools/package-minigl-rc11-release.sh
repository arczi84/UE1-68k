#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_exe="$ue_root/build/gcc16-rc11-release/Unreal/Unreal"
ue_dest="${1:-$ue_root/Unreal-MiniGL-release-rc11}"
if test -e "$ue_dest"; then
  echo "Refusing to overwrite existing package: $ue_dest" >&2
  exit 1
fi
test -s "$ue_exe"
mkdir -p "$ue_dest/System"
cp "$ue_exe" "$ue_dest/System/Unreal-MiniGL"
cp "$ue_root/tools/release/Unreal-MiniGL.ini" "$ue_dest/System/"
cp "$ue_root/tools/release/Start-Unreal" "$ue_dest/System/"
cp "$ue_root/tools/release/Unreal-MiniGL.info" "$ue_dest/System/"
cp "$ue_root/tools/release/System.info" "$ue_dest/"
cp "$ue_root/Engine/Localization/NMiniGLDrv.int" "$ue_dest/System/"
cp "$ue_root/tools/release/INSTALL-EN.txt" "$ue_dest/"
cp "$ue_root/tools/release/INSTALACJA-PL.txt" "$ue_dest/"
cp "$ue_root/tools/release/BUILD.txt" "$ue_dest/"
(
  cd "$ue_dest"
  sha256sum System/Unreal-MiniGL System/Unreal-MiniGL.ini \
    System/Start-Unreal System/NMiniGLDrv.int \
    System/Unreal-MiniGL.info System.info \
    INSTALL-EN.txt INSTALACJA-PL.txt BUILD.txt > SHA256SUMS
  sha256sum -c SHA256SUMS
)
echo "Release candidate: $ue_dest"
