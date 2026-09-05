# pc3host - the Pico Computer 3's BASIC toolchain built for a PC.
#
# Nothing here is a copy.  Every source is compiled where it lives in the
# FUZIX tree (the pc3 branch), so the host build and the board build are
# the same files.  Phase 0 of REVIEW.md: the tools, the gates, and the
# seam test that proves the display core compiles without the kernel.

cmake_minimum_required(VERSION 3.16)
# C throughout; CXX only because MiniFB carries C++ and the server that
# links it has to be linked as C++.
project(pc3host C CXX)

# ---- where the shared sources live -----------------------------------------
# ext/FUZIX is the submodule, pinned to a commit.  A developer working in
# a live FUZIX checkout points FUZIX_ROOT at it instead:
#     cmake -S . -B build -DFUZIX_ROOT=/home/me/src/FUZIX
set(FUZIX_ROOT "" CACHE PATH "FUZIX tree (pc3 branch) to build from")
if(FUZIX_ROOT STREQUAL "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/ext/FUZIX/Kernel")
        set(FUZIX_ROOT "${CMAKE_SOURCE_DIR}/ext/FUZIX")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/../FUZIX/Kernel")
        get_filename_component(FUZIX_ROOT "${CMAKE_SOURCE_DIR}/../FUZIX" ABSOLUTE)
    else()
        message(FATAL_ERROR "No FUZIX tree: run 'git submodule update --init' "
                            "or pass -DFUZIX_ROOT=<path>")
    endif()
endif()
message(STATUS "FUZIX_ROOT: ${FUZIX_ROOT}")

set(CC_DIR   ${FUZIX_ROOT}/Applications/CC)
set(MMB_DIR  ${FUZIX_ROOT}/Applications/mmb2c)
set(CPP_DIR  ${FUZIX_ROOT}/Applications/cpp)
set(ED_DIR   ${FUZIX_ROOT}/Applications/mmedit)
set(PLAT_DIR ${FUZIX_ROOT}/Kernel/platform/platform-rpipico)
set(UTIL_DIR ${PLAT_DIR}/utils)
set(LIBS_DIR ${FUZIX_ROOT}/Library/libs)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(BIN ${CMAKE_BINARY_DIR}/bin)

