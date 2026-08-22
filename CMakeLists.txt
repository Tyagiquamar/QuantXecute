cmake_minimum_required(VERSION 3.21)

project(QuantXecute
    VERSION 0.2.0
    DESCRIPTION "Real-time market-data and execution-simulation engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(QX_BUILD_TESTS "Build the QuantXecute test suite" ON)
option(QX_BUILD_CONSOLE "Build the legacy ImGui desktop console (needs glfw/opengl/imgui/ixwebsocket)" OFF)
option(QX_WERROR "Treat compiler warnings as errors" ON)
option(QX_SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
option(QX_SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)
option(QX_SANITIZE_THREAD "Enable ThreadSanitizer" OFF)

add_subdirectory(core)

if(QX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()

if(QX_BUILD_CONSOLE)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(ixwebsocket CONFIG REQUIRED)

    set(QX_CONSOLE_SRC
        src/main.cpp
        src/models/SimulationModel.cpp
        src/models/WebSocketClient.cpp
        src/ui/imgui.cpp
        src/ui/imgui_draw.cpp
        src/ui/imgui_tables.cpp
        src/ui/imgui_widgets.cpp
        src/ui/InputPanel.cpp
        src/ui/OutputPanel.cpp
        src/ui/backend/imgui_impl_glfw.cpp
        src/ui/backend/imgui_impl_opengl3.cpp)

    add_executable(TradeSimulator ${QX_CONSOLE_SRC})

    target_include_directories(TradeSimulator PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/backend)

    target_link_libraries(TradeSimulator PRIVATE
        quantxecute_core
        glfw
        OpenGL::GL
        nlohmann_json::nlohmann_json
        ixwebsocket::ixwebsocket)

    qx_apply_warnings(TradeSimulator)
endif()
