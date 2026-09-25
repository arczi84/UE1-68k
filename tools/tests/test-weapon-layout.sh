#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_testdir=$(mktemp -d /tmp/ue1-weapon-test.XXXXXX)
{
  printf '%s\n' '#include <cassert>' '#include <cstddef>' '#include <cstring>' \
    'typedef int INT; typedef int UBOOL; typedef unsigned char BYTE;' \
    'enum { RF_Destroyed=0x800000 }; struct UClass;' \
    'struct UObject { int index=0,flags=0; UClass* cls=nullptr;' \
    'int GetIndex(){return index;} int GetFlags(){return flags;} UClass* GetClass(){return cls;}' \
    'bool IsA(UClass* c){return cls==c;} const char* GetName(){return "test";} };' \
    'struct UClass:UObject { int size; int GetPropertiesSize(){return size;} };' \
    'struct AWeapon:UObject { static UClass* StaticClass; }; UClass* AWeapon::StaticClass;' \
    'struct APawn:UObject { AWeapon* Weapon; AWeapon* LinkedWeapon; };' \
    'struct UObjectProperty { int Offset; }; UObjectProperty prop; bool hasProp=true;' \
    'template<class T> T* FindField(UClass*,const char*) { return hasProp?&prop:nullptr; }' \
    'UObject* objects[8]={}; struct Manager { UObject* GetIndexedObject(int i){return i>=0 && i<8?objects[i]:nullptr;} } GObj;' \
    'UObject* readable[8]={}; int AmigaAllocReadable(const void* p,unsigned long) { for(auto o:readable) if(o && p==o) return 1; return 0; }' \
    'void appMemcpy(void* d,const void* s,size_t n){memcpy(d,s,n);} void debugf(const char*,...) {}'
  sed -n '/^static UBOOL SpriteRegisteredObject(/,/^}/p' "$ue_root/Source/Render/Src/UnSprite.cpp"
  sed -n '/^static AWeapon\* SpriteLinkedWeapon(/,/^}/p' "$ue_root/Source/Render/Src/UnSprite.cpp"
  printf '%s\n' 'int main(){' \
    'UClass pawnClass,weaponClass,otherClass; AWeapon weapon; APawn pawn;' \
    'pawnClass.size=sizeof(pawn); pawn.cls=&pawnClass; pawn.Weapon=(AWeapon*)0xd3b;' \
    'weaponClass.index=1; otherClass.index=3; weapon.index=2; weapon.cls=&weaponClass;' \
    'AWeapon::StaticClass=&weaponClass;' \
    'objects[1]=readable[1]=&weaponClass; objects[2]=readable[2]=&weapon; objects[3]=readable[3]=&otherClass;' \
    'prop.Offset=(BYTE*)&pawn.LinkedWeapon-(BYTE*)&pawn; pawn.LinkedWeapon=&weapon;' \
    'assert(SpriteLinkedWeapon(&pawn)==&weapon);' \
    'pawn.LinkedWeapon=nullptr; assert(!SpriteLinkedWeapon(&pawn));' \
    'pawn.LinkedWeapon=(AWeapon*)0xd3b; assert(!SpriteLinkedWeapon(&pawn));' \
    'pawn.LinkedWeapon=(AWeapon*)0x1000; assert(!SpriteLinkedWeapon(&pawn));' \
    'pawn.LinkedWeapon=&weapon; objects[2]=nullptr; assert(!SpriteLinkedWeapon(&pawn)); objects[2]=&weapon;' \
    'weapon.flags=RF_Destroyed; assert(!SpriteLinkedWeapon(&pawn)); weapon.flags=0;' \
    'weapon.cls=(UClass*)0xd3b; assert(!SpriteLinkedWeapon(&pawn));' \
    'weapon.cls=&otherClass; assert(!SpriteLinkedWeapon(&pawn)); weapon.cls=&weaponClass;' \
    'hasProp=false; assert(!SpriteLinkedWeapon(&pawn)); hasProp=true;' \
    'prop.Offset=-1; assert(!SpriteLinkedWeapon(&pawn));' \
    'prop.Offset=sizeof(pawn); assert(!SpriteLinkedWeapon(&pawn));' \
    '}'
} | g++ -x c++ -O2 -Wall -Wextra -o "$ue_testdir/check" -
"$ue_testdir/check"
printf '%s\n' 'Linked Weapon offset and invalid-pointer checks passed (mock object registry).'
