cmake_minimum_required(VERSION 3.24)
project(DiskViewQmlPrototype VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(CMAKE_PREFIX_PATH "D:/tools/Qt/6.12.0/msvc2022_64" CACHE PATH "Qt 6.12 SDK root")
find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.12)

qt_add_executable(diskview-qml-prototype
    src/main.cpp
    src/ChartItem.cpp src/ChartItem.h
    src/DiskViewModel.cpp src/DiskViewModel.h)
qt_add_qml_module(diskview-qml-prototype
    URI DiskViewPrototype
    VERSION 1.0
    QML_FILES qml/Main.qml)
target_link_libraries(diskview-qml-prototype PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(diskview-qml-prototype PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(diskview-qml-prototype PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus)
endif()
set_target_properties(diskview-qml-prototype PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(diskview-chart-geometry-tests tests/chart-geometry-tests.cpp)
target_include_directories(diskview-chart-geometry-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

install(TARGETS diskview-qml-prototype RUNTIME DESTINATION bin)
