# CMake toolchain for the PS4 (OpenOrbis). Usage:
#   OO_PS4_TOOLCHAIN=/path/oosdk [GOLDHEN_SDK=/path/GoldHEN_Plugins_SDK] \
#   cmake -B build-ps4 -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-toolchain.cmake && cmake --build build-ps4
# Produces build-ps4/moonlight (ELF). tools/make_pkg.sh turns it into eboot.bin + fake PKG.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(ML_PS4 ON CACHE BOOL "" FORCE)

set(OO "$ENV{OO_PS4_TOOLCHAIN}")
if(NOT OO OR NOT EXISTS "${OO}/link.x")
    message(FATAL_ERROR "OO_PS4_TOOLCHAIN must point to the OpenOrbis toolchain (link.x not found)")
endif()
get_filename_component(ML_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(LIBCXX "${ML_ROOT}/toolchain/libcxx")
if(NOT EXISTS "${LIBCXX}/lib/libc++.a")
    message(FATAL_ERROR "libc++ for PS4 missing: run toolchain/build-libcxx.sh first")
endif()
execute_process(COMMAND clang -print-resource-dir OUTPUT_VARIABLE CLANG_RES OUTPUT_STRIP_TRAILING_WHITESPACE)
# Find ld.lld: prefer the versioned one that matches the clang in PATH (e.g. ld.lld-21),
# fall back to unversioned ld.lld, then search LLVM bin dirs.
find_program(ML_LLD NAMES ld.lld-21 ld.lld-20 ld.lld-19 ld.lld-18 ld.lld
    HINTS /usr/lib/llvm-21/bin /usr/lib/llvm-20/bin /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin /usr/bin)
if(NOT ML_LLD)
    message(FATAL_ERROR "ld.lld not found — install lld (e.g. apt install lld)")
endif()
message(STATUS "Using linker: ${ML_LLD}")

# On Windows/WSL, the OpenOrbis zip omits musl C headers (string.h etc.);
# use the bundled copy in toolchain/musl-include/ as a fallback.
if(EXISTS "${OO}/include/string.h")
    set(ML_MUSL_INCLUDE "${OO}/include")
else()
    set(ML_MUSL_INCLUDE "${ML_ROOT}/toolchain/musl-include")
    message(STATUS "OpenOrbis musl headers not in ${OO}/include — using bundled toolchain/musl-include/")
endif()

set(ML_PS4_BASE "--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -U__FreeBSD__ -D_DEFAULT_SOURCE -nostdinc")
set(ML_PS4_SYS "-isystem ${ML_ROOT}/toolchain/overlay -isystem ${OO}/include -isystem ${ML_MUSL_INCLUDE} -isystem ${CLANG_RES}/include")
set(CMAKE_C_FLAGS_INIT "${ML_PS4_BASE} ${ML_PS4_SYS}")
# libc++ headers must come before the C library headers
set(CMAKE_CXX_FLAGS_INIT "${ML_PS4_BASE} -nostdinc++ -isystem ${LIBCXX}/include/c++/v1 ${ML_PS4_SYS}")

set(ML_PS4_LIBS "-lc++ -lc++abi -lc -lkernel -lSceVideoOut -lSceAudioOut -lScePad -lSceUserService -lSceSysmodule -lSceSystemService -lSceAvPlayer -lSceImeDialog -lSceCommonDialog -lSceNet -lSceNetCtl -lSceRandom")
set(ML_GOLDHEN "")
if(DEFINED ENV{GOLDHEN_SDK} AND EXISTS "$ENV{GOLDHEN_SDK}/libGoldHEN_Hook.a")
    set(ML_GOLDHEN "-L$ENV{GOLDHEN_SDK} -lGoldHEN_Hook")
    set(ML_GOLDHEN_INCLUDE "$ENV{GOLDHEN_SDK}/include" CACHE PATH "" FORCE)
endif()
set(CMAKE_CXX_LINK_EXECUTABLE
    "${ML_LLD} <OBJECTS> -o <TARGET> -m elf_x86_64 -pie --script ${ML_ROOT}/toolchain/link_ps4.x --eh-frame-hdr -L${LIBCXX}/lib -L${OO}/lib <LINK_LIBRARIES> ${ML_GOLDHEN} ${ML_PS4_LIBS} ${OO}/lib/crt1.o")
