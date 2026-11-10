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

set(ML_PS4_BASE "--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -U__FreeBSD__ -D_DEFAULT_SOURCE -nostdinc")
set(ML_PS4_SYS "-isystem ${ML_ROOT}/toolchain/overlay -isystem ${OO}/include -isystem ${CLANG_RES}/include")
set(CMAKE_C_FLAGS_INIT "${ML_PS4_BASE} ${ML_PS4_SYS}")
# libc++ headers must come before the C library headers
set(CMAKE_CXX_FLAGS_INIT "${ML_PS4_BASE} -nostdinc++ -isystem ${LIBCXX}/include/c++/v1 ${ML_PS4_SYS}")

set(ML_PS4_LIBS "-lc++ -lc++abi -lc -lkernel -lSceVideoOut -lSceAudioOut -lScePad -lSceUserService -lSceSysmodule -lSceSystemService -lSceAvPlayer -lSceImeDialog -lSceCommonDialog -lSceNet -lSceSsl -lSceHttp")
set(ML_GOLDHEN "")
if(DEFINED ENV{GOLDHEN_SDK} AND EXISTS "$ENV{GOLDHEN_SDK}/libGoldHEN_Hook.a")
    set(ML_GOLDHEN "-L$ENV{GOLDHEN_SDK} -lGoldHEN_Hook")
    set(ML_GOLDHEN_INCLUDE "$ENV{GOLDHEN_SDK}/include" CACHE PATH "" FORCE)
endif()
set(CMAKE_CXX_LINK_EXECUTABLE
    "ld.lld <OBJECTS> -o <TARGET> -m elf_x86_64 -pie --script ${OO}/link.x --eh-frame-hdr -L${OO}/lib -L${LIBCXX}/lib <LINK_LIBRARIES> ${ML_GOLDHEN} ${ML_PS4_LIBS} ${OO}/lib/crt1.o")
