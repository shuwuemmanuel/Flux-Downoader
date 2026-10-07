cmake_minimum_required(VERSION 3.19)

project(FluxDownloader VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(FLUX_WITH_TORRENT "Build BitTorrent support (needs libtorrent-rasterbar 2.x)" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
find_package(CURL REQUIRED)

set(FLUX_SOURCES
    src/main.cpp

    src/core/DownloadItem.h
    src/core/DownloadItem.cpp
    src/core/SettingsManager.h
    src/core/SettingsManager.cpp
    src/core/SpeedLimiter.h
    src/core/SpeedLimiter.cpp
    src/core/DownloadEngine.h
    src/core/DownloadEngine.cpp
    src/core/QueueManager.h
    src/core/QueueManager.cpp
    src/core/TorrentEngine.h
    src/core/TorrentEngine.cpp
    src/core/YouTubeEngine.h
    src/core/YouTubeEngine.cpp
    src/core/ClipboardMonitor.h
    src/core/ClipboardMonitor.cpp
    src/core/LocalServer.h
    src/core/LocalServer.cpp

    src/ui/Format.h
    src/ui/Format.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/AddDownloadDialog.h
    src/ui/AddDownloadDialog.cpp
    src/ui/AddTorrentDialog.h
    src/ui/AddTorrentDialog.cpp
    src/ui/YouTubeDialog.h
    src/ui/YouTubeDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/ProgressWindow.h
    src/ui/ProgressWindow.cpp
    src/ui/DownloadDialogs.h
    src/ui/DownloadDialogs.cpp

    resources/resources.qrc
)

if(WIN32)
    list(APPEND FLUX_SOURCES resources/app.rc)
endif()

add_executable(FluxDownloader ${FLUX_SOURCES})

target_include_directories(FluxDownloader PRIVATE src)
target_compile_definitions(FluxDownloader PRIVATE FLUX_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(FluxDownloader PRIVATE Qt6::Widgets Qt6::Network CURL::libcurl)
if(WIN32)
    # curl.h pulls in windows.h, whose min/max macros break std::min/std::max.
    target_compile_definitions(FluxDownloader PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(FluxDownloader PRIVATE /W3 /utf-8)
else()
    target_compile_options(FluxDownloader PRIVATE -Wall -Wextra)
endif()

if(FLUX_WITH_TORRENT)
    find_package(LibtorrentRasterbar 2.0 CONFIG QUIET)
    if(LibtorrentRasterbar_FOUND)
        target_link_libraries(FluxDownloader PRIVATE LibtorrentRasterbar::torrent-rasterbar)
        target_compile_definitions(FluxDownloader PRIVATE FLUX_HAVE_LIBTORRENT=1)
        message(STATUS "Flux Downloader: BitTorrent support ENABLED (libtorrent ${LibtorrentRasterbar_VERSION})")
    else()
        message(WARNING "Flux Downloader: libtorrent-rasterbar not found - BitTorrent support DISABLED. "
                        "Everything else (HTTP, YouTube, browser integration) still works.")
    endif()
endif()

set_target_properties(FluxDownloader PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Ship the Chrome extension next to the executable so Settings ->
# "Auto-Install Extension" can find it.
add_custom_command(TARGET FluxDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/chrome_extension
            $<TARGET_FILE_DIR:FluxDownloader>/chrome_extension
)

install(TARGETS FluxDownloader
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)
install(DIRECTORY chrome_extension DESTINATION .)
if(WIN32)
    install(FILES scripts/setup_extension.bat scripts/diagnose_extension.bat DESTINATION .)
endif()

# ---- Tests ---------------------------------------------------------------
option(FLUX_BUILD_TESTS "Build the core engine tests" OFF)
if(FLUX_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(flux_tests
        tests/test_core.cpp
        src/core/DownloadItem.cpp
        src/core/SettingsManager.cpp
        src/core/SpeedLimiter.cpp
        src/core/DownloadEngine.cpp
        src/core/QueueManager.cpp
        src/core/YouTubeEngine.cpp
        src/core/LocalServer.cpp
    )
    target_include_directories(flux_tests PRIVATE src)
    if(WIN32)
        target_compile_definitions(flux_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    if(MSVC)
        target_compile_options(flux_tests PRIVATE /utf-8)
    endif()
    target_link_libraries(flux_tests PRIVATE Qt6::Widgets Qt6::Network Qt6::Test CURL::libcurl)
    add_test(NAME flux_tests COMMAND flux_tests)
endif()
