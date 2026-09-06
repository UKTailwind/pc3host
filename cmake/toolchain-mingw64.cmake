# toolchain-mingw64.cmake - cross-build the Windows programs from Linux.
#
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake
#   cmake --build build-win -j8
#
# The compiler is Debian/Ubuntu's mingw-w64 package (x86_64-w64-mingw32-*).
# Everything links statically - libgcc, libstdc++, winpthread - so the
# .exe files run on any 64-bit Windows 10 or later with nothing beside
# them.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(PC3_MINGW_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${PC3_MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${PC3_MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${PC3_MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${PC3_MINGW_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Windows 10 APIs (AF_UNIX, WSAPoll, virtual-terminal console modes).
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
