cmake_minimum_required(VERSION 3.21)

project(anno117_pipe LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "anno117_pipe targets Windows (Win32 + Direct3D 12) only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)

find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(anno117_pipe
    src/main.cpp
    src/pipe.cpp
    src/pipe.h
    src/statistics_server.cpp
    src/statistics_server.h
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

target_include_directories(anno117_pipe PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(anno117_pipe PRIVATE
    d3d12
    dxgi
    d3dcompiler
    httplib::httplib
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(anno117_pipe PRIVATE /W4 /EHsc)
endif()

# The calculator is vendored as a submodule (external/anno-117-calculator) rather than a sibling
# checkout, so its default serving path is baked in relative to this source tree rather than
# guessed relative to the built exe's location - this is a build-from-source example app, not
# something installed away from its source. --calculator-dir still overrides it at runtime (see
# main.cpp); the app already degrades gracefully when the directory doesn't exist.
target_compile_definitions(anno117_pipe PRIVATE
    ANNO117_CALCULATOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/external/anno-117-calculator"
)
