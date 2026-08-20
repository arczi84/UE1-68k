set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

set(CMAKE_C_COMPILER powerpc-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER powerpc-linux-gnu-g++)
set(CMAKE_AR powerpc-linux-gnu-ar)
set(CMAKE_RANLIB powerpc-linux-gnu-ranlib)
set(CMAKE_STRIP powerpc-linux-gnu-strip)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(PPC_LINUX_SYSROOT "/home/arczi/rootfs-ppc" CACHE PATH
    "Root containing the PowerPC SDL 1.2 headers and library")
set(LINUX_PPC ON CACHE BOOL "32-bit big-endian PowerPC Linux build" FORCE)
