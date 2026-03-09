cmake_minimum_required(VERSION 3.16)
project(Flash4Viewer VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── wxWidgets ────────────────────────────────────────────────────────────────
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# ── FFmpeg (libav*) ──────────────────────────────────────────────────────────
# Use pkg-config on Linux/macOS; on Windows point CMAKE_PREFIX_PATH at your
# FFmpeg install or set FFMPEG_ROOT.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG REQUIRED
        libavformat
        libavcodec
        libavutil
        libswscale)
else()
    # Fallback: manual search (useful on Windows with vcpkg / prebuilt FFmpeg)
    set(FFMPEG_ROOT "" CACHE PATH "Root of FFmpeg installation")
    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
              HINTS "${FFMPEG_ROOT}/include")
    find_library(AVFORMAT_LIB avformat HINTS "${FFMPEG_ROOT}/lib")
    find_library(AVCODEC_LIB  avcodec  HINTS "${FFMPEG_ROOT}/lib")
    find_library(AVUTIL_LIB   avutil   HINTS "${FFMPEG_ROOT}/lib")
    find_library(SWSCALE_LIB  swscale  HINTS "${FFMPEG_ROOT}/lib")

    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
    set(FFMPEG_LIBRARIES
        ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB})
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

if(PkgConfig_FOUND AND FFMPEG_FOUND)
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
