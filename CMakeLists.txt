cmake_minimum_required(VERSION 3.16)
project(Flash4Viewer VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── wxWidgets ────────────────────────────────────────────────────────────────
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# ── FFmpeg (libav*) ──────────────────────────────────────────────────────────
# On Windows, pkg-config returns bare library names without full paths, which
# breaks the MSVC linker. Use find_path/find_library instead so CMake always
# resolves absolute paths (works with vcpkg via CMAKE_PREFIX_PATH).
# On Linux/macOS, pkg-config is reliable and preferred.
if(WIN32)
    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h)
    find_library(AVFORMAT_LIB avformat)
    find_library(AVCODEC_LIB  avcodec)
    find_library(AVUTIL_LIB   avutil)
    find_library(SWSCALE_LIB  swscale)

    foreach(_lib FFMPEG_INCLUDE_DIR AVFORMAT_LIB AVCODEC_LIB AVUTIL_LIB SWSCALE_LIB)
        if(NOT ${_lib})
            message(FATAL_ERROR "FFmpeg component not found: ${_lib}. "
                "Ensure ffmpeg:x64-windows is installed in vcpkg and "
                "-DCMAKE_TOOLCHAIN_FILE points to vcpkg.cmake.")
        endif()
    endforeach()

    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
    set(FFMPEG_LIBRARIES ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB})
else()
    # Linux / macOS: pkg-config gives correct full paths
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED
        libavformat
        libavcodec
        libavutil
        libswscale)
endif()

# ── Source files ─────────────────────────────────────────────────────────────
set(SOURCES
    src/App.cpp
    src/MainFrame.cpp)

set(HEADERS
    include/App.h
    include/MainFrame.h)

# ── Executable ───────────────────────────────────────────────────────────────
if(WIN32)
    # GUI subsystem on Windows (no console window)
    add_executable(FlashView WIN32 ${SOURCES} ${HEADERS})
elseif(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "FlashView")
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_ICON_FILE FlashView.png)
    set(APP_ICON resources/FlashView.png)
    set_source_files_properties(${APP_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    add_executable(FlashView MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${APP_ICON})
else()
    add_executable(FlashView ${SOURCES} ${HEADERS})
endif()

target_include_directories(FlashView PRIVATE
    include
    ${FFMPEG_INCLUDE_DIRS})

target_link_libraries(FlashView PRIVATE
    ${wxWidgets_LIBRARIES}
    ${FFMPEG_LIBRARIES})

if(NOT WIN32 AND FFMPEG_CFLAGS_OTHER)
    target_compile_options(FlashView PRIVATE ${FFMPEG_CFLAGS_OTHER})
endif()

if(WIN32)
    target_link_libraries(FlashView PRIVATE ws2_32)
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS FlashView
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE)
    install(FILES resources/FlashView.png
            DESTINATION share/pixmaps)
endif()