# The headers cc's include directory carries - what the board's
# mkccimage.sh stages into /usr/lib/cc/include, by the same rule: the
# bcrun-world C headers, every mmb_*.h, the pin registers, the four
# FCC-view headers by name (not fcc/include's stdlib.h), and the ioctl
# header a PC3 C program includes.  One list, used for the build tree's
# lib/cc/include and for the installed one.
file(GLOB CC_INCLUDES ${CC_DIR}/hosttest/ctest-include/*.h)
file(GLOB MMB_INCLUDES ${MMB_DIR}/mmb_*.h)
set(PC3_CC_HEADERS ${CC_INCLUDES} ${MMB_INCLUDES}
    ${MMB_DIR}/fcc/include/math.h ${MMB_DIR}/fcc/include/ctype.h
    ${MMB_DIR}/fcc/include/stdint.h ${MMB_DIR}/fcc/include/time.h
    ${FUZIX_ROOT}/Library/include/sys/pc3io.h
    ${PLAT_DIR}/pico_ioctl.h)

# ---- the Compiler Kit: cpp, cc0, cc1, cc2, the tools, bcrun ----------------
# Flags are Applications/CC/Makefile.host's, exactly: the same object
# format and the same 32-bit model the gates have always been run with.
# -include unistd.h: the passes call write() and expect the Fuzix headers
# to have declared it - a host-only concern, fixed here as it is there.
set(KIT_FLAGS -g -O1 -Wall -Wno-unused-result -include unistd.h)
set(KIT_DEFS  CPU_armm0 NO_FLOAT BIG_TABLES)

function(kit_tool name)
    add_executable(${name} ${ARGN})
    target_compile_options(${name} PRIVATE ${KIT_FLAGS})
    target_compile_definitions(${name} PRIVATE ${KIT_DEFS})
endfunction()

kit_tool(cc0 ${CC_DIR}/frontend.c)
kit_tool(cc1
    ${CC_DIR}/body.c ${CC_DIR}/declaration.c ${CC_DIR}/enum.c
    ${CC_DIR}/error.c ${CC_DIR}/expression.c ${CC_DIR}/header.c
    ${CC_DIR}/idxdata.c ${CC_DIR}/initializer.c ${CC_DIR}/label.c
    ${CC_DIR}/lex.c ${CC_DIR}/main.c ${CC_DIR}/primary.c
    ${CC_DIR}/stackframe.c ${CC_DIR}/storage.c ${CC_DIR}/struct.c
    ${CC_DIR}/switch.c ${CC_DIR}/symbol.c ${CC_DIR}/tree.c
    ${CC_DIR}/type.c ${CC_DIR}/type_iterator.c ${CC_DIR}/target-thumb.c)
# backend-bcode.c #includes backend-thumb.c; the depfile tracks it.
kit_tool(cc2 ${CC_DIR}/backend.c ${CC_DIR}/backend-bcode.c)
kit_tool(bcdump ${CC_DIR}/bcdump.c)
kit_tool(dumptokens ${CC_DIR}/dumptokens.c)
# The cc driver, installed as `cc`.  Under PC3_HOST it finds the passes
# and headers in ../lib/cc/ beside its own directory and cpp, mmbc and
# bcrun beside itself, so the tree runs from build/ or from /opt/pc3
# alike - /usr/lib/cc and /usr/bin are the board's places.
kit_tool(ccbc ${CC_DIR}/ccbc.c)
set_target_properties(ccbc PROPERTIES OUTPUT_NAME cc)
# ...and the build tree gets that layout too - lib/cc/{cc0,cc1,cc2,
# include/} beside bin/ - so build/bin/cc works exactly as the installed
# one does, which is how the samples get compiled here before a package
# is made.
set(CCLAYOUT ${CMAKE_BINARY_DIR}/lib/cc)
add_custom_command(TARGET ccbc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CCLAYOUT}/include
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${BIN}/cc0 ${CCLAYOUT}/cc0
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${BIN}/cc1 ${CCLAYOUT}/cc1
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${BIN}/cc2 ${CCLAYOUT}/cc2
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PC3_CC_HEADERS} ${CCLAYOUT}/include
    VERBATIM)

# ---- the client library: /dev/sys on a PC ----------------------------------
# What a program opens as /dev/sys and ioctls, this carries to pc3d over
# a socket.  bcrun (for the runtime and for bytecode programs) and the
# image programs link it and are built with PC3_HOST, which is the one
# define the FUZIX sources know for "on a PC, talk to the server".
add_library(pc3client STATIC client/pc3client.c)
target_include_directories(pc3client PUBLIC client proto ${PLAT_DIR})
target_compile_definitions(ccbc PRIVATE PC3_HOST)
target_link_libraries(ccbc PRIVATE pc3client)

# bcrun carries the mm runtime natively: bcrun_mm.c #includes
# mmb2c/mmb_runtime.c, hence the include path.  fnum.c comes from the C
# library source rather than being linked, so %e and %g are the board's.
# MM_PC3 is the board's define: it selects the runtime's real hardware
# paths (the gate build in Applications/CC/Makefile.host leaves it off
# and gets the display-less runtime).  MM_HOSTED_ONLY says there is no
# kernel behind those paths - keep the local maths table, do not ask
# for the kernel's.  PC3_HOST routes /dev/sys to the server.
kit_tool(bcrun ${CC_DIR}/bcrun.c ${LIBS_DIR}/fnum.c)
target_include_directories(bcrun PRIVATE ${MMB_DIR})
target_compile_definitions(bcrun PRIVATE PC3_HOST MM_PC3 MM_HOSTED_ONLY)
target_link_libraries(bcrun PRIVATE pc3client m)

# The gates' bcrun: the display-less shape Makefile.host builds, which
# is what every tests/*.expected was blessed against.  The board-shaped
# bcrun above, run with no display, refuses what the board would refuse
# - BLIT, SPRITE, the pins - where the display-less runtime says "not
# here" and carries on, so the two cannot share expected output.  It is
# named bcrun in its own directory beside links to the passes, which is
# the layout fcctests.sh takes as BIN.
set(GATEBIN ${CMAKE_BINARY_DIR}/gatebin)
kit_tool(bcrun-gate ${CC_DIR}/bcrun.c ${LIBS_DIR}/fnum.c)
target_include_directories(bcrun-gate PRIVATE ${MMB_DIR})
target_link_libraries(bcrun-gate PRIVATE m)
set_target_properties(bcrun-gate PROPERTIES OUTPUT_NAME bcrun
                                            RUNTIME_OUTPUT_DIRECTORY ${GATEBIN})
add_custom_command(TARGET bcrun-gate POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${BIN}/cc0 ${GATEBIN}/cc0
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${BIN}/cc1 ${GATEBIN}/cc1
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${BIN}/cc2 ${GATEBIN}/cc2)
add_dependencies(bcrun-gate cc0 cc1 cc2)

# Fuzix's cpp.  fccbuild.sh uses gcc -E on the host today; this is what
# ccbc drives, and what a PC without gcc will need.
add_executable(cpp ${CPP_DIR}/cpp.c ${CPP_DIR}/hash.c ${CPP_DIR}/main.c
                   ${CPP_DIR}/token1.c ${CPP_DIR}/token2.c)
target_compile_options(cpp PRIVATE -w)

# ---- mmbc: the translator in C ---------------------------------------------
add_executable(mmbc
    ${MMB_DIR}/mmbc/mmbc_main.c ${MMB_DIR}/mmbc/mmbc_lex.c
    ${MMB_DIR}/mmbc/mmbc_util.c ${MMB_DIR}/mmbc/mmbc_tab.c
    ${MMB_DIR}/mmbc/mmbc_sym.c ${MMB_DIR}/mmbc/mmbc_expr.c
    ${MMB_DIR}/mmbc/mmbc_builtin.c ${MMB_DIR}/mmbc/mmbc_decl.c
    ${MMB_DIR}/mmbc/mmbc_walk.c ${MMB_DIR}/mmbc/mmbc_stmt.c
    ${MMB_DIR}/mmbc/mmbc_type.c ${MMB_DIR}/mmbc/mmbc_out.c
    ${MMB_DIR}/mmbc/mmbc_font.c)
target_compile_options(mmbc PRIVATE -O2 -g -Wall -Wextra -Wno-unused-parameter)

# ---- mmedit ------------------------------------------------------------------
# The editor with its POSIX shim, as the pty harness has always built it.
add_executable(mmedit ${ED_DIR}/mmedit.c ${ED_DIR}/shim.c
                      ${ED_DIR}/editor.c ${ED_DIR}/keywords.c)
target_include_directories(mmedit PRIVATE ${ED_DIR})
target_compile_options(mmedit PRIVATE -O0 -g)
target_compile_definitions(mmedit PRIVATE PC3_HOST)	# F2 runs the cc beside it
target_link_libraries(mmedit PRIVATE pc3client)

# ---- the programs around a program: loaders and players ------------------
# The image programs and the players open /dev/sys through pc3sys.h and
# so reach the server, which answers the display codes and, since phase
# 3, the sound codes.  Building them all here is what keeps them
# compiling as the shared code moves.
function(pc3_util name)
    add_executable(${name} ${ARGN})
    target_compile_options(${name} PRIVATE -std=gnu99 -O2 -Wall -Wno-unused)
    target_include_directories(${name} PRIVATE ${UTIL_DIR} ${PLAT_DIR})
endfunction()
function(pc3_image name)
    pc3_util(${name} ${ARGN})
    target_compile_definitions(${name} PRIVATE PC3_HOST)
    target_link_libraries(${name} PRIVATE pc3client)
endfunction()

pc3_image(loadjpg   ${UTIL_DIR}/loadjpg.c ${UTIL_DIR}/picojpeg.c)
pc3_image(loadpng   ${UTIL_DIR}/loadpng.c ${UTIL_DIR}/upng.c ${UTIL_DIR}/upng_pc3.c)
set_source_files_properties(${UTIL_DIR}/upng.c PROPERTIES
    COMPILE_DEFINITIONS UPNG_NO_FILE)      # as utils/Makefile does
pc3_image(loadimage ${UTIL_DIR}/loadimage.c)
pc3_image(saveimage ${UTIL_DIR}/saveimage.c)
pc3_image(playmp3   ${UTIL_DIR}/playmp3.c)
pc3_image(playwav   ${UTIL_DIR}/playwav.c)
pc3_image(playflac  ${UTIL_DIR}/playflac.c)
pc3_image(playmod   ${UTIL_DIR}/playmod.c ${UTIL_DIR}/hxcmod.c)
pc3_image(playsnd   ${UTIL_DIR}/playsnd.c)
target_link_libraries(playmp3  PRIVATE m)
target_link_libraries(playflac PRIVATE m)
target_link_libraries(playmod  PRIVATE m)
target_link_libraries(playsnd  PRIVATE m)

# The board's two sound probes, for the gates: pcmpace feeds the PCM
# ring through a named pacing pattern and reports underruns; sndharness
# is playsnd's renderer on its own, writing a WAV, and is the reference
# the sound seam test compares the kernel synth against.
pc3_image(pcmpace ${UTIL_DIR}/pcmpace.c)
pc3_util(sndharness ${UTIL_DIR}/sndharness.c)

# ---- the seam: the kernel's display core, compiled without the kernel ----
# display.c is the portable half of the PC3 display driver (its scanout
# is display_hstx.c).  This links it against a stand-in for everything
# display_priv.h asks of the hardware and drives the primitives.  If
# this stops compiling, the seam has been crossed in the kernel.
add_executable(display_seam tests/seam/display_seam.c ${PLAT_DIR}/display.c)
target_compile_definitions(display_seam PRIVATE PC3_HOST)
target_include_directories(display_seam PRIVATE ${PLAT_DIR})
target_compile_options(display_seam PRIVATE -g -Wall -Wextra -Wno-unused-parameter)

# ---- the C standard, and why it is pinned --------------------------------------
# gcc 15 compiles C23 by default, and against glibc 2.38+ headers that
# turns strtol and friends into __isoc23_strtol, a symbol only glibc
# 2.38 has.  In C11 the same call is plain strtol, and a dynamic binary
# built here then asks for nothing newer than GLIBC_2.34 - which every
# Ubuntu from 22.04 on has.  MiniFB pins C11 itself.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# ---- static linking, for a package that runs on any glibc --------------------
# Built here against a recent glibc, a dynamic binary would demand that
# glibc on the machine it is installed on.  Static, it demands nothing -
# so every TOOL is linked static for the package.  pc3d is NOT: libX11
# dlopen()s the system's libXcursor to draw the cursor MiniFB creates at
# window open, and a statically linked glibc can only dlopen libraries
# built against the same glibc.  On the first machine with a different
# glibc the window appeared and the server died.  So pc3d is an ordinary
# dynamic program, and the package depends on the X11 libraries and on
# GLIBC_2.34 (see the C standard note above).
option(PC3_STATIC "Link the tools statically (never pc3d)" OFF)

# ---- the device server -------------------------------------------------------
# MiniFB is the window.  X11 without OpenGL: the server hands it a
# buffer already at the raster's size, so nothing needs scaling, and
# that is the path with the fewest libraries between us and the screen.
set(MINIFB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIFB_BUILD_VERSION_INFO OFF CACHE BOOL "" FORCE)
set(MINIFB_USE_OPENGL_API OFF CACHE BOOL "" FORCE)
set(MINIFB_USE_WAYLAND_API OFF CACHE BOOL "" FORCE)
add_subdirectory(ext/minifb EXCLUDE_FROM_ALL)

# pc3d plays the kernel: the kernel's own display core and font table
# compile into it (display.c, fonts.c), against disphw.c's hooks and
# the two stub headers in server/kstub that stand in for <kernel.h>
# and <kdata.h>.  dispatch.c is misc.c's ioctl switch for a PC.
# The keyboard is the kernel's decoder too: kbd_decode.c, vendored in
# the platform directory byte-identical to the MicroPython port's, fed
# HID boot reports that keyboard.c builds from MiniFB's key events.
file(READ VERSION PC3_VERSION)
string(STRIP "${PC3_VERSION}" PC3_VERSION)
# The sound is the kernel's too: sound.c (the synths, the PCM ring, the
# ownership rules) compiles in against sndhw.c's hooks and plays through
# miniaudio (ext/miniaudio, one header, its implementation in
# server/miniaudio.c; the backends are dlopen'd at run time).
find_package(Threads REQUIRED)
add_executable(pc3d
    server/pc3d.c server/dispatch.c server/disphw.c server/present.c
    server/window.c server/fontdata.c server/keyboard.c server/crash.c
    server/sndhw.c server/miniaudio.c
    ${PLAT_DIR}/display.c ${PLAT_DIR}/fonts.c ${PLAT_DIR}/kbd_decode.c
    ${PLAT_DIR}/sound.c)
target_compile_definitions(pc3d PRIVATE PC3_HOST PC3D_VERSION="${PC3_VERSION}")
target_include_directories(pc3d PRIVATE server server/kstub proto ${PLAT_DIR}
                                        ext/minifb/include ext/miniaudio)
target_compile_options(pc3d PRIVATE -O2 -g -Wall -Wextra -Wno-unused-parameter)
set_source_files_properties(server/miniaudio.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(pc3d PRIVATE minifb X11 Threads::Threads ${CMAKE_DL_LIBS} m)
set_target_properties(pc3d PROPERTIES LINKER_LANGUAGE CXX)

# The sound seam: sound.c compiled against stub hooks and driven
# directly - ownership, the ring, mono expansion, underrun counting, the
# BBC square, and the MMBasic synth against sndharness sample for
# sample (tests/seam/sound_seam.sh).
add_executable(sound_seam tests/seam/sound_seam.c ${PLAT_DIR}/sound.c)
target_compile_definitions(sound_seam PRIVATE PC3_HOST)
target_include_directories(sound_seam PRIVATE ${PLAT_DIR})
target_compile_options(sound_seam PRIVATE -g -Wall -Wextra -Wno-unused-parameter)

# pc3key presses keys in the server from a script (tests/e2e/run.sh).
add_executable(pc3key tools/pc3key.c)
target_include_directories(pc3key PRIVATE ext/minifb/include)
target_link_libraries(pc3key PRIVATE pc3client)

# ---- static linking, the link step (the option is declared above MiniFB) ----
if(PC3_STATIC)
    foreach(t cc0 cc1 cc2 bcdump dumptokens ccbc bcrun bcrun-gate cpp mmbc mmedit
              loadjpg loadpng loadimage saveimage playmp3 playwav playflac playmod
              playsnd pcmpace sndharness pc3key display_seam sound_seam)
        target_link_options(${t} PRIVATE -static)
    endforeach()
    # pc3d stays dynamic (see the note at the option), C++ runtime
    # included: this box's static libstdc++ references glibc 2.36 and
    # 2.38 symbols (arc4random, the C23 strtoul) that the shared one on
    # the target does not impose.  MiniFB's C++ needs only symbols every
    # libstdc++6 has had for a decade.
endif()

# ---- installing ------------------------------------------------------------------
# The board's layout, under one prefix (/opt/pc3 for the package):
#   bin/         the programs, and cc, and the pc3 launcher
#   lib/cc/      cc0 cc1 cc2 and include/, as /usr/lib/cc on the board
#   share/       documentation, the man pages as text, and the examples
install(TARGETS pc3d bcrun mmbc mmedit cpp bcdump ccbc pc3key
                loadjpg loadpng loadimage saveimage
                playmp3 playwav playflac playmod playsnd
        RUNTIME DESTINATION bin)
install(TARGETS cc0 cc1 cc2 RUNTIME DESTINATION lib/cc)
install(PROGRAMS packaging/pc3 DESTINATION bin)
install(FILES ${PC3_CC_HEADERS} DESTINATION lib/cc/include)
install(FILES README.md REVIEW.md PHASE0.md PHASE1.md PHASE2.md PHASE3.md
              ${PLAT_DIR}/PC3-C-MANUAL.md ${MMB_DIR}/README.md
        DESTINATION share/doc/pc3host)
file(GLOB PC3_MAN ${FUZIX_ROOT}/Applications/man1/bcrun.1
                  ${FUZIX_ROOT}/Applications/man1/cc.1
                  ${FUZIX_ROOT}/Applications/man1/mmbc.1
                  ${FUZIX_ROOT}/Applications/man1/mmedit.1
                  ${FUZIX_ROOT}/Applications/man1/saveimage.1
                  ${FUZIX_ROOT}/Applications/man1/loadjpg.1
                  ${FUZIX_ROOT}/Applications/man1/loadpng.1)
install(FILES ${PC3_MAN} DESTINATION share/doc/pc3host/man)
# keys.bas is the test fixture (it writes a file and shows nothing);
# keydemo.bas is the one for a person.  The solar eclipse predictor -
# 3,213 lines of real astronomy, the translator's oldest fixture - comes
# with the input the fixture feeds it and the output it must produce;
# it also sits in share/mmb2c/tests with the rest of the corpus.
install(FILES tests/e2e/gfx1.bas tests/e2e/gfxc.c tests/e2e/keydemo.bas
              ${MMB_DIR}/tests/solar_eclipse.bas
              ${MMB_DIR}/tests/solar_eclipse.in
              ${MMB_DIR}/tests/solar_eclipse.expected
        DESTINATION share/examples)
install(DIRECTORY ${MMB_DIR}/samples/ DESTINATION share/examples/samples
        FILES_MATCHING PATTERN "*.bas" PATTERN "*.md")
# The whole MMBasic directory from the FUZIX tree - the translator in
# Python and in C, the runtime and its headers, the tests with their
# expected output, the samples, the plans and the coverage notes - so a
# machine with the package has the sources the programs were built
# from and the corpus they were proven on.  Build products stay out.
install(DIRECTORY ${MMB_DIR}/ DESTINATION share/mmb2c
        PATTERN "build" EXCLUDE
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.o" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN ".symtmp" EXCLUDE
        PATTERN "mmbc/mmbc" EXCLUDE)

# ---- the gates ---------------------------------------------------------------
# Today's gates, run against THIS build's binaries.  They live in the
# FUZIX tree and take the binaries by environment (BIN, MMBC, MMB2C);
# cc2 writes .symtmp in the working directory, so the fcc gates each get
# their own and never run together.  PC3_DISPLAY=off keeps them the
# display-less runs their .expected files were blessed as: with a
# server answering, PIXEL() would read real pixels back.
enable_testing()
set(GATE ${CMAKE_BINARY_DIR}/gate)

add_test(NAME seam-display COMMAND display_seam)
add_test(NAME seam-sound
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/seam/sound_seam.sh ${BIN} ${GATE}/sound)

add_test(NAME fcc-gate
    COMMAND ${CMAKE_COMMAND} -E env BIN=${GATEBIN} W=${GATE}/fcc PC3_DISPLAY=off
            bash ${MMB_DIR}/fcc/fcctests.sh
    WORKING_DIRECTORY ${GATE}/fcc)
add_test(NAME fcc-gate-mmbc
    COMMAND ${CMAKE_COMMAND} -E env BIN=${GATEBIN} W=${GATE}/fccm MMB2C=${BIN}/mmbc
            PC3_DISPLAY=off bash ${MMB_DIR}/fcc/fcctests.sh
    WORKING_DIRECTORY ${GATE}/fccm)

# The end-to-end gate: programs against a headless server, readbacks
# and screens against goldens (tests/e2e/run.sh).
add_test(NAME e2e-display
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/e2e/run.sh ${BIN} ${FUZIX_ROOT} ${GATE}/e2e)
set_tests_properties(e2e-display PROPERTIES TIMEOUT 300 RESOURCE_LOCK symtmp LABELS e2e)
add_test(NAME cgate
    COMMAND ${CMAKE_COMMAND} -E env MMBC=${BIN}/mmbc W=${GATE}/cgate
            bash ${MMB_DIR}/mmbc/cgate.sh
    WORKING_DIRECTORY ${GATE}/cgate)
add_test(NAME libgate
    COMMAND sh ${MMB_DIR}/libgate.sh
    WORKING_DIRECTORY ${MMB_DIR})
# The gcc path: the reference runtime, built and checked by the tree's
# own Makefile.  It builds into ${MMB_DIR}/build, as it always has.
add_test(NAME gcc-check
    COMMAND make -s -C ${MMB_DIR} check)

file(MAKE_DIRECTORY ${GATE}/fcc ${GATE}/fccm ${GATE}/cgate)
set_tests_properties(fcc-gate fcc-gate-mmbc cgate gcc-check PROPERTIES
    TIMEOUT 1800 RESOURCE_LOCK symtmp)
set_tests_properties(fcc-gate fcc-gate-mmbc cgate gcc-check libgate PROPERTIES
    LABELS gates)
set_tests_properties(seam-display seam-sound PROPERTIES LABELS seam)
