cmake_minimum_required(VERSION 3.20)
project(visual_iptv_x11 VERSION 1.2.12 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VIPTV_SANITIZE "Enable AddressSanitizer/UBSan" OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
find_program(FFMPEG_EXECUTABLE NAMES ffmpeg REQUIRED)
find_program(MPV_EXECUTABLE NAMES mpv)

function(vip_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2)
    if(VIPTV_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

function(vip_thumbnail_policy_wrap target)
    target_link_options(${target} PRIVATE
        -Wl,--wrap=vip_thumbnail_scheduler_enqueue
        -Wl,--wrap=vip_thumbnail_scheduler_cancel_pending
        -Wl,--wrap=vip_thumbnail_capture_with_decoder)
endfunction()

add_library(vip_core src/core/core.c src/core/streaming_services.c)
target_include_directories(vip_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
vip_warnings(vip_core)

add_library(vip_provider
    src/provider/xtream.c
    src/provider/m3u.c
    src/provider/server_resolver.c
    src/provider/pluto.c)
target_link_libraries(vip_provider PUBLIC vip_core PkgConfig::CURL PkgConfig::JSONC OpenSSL::Crypto)
vip_warnings(vip_provider)

add_library(vip_database src/database/database.c)
target_link_libraries(vip_database PUBLIC vip_core PkgConfig::SQLITE3 Threads::Threads)
vip_warnings(vip_database)

add_library(vip_decoder src/decoder/decoder.c src/decoder/ffmpeg_cli.c)
target_include_directories(vip_decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder)
target_link_libraries(vip_decoder PUBLIC vip_core)
vip_warnings(vip_decoder)

add_library(vip_thumbnails src/thumbnails/thumbnails.c)
target_link_libraries(vip_thumbnails PUBLIC vip_core vip_decoder PkgConfig::JPEG PkgConfig::PNG PkgConfig::WEBP PkgConfig::CURL OpenSSL::Crypto Threads::Threads)
vip_warnings(vip_thumbnails)

add_library(vip_player_mpv src/player_mpv/player_mpv.c)
target_link_libraries(vip_player_mpv PUBLIC vip_core Threads::Threads PkgConfig::JSONC PkgConfig::X11)
vip_warnings(vip_player_mpv)

add_library(vip_ui_x11 src/ui_x11/x11_app.c)
target_link_libraries(vip_ui_x11 PUBLIC
    vip_core vip_provider vip_database vip_decoder vip_thumbnails vip_player_mpv
    PkgConfig::X11 PkgConfig::JPEG PkgConfig::CURL Threads::Threads)
vip_warnings(vip_ui_x11)

add_executable(visual-iptv
    src/app/main.c
    src/app/hub.c
    src/app/pluto_app.c
    src/thumbnails/thumbnail_policy.c)
set_target_properties(visual-iptv PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(visual-iptv PRIVATE vip_ui_x11)
vip_thumbnail_policy_wrap(visual-iptv)
vip_warnings(visual-iptv)

add_executable(vip-thumb-probe src/tools/thumb_probe.c)
set_target_properties(vip-thumb-probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(vip-thumb-probe PRIVATE vip_thumbnails vip_decoder)
vip_warnings(vip-thumb-probe)

message(STATUS "X11: ${X11_VERSION}")
message(STATUS "FFmpeg: ${FFMPEG_EXECUTABLE}")
if(MPV_EXECUTABLE)
    message(STATUS "mpv runtime: ${MPV_EXECUTABLE}")
else()
    message(STATUS "mpv runtime: not found in build environment (required on target system)")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_core tests/test_core.c)
    target_link_libraries(test_core PRIVATE vip_core)
    vip_warnings(test_core)
    add_test(NAME core COMMAND test_core)

    add_executable(test_streaming_services tests/test_streaming_services.c)
    target_link_libraries(test_streaming_services PRIVATE vip_core)
    vip_warnings(test_streaming_services)
    add_test(NAME streaming_services COMMAND test_streaming_services)

    add_executable(test_provider tests/test_provider.c)
    target_link_libraries(test_provider PRIVATE vip_provider PkgConfig::CURL)
    vip_warnings(test_provider)
    add_test(NAME provider COMMAND test_provider)

    add_executable(test_pluto tests/test_pluto.c)
    target_link_libraries(test_pluto PRIVATE vip_provider)
    vip_warnings(test_pluto)
    add_test(NAME pluto COMMAND test_pluto)

    add_executable(test_server_resolver tests/test_server_resolver.c)
    target_link_libraries(test_server_resolver PRIVATE vip_provider)
    vip_warnings(test_server_resolver)
    add_test(NAME server_resolver COMMAND test_server_resolver)

    add_executable(test_m3u tests/test_m3u.c)
    target_link_libraries(test_m3u PRIVATE vip_provider)
    vip_warnings(test_m3u)
    add_test(NAME m3u COMMAND test_m3u)

    add_executable(test_database tests/test_database.c)
    target_link_libraries(test_database PRIVATE vip_database)
    vip_warnings(test_database)
    add_test(NAME database COMMAND test_database)

    add_executable(test_thumbnails tests/test_thumbnails.c)
    target_link_libraries(test_thumbnails PRIVATE vip_thumbnails Threads::Threads)
    vip_warnings(test_thumbnails)
    add_test(NAME thumbnails COMMAND test_thumbnails)

    add_executable(test_thumbnail_policy tests/test_thumbnail_policy.c src/thumbnails/thumbnail_policy.c)
    target_link_libraries(test_thumbnail_policy PRIVATE vip_thumbnails Threads::Threads)
    vip_thumbnail_policy_wrap(test_thumbnail_policy)
    vip_warnings(test_thumbnail_policy)
    add_test(NAME thumbnail_policy COMMAND test_thumbnail_policy)

    add_executable(test_decoder_ffmpeg tests/test_decoder_ffmpeg.c)
    target_link_libraries(test_decoder_ffmpeg PRIVATE vip_decoder vip_thumbnails)
    vip_warnings(test_decoder_ffmpeg)
    add_test(NAME decoder_ffmpeg COMMAND test_decoder_ffmpeg)

    add_executable(test_player_mpv tests/test_player_mpv.c)
    target_link_libraries(test_player_mpv PRIVATE vip_player_mpv)
    vip_warnings(test_player_mpv)
    add_test(NAME player_mpv COMMAND test_player_mpv)
endif()

install(TARGETS visual-iptv RUNTIME DESTINATION bin)
install(FILES packaging/visual-iptv.desktop DESTINATION share/applications)
install(FILES assets/blazzing-256.png DESTINATION share/icons/hicolor/256x256/apps RENAME blazzing.png)
install(FILES assets/blazzing.png DESTINATION share/icons/hicolor/512x512/apps RENAME blazzing.png)
