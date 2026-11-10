cmake_minimum_required(VERSION 3.21)
project(diskview LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The generated C++ template builds the exe straight into the plugin folder,
# because plugin.json names it as runtime.program. Keep that convention.
add_executable(diskview main.cpp diskview.rc vendor/winshun/Ntfs.cpp)
target_compile_definitions(diskview PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# The viewer window: user32 (windows, DPI) and gdi32 (painting).
target_link_libraries(diskview PRIVATE user32 gdi32 shell32 advapi32 ole32 d2d1 uuid)

if(MSVC)
    target_compile_options(diskview PRIVATE /utf-8 /W4 /permissive-)
    # Static CRT: the exe needs no runtime DLL.
    set_target_properties(diskview PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(diskview PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -O2)
    target_link_options(diskview PRIVATE -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(diskview PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(diskview PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}")
endforeach()

# Layout/scanner regressions plus hidden-window message and rendering checks.
add_executable(diskview_tests tests/test_layout.cpp vendor/winshun/Ntfs.cpp)
target_include_directories(diskview_tests PRIVATE "${CMAKE_SOURCE_DIR}")
target_compile_definitions(diskview_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# viewer.h (the window's colours) needs the same windows libraries as the exe.
target_link_libraries(diskview_tests PRIVATE user32 gdi32 shell32 advapi32 ole32 d2d1 uuid)
if(MSVC)
    target_compile_options(diskview_tests PRIVATE /utf-8 /W4 /permissive-)
    # Static CRT, like the plugin exe: the test binary must run on a machine
    # without the redistributable installed.
    set_target_properties(diskview_tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(diskview_tests PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -O2)
    target_link_options(diskview_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(diskview_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(diskview_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()

enable_testing()
add_test(NAME diskview_logic COMMAND diskview_tests)
set_tests_properties(diskview_logic PROPERTIES TIMEOUT 30)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/tools/verify/smoke-plugin.py")
    add_test(NAME diskview_protocol COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/verify/smoke-plugin.py" "$<TARGET_FILE:diskview>")
    set_tests_properties(diskview_protocol PROPERTIES TIMEOUT 45)
endif()
