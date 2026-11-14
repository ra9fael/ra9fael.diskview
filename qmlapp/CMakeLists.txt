# The QML edition of Disk View. Built through the branch root with
# -DDISKVIEW_BUILD_QML=ON, so the native and the QML targets share one
# toolchain and one Qt configuration.

# Scoped to this directory on purpose: the native target keeps its own
# settings and never gets Qt's AUTOMOC.
qt_policy(SET QTP0001 NEW)
qt_standard_project_setup(REQUIRES 6.12)

qt_add_executable(diskview-qml
    src/main.cpp
    src/QmlHost.cpp src/QmlHost.h
    src/ScanService.cpp src/ScanService.h
    src/ScanSettings.cpp src/ScanSettings.h
    src/ChartItem.cpp src/ChartItem.h
    src/DiskViewModel.cpp src/DiskViewModel.h
    src/DockState.cpp src/DockState.h
    src/FileListModel.cpp src/FileListModel.h
    src/ThemeBridge.cpp src/ThemeBridge.h
    # The function icons: the same Phosphor set and pixel-aligned glyph
    # renderer the main app uses, copied so the plugin stands alone.
    src/Phosphor.cpp src/Phosphor.h
    src/PhosphorGlyph.cpp src/PhosphorGlyph.h
    # The elevated scan helper reads the NTFS MFT with this reader.
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/winshun/Ntfs.cpp")
qt_add_qml_module(diskview-qml
    URI DiskViewPrototype
    VERSION 1.0
    QML_FILES qml/Main.qml qml/ScanSettingsDialog.qml qml/ScanIssuesDialog.qml qml/Glyph.qml qml/DockWorkspace.qml)
qt_add_resources(diskview-qml "diskview_qml_fixture"
    PREFIX "/fixtures"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    FILES fixtures/disk-tree.json)
# The icon fonts the glyphs are drawn from (Phosphor 2.1.2, MIT; see
# resources/fonts/Phosphor-LICENSE.txt). ws::phosphor::loadFonts reads them
# from :/fonts before the first window.
qt_add_resources(diskview-qml "diskview_qml_fonts"
    PREFIX "/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
    FILES resources/fonts/Phosphor.ttf resources/fonts/Phosphor-Fill.ttf)
# The window icon is the app.ico the native edition ships in its rc: one file,
# so both editions show the same icon in the taskbar and the title bar.
qt_add_resources(diskview-qml "diskview_qml_icon"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/.."
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/../app.ico")
target_link_libraries(diskview-qml PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
# The scan helper needs the shell (to ask for authorization), the security
# API (for the pipe's access control) and COM (for the session id).
target_link_libraries(diskview-qml PRIVATE user32 shell32 advapi32 ole32 uuid)
# The generated type registration includes the QML_ELEMENT headers by their
# bare name, so their folder has to be on the include path. The branch root
# holds scan.h and tree.h, which the scanner reuses.
target_include_directories(diskview-qml PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_definitions(diskview-qml PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(diskview-qml PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus)
    # The dynamic CRT, matching the prebuilt Qt runtime: a static CRT app and
    # the Qt DLLs do not share a heap, and the QML type registration crosses
    # that boundary while the module registers itself.
    set_target_properties(diskview-qml PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
# The standalone launcher: a plain executable, so the QML edition can be
# exercised without the WinShun host loading a plugin.
set_target_properties(diskview-qml PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# A console subsystem launcher: the self-test report and the QML diagnostics
# have to reach the terminal that started it.
set_target_properties(diskview-qml PROPERTIES WIN32_EXECUTABLE OFF)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(diskview-qml PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# ---- the WinShun plugin build ------------------------------------------------
#
# WinShun runs the program named in plugin.json and talks JSON-RPC over its
# stdin/stdout. The split is the one WinShun's own winshun.capture and
# ra9fael.binarylab-qml use: a starter without Qt (static CRT, no runtime DLL
# for the strict sandbox to trip over) loads a DLL built on the host's Qt.
#
# It is behind its own switch so the standalone launcher above keeps building
# on a machine without a WinShun checkout.
option(DISKVIEW_BUILD_PLUGIN "Build the WinShun plugin variant of the QML edition" OFF)
if(DISKVIEW_BUILD_PLUGIN)
    # The manifest has to name a program, so the stub keeps the edition's name.
    add_executable(diskview-qml-plugin stub/main.cpp)
    set_target_properties(diskview-qml-plugin PROPERTIES OUTPUT_NAME "diskview-qml")
    target_compile_definitions(diskview-qml-plugin PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(diskview-qml-plugin PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(diskview-qml-plugin PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/plugin")
    endforeach()
    if(MSVC)
        target_compile_options(diskview-qml-plugin PRIVATE /utf-8 /W4 /permissive-)
        # Static CRT: the strict sandbox has to be able to start the starter
        # before any Qt is loaded.
        set_target_properties(diskview-qml-plugin PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    qt_add_library(diskview-qml-app SHARED
        src/plugin_main.cpp
        src/QmlHost.cpp src/QmlHost.h
        src/ScanService.cpp src/ScanService.h
        src/ScanSettings.cpp src/ScanSettings.h
        src/ChartItem.cpp src/ChartItem.h
        src/DiskViewModel.cpp src/DiskViewModel.h
        src/DockState.cpp src/DockState.h
        src/FileListModel.cpp src/FileListModel.h
        src/ThemeBridge.cpp src/ThemeBridge.h
        src/Phosphor.cpp src/Phosphor.h
        src/PhosphorGlyph.cpp src/PhosphorGlyph.h
        "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/winshun/Ntfs.cpp")
    qt_add_qml_module(diskview-qml-app
        URI DiskViewPrototype
        VERSION 1.0
        QML_FILES qml/Main.qml qml/ScanSettingsDialog.qml qml/ScanIssuesDialog.qml qml/Glyph.qml qml/DockWorkspace.qml)
    qt_add_resources(diskview-qml-app "diskview_qml_fixture"
        PREFIX "/fixtures"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        FILES fixtures/disk-tree.json)
    qt_add_resources(diskview-qml-app "diskview_qml_fonts"
        PREFIX "/fonts"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
        FILES resources/fonts/Phosphor.ttf resources/fonts/Phosphor-Fill.ttf)
    qt_add_resources(diskview-qml-app "diskview_qml_icon"
        PREFIX "/"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/.."
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/../app.ico")
    target_link_libraries(diskview-qml-app PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
        user32 shell32 advapi32 ole32 uuid)
    target_include_directories(diskview-qml-app PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_compile_definitions(diskview-qml-app PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    # The plugin protocol header lives at the branch root.
    target_include_directories(diskview-qml-app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    if(MSVC)
        target_compile_options(diskview-qml-app PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus)
        set_target_properties(diskview-qml-app PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    # On Windows the DLL is a runtime artifact, not a library one: this is the
    # folder the stub loads it from.
    set_target_properties(diskview-qml-app PROPERTIES
        OUTPUT_NAME "DiskViewQmlApp"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(diskview-qml-app PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/plugin"
            RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/plugin")
    endforeach()
endif()

add_executable(diskview-chart-geometry-tests tests/chart-geometry-tests.cpp)
target_include_directories(diskview-chart-geometry-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_target_properties(diskview-chart-geometry-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(diskview-chart-geometry-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()

# The settings of the scan: defaults, clamping and the round trip through the
# settings file.
add_executable(diskview-settings-tests tests/settings-tests.cpp src/ScanSettings.cpp src/ScanSettings.h)
target_link_libraries(diskview-settings-tests PRIVATE Qt6::Core)
target_include_directories(diskview-settings-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(diskview-settings-tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    set_target_properties(diskview-settings-tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set_target_properties(diskview-settings-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(diskview-settings-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()

# The scanner's contract with the caller: a held scan, the service on top of
# it, and the directories the scan could not read.
add_executable(diskview-scan-tests tests/scan-tests.cpp src/ScanService.cpp src/ScanSettings.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/winshun/Ntfs.cpp")
target_link_libraries(diskview-scan-tests PRIVATE Qt6::Core user32 shell32 advapi32 ole32 uuid)
target_include_directories(diskview-scan-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_definitions(diskview-scan-tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    set_target_properties(diskview-scan-tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set_target_properties(diskview-scan-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(diskview-scan-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()

# The list panel of the tree: the order of the rows, the filter, the ranking
# of the largest files and the history of the visited folders.
add_executable(diskview-model-tests tests/model-tests.cpp src/DiskViewModel.cpp src/DiskViewModel.h)
# The extension colours are QColor values, so the test needs Gui as well.
target_link_libraries(diskview-model-tests PRIVATE Qt6::Core Qt6::Gui)
target_include_directories(diskview-model-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(diskview-model-tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    set_target_properties(diskview-model-tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set_target_properties(diskview-model-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(diskview-model-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()

enable_testing()
add_executable(diskview-dock-tests tests/dock-tests.cpp src/DockState.cpp src/DockState.h)
target_link_libraries(diskview-dock-tests PRIVATE Qt6::Core)
target_include_directories(diskview-dock-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    set_target_properties(diskview-dock-tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set_target_properties(diskview-dock-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_test(NAME diskview_dock COMMAND diskview-dock-tests)
add_test(NAME diskview_chart_geometry COMMAND diskview-chart-geometry-tests)
set_tests_properties(diskview_chart_geometry PROPERTIES TIMEOUT 30)
add_test(NAME diskview_scan_settings COMMAND diskview-settings-tests)
set_tests_properties(diskview_scan_settings PROPERTIES TIMEOUT 30)
add_test(NAME diskview_scan_behaviour COMMAND diskview-scan-tests)
set_tests_properties(diskview_scan_behaviour PROPERTIES TIMEOUT 60)
add_test(NAME diskview_list_model COMMAND diskview-model-tests)
set_tests_properties(diskview_list_model PROPERTIES TIMEOUT 30)
