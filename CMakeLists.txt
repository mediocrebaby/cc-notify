cmake_minimum_required(VERSION 3.14)

project(cc-notify
    VERSION 1.0.0
    DESCRIPTION "Claude Code task completion notifier"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)



if(APPLE)
  add_executable(cc-notify MACOSX_BUNDLE src/main.cpp)
else()
  add_executable(cc-notify src/main.cpp)
endif()

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

  # Copy icon files to the output directory after build.
  add_custom_command(TARGET cc-notify POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_SOURCE_DIR}/win/icon.png"
              "$<TARGET_FILE_DIR:cc-notify>/icon.png"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_SOURCE_DIR}/resources/icons_ico/icon_64x64.ico"
              "$<TARGET_FILE_DIR:cc-notify>/icon.ico"
      COMMENT "Copying icon files to output directory"
  )

elseif(APPLE)
  # macOS: Objective-C++ source + UserNotifications framework (macOS 10.14+)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 17)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14"
        CACHE STRING "Minimum macOS deployment version")

  target_sources(cc-notify PRIVATE src/notifier_mac.mm)
  target_link_libraries(cc-notify PRIVATE
        "-framework Cocoa"
        "-framework UserNotifications"
    )
  target_compile_options(cc-notify PRIVATE -Wall -Wextra
        $<$<CONFIG:Release>:-O2>)

  # UNUserNotificationCenter requires a valid app bundle.
  # Package the binary as cc-notify.app so the system can resolve it.
  set_target_properties(cc-notify PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER  "com.cc-notify.app"
        MACOSX_BUNDLE_BUNDLE_NAME     "cc-notify"
        MACOSX_BUNDLE_BUNDLE_VERSION  "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST      "${CMAKE_SOURCE_DIR}/macos/Info.plist.in"
    )

    # Copy icon into the app bundle's Resources folder.
    set(ICON_SRC "${CMAKE_SOURCE_DIR}/macos/AppIcon.icns")
    add_custom_command(TARGET cc-notify POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:cc-notify>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ICON_SRC}"
                "$<TARGET_BUNDLE_DIR:cc-notify>/Contents/Resources/AppIcon.icns"
        COMMENT "Copying app icon into bundle"
    )

    # Ad-hoc code sign so macOS will honour UNUserNotificationCenter requests.
    add_custom_command(TARGET cc-notify POST_BUILD
        COMMAND codesign --force --deep --sign -
                "$<TARGET_BUNDLE_DIR:cc-notify>"
        COMMENT "Ad-hoc signing cc-notify.app"
    )

    # Register with Launch Services so the notification icon stays up-to-date.
    add_custom_command(TARGET cc-notify POST_BUILD
        COMMAND /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
                -f "$<TARGET_BUNDLE_DIR:cc-notify>"
        COMMENT "Registering cc-notify.app with Launch Services"
    )

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
if(APPLE)
  install(TARGETS cc-notify BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  install(TARGETS cc-notify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ── Build summary ─────────────────────────────────────────────────────────────
message(STATUS "cc-notify ${PROJECT_VERSION}")
message(STATUS "  Platform : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Install  : ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
