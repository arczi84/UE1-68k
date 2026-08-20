# CMake toolchain file for AmigaOS 3.x on m68k.
#
# Usage:
#   cmake -G"Unix Makefiles" -Bbuild-amiga Source \
#     -DCMAKE_TOOLCHAIN_FILE=Source/cmake/AmigaOS68k.cmake
#
# Override the defaults below with -D as needed, e.g. -DAMIGA_CPU=68040.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR m68k)
set(AMIGA TRUE)

# Toolchain location. bebbo's m68k-amigaos GCC.
if(NOT DEFINED AMIGA_TOOLCHAIN_ROOT)
  set(AMIGA_TOOLCHAIN_ROOT "/opt/amiga" CACHE PATH "m68k-amigaos toolchain root")
endif()

# Where SDL 1.2 and libGL.a live. These ship outside the toolchain.
if(NOT DEFINED AMIGA_SDK_ROOT)
  set(AMIGA_SDK_ROOT "/mnt/d/amiga-gcc2" CACHE PATH "Amiga SDK root (SDL, GL)")
endif()

# Target CPU. Default 68040 to match the AmiKit test setup (cpu_type=68040);
# 68060 or 68020 also work. -mhard-float requires an FPU (68040/68060/68882).
if(NOT DEFINED AMIGA_CPU)
  set(AMIGA_CPU "68040" CACHE STRING "Target 68k CPU")
endif()

set(CMAKE_C_COMPILER   "${AMIGA_TOOLCHAIN_ROOT}/bin/m68k-amigaos-gcc")
set(CMAKE_CXX_COMPILER "${AMIGA_TOOLCHAIN_ROOT}/bin/m68k-amigaos-g++")
set(CMAKE_AR           "${AMIGA_TOOLCHAIN_ROOT}/bin/m68k-amigaos-ar")
set(CMAKE_RANLIB       "${AMIGA_TOOLCHAIN_ROOT}/bin/m68k-amigaos-ranlib")

# The toolchain produces AmigaOS hunk executables, not ELF; CMake's default
# compiler test tries to link a full binary and would need the whole SDK.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "${AMIGA_TOOLCHAIN_ROOT}" "${AMIGA_SDK_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# -noixemul selects the libnix C library. Linking against ixemul instead
# leaves libSDL.a with undefined refs (__bcopz, __sF, signal).
# -fno-PIC: AmigaOS m68k has no GOT/PLT; PIC codegen (which CMake enables by
# default for library targets) emits _GLOBAL_OFFSET_TABLE_/@PLTPC the
# assembler rejects. Disable it globally for anything built via this toolchain.
#
# NOTE on struct alignment: 68k GCC defaults to 2-byte alignment for int/long
# inside structs, but the engine computes UObject property layout assuming
# 4-byte alignment (x86/MSVC uses /Zp4), so sizeof(class) != GetPropertiesSize()
# (first tripped by the UFireTexture size assert). The obvious fix -malign-int
# CRASHES gcc 6.5 (internal compiler error / segfault on UnCache.cpp etc.), so
# it can't be used. Alignment must be handled another way (see UnGcc.h GCC_ALIGN
# usage / per-field alignment, or an alignment attribute on the base types).
# -mhard-float: emit FPU instructions (fmul/fadd/fmove) instead of soft-float
# calls (___mulsf3/___addsf3). gcc 6.5 targeting -m68040 defaults to SOFT float,
# and soft-float combined with -ffast-math produces wrong FP results — which made
# the mesh vertex transform (ComputeOutcode/TransformPointBy) yield garbage, so
# every vertex was outcode-rejected and meshes rendered invisible. 68040/68060
# have an on-chip FPU so hard-float is correct and far faster. Must be applied to
# ALL translation units (C and C++) for a consistent float ABI.
set(AMIGA_COMMON_FLAGS "-noixemul -m${AMIGA_CPU} -mhard-float -fomit-frame-pointer -ffast-math -fno-PIC -fno-pic")

# C++ standard library graft for the AmigaPorts gcc 15 build.
# That toolchain ships m68k-amigaos-g++ (cc1plus works) but was built WITHOUT
# libstdc++: no <new>/<memory> headers and no libstdc++.a/libsupc++.a anywhere
# in it, so C++ TUs die at the first standard header. The bebbo gcc 6.5 tree
# has the complete m68k libstdc++ (same target ABI), and gcc 15's g++ compiles
# those headers cleanly. When the active toolchain is NOT the 6.5 tree, graft
# 6.5's C++ headers + libs in. For the plain 6.5 build (AMIGA_TOOLCHAIN_ROOT=
# /opt/amiga) this whole block is skipped, leaving that build untouched.
set(AMIGA_CXX_STDLIB_FLAGS "")
set(AMIGA_CXX_STDLIB_LIBS  "")
if(NOT AMIGA_TOOLCHAIN_ROOT MATCHES "/opt/amiga$")
  # Root of the 6.5 tree that owns the C++ headers/libs. Override with
  # -DAMIGA_CXX_STDLIB_ROOT if 6.5 lives elsewhere.
  if(NOT DEFINED AMIGA_CXX_STDLIB_ROOT)
    set(AMIGA_CXX_STDLIB_ROOT "/opt/amiga/lib/gcc/m68k-amigaos/6.5.0b"
        CACHE PATH "gcc 6.5 tree supplying libstdc++ for the gcc15 build")
  endif()
  set(_cxx_inc "${AMIGA_CXX_STDLIB_ROOT}/include/c++")
  if(NOT EXISTS "${_cxx_inc}/new")
    message(FATAL_ERROR
      "C++ stdlib graft: no <new> at ${_cxx_inc}. Set AMIGA_CXX_STDLIB_ROOT "
      "to a gcc 6.5 tree, or point AMIGA_TOOLCHAIN_ROOT at /opt/amiga.")
  endif()
  # -isystem so these lose to any real headers and stay quiet on warnings.
  # The m68k-amigaos subdir holds the target-specific bits/c++config.h.
  # -fno-sized-deallocation: gcc 15 defaults to emitting the C++14 sized
  # operator delete(void*, size_t) (_ZdlPvm), which 6.5's libsupc++ predates
  # (it only has _ZdlPv). Without this the final link fails with undefined
  # `operator delete(void*, unsigned long)`. Turn it off to use plain delete.
  #
  # -malign-int: forces 4-byte alignment for int/long/float inside structs so
  # 68k struct layout matches x86/MSVC (/Zp4), which the engine's UObject
  # property layout assumes (sizeof(class) == GetPropertiesSize()). This is THE
  # fix for the alignment ABI mismatch. It ICEs gcc 6.5 (hence guarded out of
  # the 6.5 build), but gcc 15 compiles it fine.
  set(AMIGA_CXX_STDLIB_FLAGS
      "-isystem ${_cxx_inc} -isystem ${_cxx_inc}/m68k-amigaos -fno-sized-deallocation -malign-int")
  set(AMIGA_CXX_STDLIB_LIBS
      "-L${AMIGA_CXX_STDLIB_ROOT} -lstdc++ -lsupc++")
endif()

# Belt-and-suspenders: keep CMake from re-adding -fPIC to library targets.
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")

set(CMAKE_C_FLAGS_INIT   "${AMIGA_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${AMIGA_COMMON_FLAGS} ${AMIGA_CXX_STDLIB_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-noixemul ${AMIGA_CXX_STDLIB_LIBS}")
