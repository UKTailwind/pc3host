# pc3host - the Pico Computer 3's BASIC toolchain built for a PC.
#
# Nothing here is a copy.  Every source is compiled where it lives in the
# FUZIX tree (the pc3 branch), so the host build and the board build are
# the same files.  Phase 0 of REVIEW.md: the tools, the gates, and the
# seam test that proves the display core compiles without the kernel.

cmake_minimum_required(VERSION 3.16)
project(pc3host C)

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
# The cc driver.  Its /usr/lib/cc and /usr/bin paths are the board's
# (phase 5 makes them relocatable); it builds and is installed so the
# layout is exercised, not because it runs from here yet.
kit_tool(ccbc ${CC_DIR}/ccbc.c)

# bcrun carries the mm runtime natively: bcrun_mm.c #includes
# mmb2c/mmb_runtime.c, hence the include path.  fnum.c comes from the C
# library source rather than being linked, so %e and %g are the board's.
kit_tool(bcrun ${CC_DIR}/bcrun.c ${LIBS_DIR}/fnum.c)
target_include_directories(bcrun PRIVATE ${MMB_DIR})
target_link_libraries(bcrun PRIVATE m)

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

# ---- the programs around a program: loaders and players ------------------
# They open /dev/sys and ioctl it; on a host that fails politely until
# the client library (phase 1) stands in for the kernel.  Building them
# here is what keeps them compiling as the shared code moves.
function(pc3_util name)
    add_executable(${name} ${ARGN})
    target_compile_options(${name} PRIVATE -std=gnu99 -O2 -Wall -Wno-unused)
    target_include_directories(${name} PRIVATE ${UTIL_DIR} ${PLAT_DIR})
endfunction()

pc3_util(loadjpg   ${UTIL_DIR}/loadjpg.c ${UTIL_DIR}/picojpeg.c)
pc3_util(loadpng   ${UTIL_DIR}/loadpng.c ${UTIL_DIR}/upng.c ${UTIL_DIR}/upng_pc3.c)
set_source_files_properties(${UTIL_DIR}/upng.c PROPERTIES
    COMPILE_DEFINITIONS UPNG_NO_FILE)      # as utils/Makefile does
pc3_util(loadimage ${UTIL_DIR}/loadimage.c)
pc3_util(saveimage ${UTIL_DIR}/saveimage.c)
pc3_util(playmp3   ${UTIL_DIR}/playmp3.c)
pc3_util(playwav   ${UTIL_DIR}/playwav.c)
pc3_util(playflac  ${UTIL_DIR}/playflac.c)
pc3_util(playmod   ${UTIL_DIR}/playmod.c ${UTIL_DIR}/hxcmod.c)
pc3_util(playsnd   ${UTIL_DIR}/playsnd.c)
target_link_libraries(playmp3  PRIVATE m)
target_link_libraries(playflac PRIVATE m)
target_link_libraries(playmod  PRIVATE m)
target_link_libraries(playsnd  PRIVATE m)

# ---- the seam: the kernel's display core, compiled without the kernel ----
# display.c is the portable half of the PC3 display driver (its scanout
# is display_hstx.c).  This links it against a stand-in for everything
# display_priv.h asks of the hardware and drives the primitives.  If
# this stops compiling, the seam has been crossed in the kernel.
add_executable(display_seam tests/seam/display_seam.c ${PLAT_DIR}/display.c)
target_compile_definitions(display_seam PRIVATE PC3_HOST)
target_include_directories(display_seam PRIVATE ${PLAT_DIR})
target_compile_options(display_seam PRIVATE -g -Wall -Wextra -Wno-unused-parameter)

# ---- the gates ---------------------------------------------------------------
# Today's gates, run against THIS build's binaries.  They live in the
# FUZIX tree and take the binaries by environment (BIN, MMBC, MMB2C);
# cc2 writes .symtmp in the working directory, so the fcc gates each get
# their own and never run together.
enable_testing()
set(GATE ${CMAKE_BINARY_DIR}/gate)

add_test(NAME seam-display COMMAND display_seam)

add_test(NAME fcc-gate
    COMMAND ${CMAKE_COMMAND} -E env BIN=${BIN} W=${GATE}/fcc
            bash ${MMB_DIR}/fcc/fcctests.sh
    WORKING_DIRECTORY ${GATE}/fcc)
add_test(NAME fcc-gate-mmbc
    COMMAND ${CMAKE_COMMAND} -E env BIN=${BIN} W=${GATE}/fccm MMB2C=${BIN}/mmbc
            bash ${MMB_DIR}/fcc/fcctests.sh
    WORKING_DIRECTORY ${GATE}/fccm)
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
set_tests_properties(seam-display PROPERTIES LABELS seam)
