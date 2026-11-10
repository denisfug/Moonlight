cmake_minimum_required(VERSION 3.20)
project(MoonlightHB VERSION 0.1.0 LANGUAGES C CXX)

# Two builds from the same sources:
#   host (default): PC build for tests and screenshots
#   PS4: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-toolchain.cmake  (see docs/BUILD_GUIDE.md)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(ML_VERSION "01.11")

set(ML_WARN -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Werror=return-type -Werror=uninitialized)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>")

# ---------------------------------------------------------------- Mbed TLS 3.6.4 (Apache-2.0): the app's own TLS
file(GLOB ML_MBEDTLS_SOURCES ${CMAKE_SOURCE_DIR}/third_party/mbedtls/library/*.c)
add_library(ml_mbedtls STATIC ${ML_MBEDTLS_SOURCES})
target_include_directories(ml_mbedtls PUBLIC third_party/mbedtls/include src/net/mbedtls_cfg PRIVATE third_party/mbedtls/library)
target_compile_definitions(ml_mbedtls PUBLIC "MBEDTLS_USER_CONFIG_FILE=\"ml_mbedtls_user_config.h\"")
target_compile_options(ml_mbedtls PRIVATE -w)

# ---------------------------------------------------------------- portable core (no platform code)
add_library(ml_core STATIC
    src/core/json.cpp
    src/core/net.cpp
    src/core/md5.cpp
    src/storage/store.cpp
    src/addons/protocol.cpp
    src/addons/source_manager.cpp
    src/library/library.cpp
    src/live/m3u.cpp
    src/music/music.cpp
    src/music/subsonic.cpp
    src/radio/radio.cpp
    src/player/player.cpp
    src/player/mp3_core.c
    src/player/nv12.cpp
    src/net/tls.cpp
    src/net/web_client.cpp
    src/net/range_reader.cpp
)
target_include_directories(ml_core PUBLIC src third_party)
target_link_libraries(ml_core PUBLIC ml_mbedtls)
target_compile_options(ml_core PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${ML_WARN}>")

# ---------------------------------------------------------------- user interface (software renderer)
add_library(ml_ui STATIC
    src/ui/canvas.cpp
    src/ui/font.cpp
    src/ui/input_map.cpp
    src/ui/widgets.cpp
    src/ui/stb_impl.c
    src/ui/image_loader.cpp
    src/app/demo_data.cpp
    src/app/app.cpp
    src/app/remote.cpp
    src/app/catalog_loader.cpp
    src/app/updater.cpp
    src/app/screens_home.cpp
    src/app/screens_media.cpp
    src/app/screens_music.cpp
    src/app/screens_radio.cpp
    src/app/screens_video.cpp
    src/app/screens_misc.cpp
)
target_link_libraries(ml_ui PUBLIC ml_core)
target_compile_options(ml_ui PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${ML_WARN}>")
target_compile_definitions(ml_ui PRIVATE ML_VERSION="${ML_VERSION}")

if(ML_PS4)
    # ------------------------------------------------------------ PS4 executable (ELF -> eboot via tools/make_pkg.sh)
    add_executable(moonlight
        src/main.cpp
        src/platform/ps4/ps4_platform.cpp
        src/platform/ps4/video/video_out.cpp
        src/platform/ps4/video/av_engine.cpp
        src/platform/ps4/controller/pad.cpp
        src/platform/ps4/audio/audio_out.cpp
        src/platform/ps4/filesystem/file_system.cpp
        src/platform/ps4/network/http_client.cpp
        src/platform/ps4/controller/ime.cpp
    )
    target_link_libraries(moonlight PRIVATE ml_ui)
    target_compile_definitions(moonlight PRIVATE ML_PLATFORM_PS4 ML_VERSION="${ML_VERSION}")
    if(ML_GOLDHEN_INCLUDE)
        target_include_directories(moonlight PRIVATE ${ML_GOLDHEN_INCLUDE})
        target_compile_definitions(moonlight PRIVATE ML_HAVE_GOLDHEN)
        message(STATUS "GoldHEN SDK: ${ML_GOLDHEN_INCLUDE} (USB/data access enabled)")
    else()
        message(WARNING "GOLDHEN_SDK not set: building without sys_sdk_jailbreak (no USB music)")
    endif()
else()
    find_package(Threads REQUIRED)
    add_library(ml_host STATIC src/platform/host/host_platform.cpp)
    target_link_libraries(ml_host PUBLIC ml_core Threads::Threads)
    target_compile_definitions(ml_host PUBLIC ML_HOST_VERSION="${ML_VERSION}")

    # Headless app runner: renders screens to PNG with scripted DualShock input.
    add_executable(moonlight_host src/main.cpp)
    target_link_libraries(moonlight_host PRIVATE ml_ui ml_host)
    target_compile_definitions(moonlight_host PRIVATE ML_PLATFORM_HOST ML_VERSION="${ML_VERSION}")

    add_executable(moonlight_tests
        tests/test_main.cpp tests/test_core.cpp tests/test_addons.cpp tests/test_music.cpp tests/test_radio.cpp tests/test_subsonic.cpp tests/test_ui.cpp tests/test_net.cpp)
    target_include_directories(moonlight_tests PRIVATE tests)
    target_link_libraries(moonlight_tests PRIVATE ml_ui ml_host)
    enable_testing()
    add_test(NAME unit COMMAND moonlight_tests)
    set_tests_properties(unit PROPERTIES ENVIRONMENT "ML_FIXTURES=${CMAKE_SOURCE_DIR}/tests/fixtures")
endif()
