cmake_minimum_required(VERSION 3.14)

project(cc-notify
    VERSION 1.0.0
    DESCRIPTION "Claude Code task completion notifier"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(cc-notify src/main.cpp)

# ── Platform-specific sources and link libraries ──────────────────────────────

if(WIN32)
    # Windows: C++/WinRT via Windows SDK headers + windowsapp.lib
    target_sources(cc-notify PRIVATE src/notifier_win.cpp)
    target_link_libraries(cc-notify PRIVATE windowsapp)

    # /utf-8   — treat source as UTF-8 (required for non-ASCII in string literals)
    # /EHsc    — enable C++ exception handling (required by winrt headers)
    # /W4 /WX  — warnings as errors
    target_compile_options(cc-notify PRIVATE /utf-8 /EHsc /W4 /WX)

    # Static CRT: single portable .exe, no VCRUNTIME redistributable needed.
    set_property(TARGET cc-notify PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(cc-notify PRIVATE _CRT_SECURE_NO_WARNINGS)

elseif(APPLE)
    # macOS: Objective-C++ source + UserNotifications framework (macOS 10.14+)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14"
        CACHE STRING "Minimum macOS deployment version")

    target_sources(cc-notify PRIVATE src/notifier_mac.mm)
    target_link_libraries(cc-notify PRIVATE
        "-framework Foundation"
        "-framework UserNotifications"
    )
    target_compile_options(cc-notify PRIVATE -Wall -Wextra
        $<$<CONFIG:Release>:-O2>)

elseif(UNIX)
    # Linux / WSL: libnotify C API (→ D-Bus org.freedesktop.Notifications)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBNOTIFY REQUIRED libnotify)

    target_sources(cc-notify PRIVATE src/notifier_unix.cpp)
    target_include_directories(cc-notify PRIVATE ${LIBNOTIFY_INCLUDE_DIRS})
    target_link_libraries(cc-notify PRIVATE ${LIBNOTIFY_LIBRARIES})
    target_compile_options(cc-notify PRIVATE
        ${LIBNOTIFY_CFLAGS_OTHER}
        -Wall -Wextra
        $<$<CONFIG:Release>:-O2>)

    # Strip release binary for smaller size.
    target_link_options(cc-notify PRIVATE $<$<CONFIG:Release>:-s>)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS cc-notify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ── Build summary ─────────────────────────────────────────────────────────────
message(STATUS "cc-notify ${PROJECT_VERSION}")
message(STATUS "  Platform : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Install  : ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
