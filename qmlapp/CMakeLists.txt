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
    src/DiskViewModel.cpp src/DiskViewModel.h)
qt_add_qml_module(diskview-qml
    URI DiskViewPrototype
    VERSION 1.0
    QML_FILES qml/Main.qml qml/ScanSettingsDialog.qml)
qt_add_resources(diskview-qml "diskview_qml_fixture"
    PREFIX "/fixtures"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    FILES fixtures/disk-tree.json)
target_link_libraries(diskview-qml PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
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

enable_testing()
add_test(NAME diskview_chart_geometry COMMAND diskview-chart-geometry-tests)
set_tests_properties(diskview_chart_geometry PROPERTIES TIMEOUT 30)
add_test(NAME diskview_scan_settings COMMAND diskview-settings-tests)
set_tests_properties(diskview_scan_settings PROPERTIES TIMEOUT 30)
