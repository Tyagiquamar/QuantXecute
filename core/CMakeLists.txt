set(QX_CORE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Book.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Decoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ExecutionSimulator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Recorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Replay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SequenceValidator.cpp)

add_library(quantxecute_core STATIC ${QX_CORE_SRC})
add_library(qx::core ALIAS quantxecute_core)

target_include_directories(quantxecute_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(quantxecute_core PUBLIC cxx_std_20)

find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(quantxecute_core PUBLIC nlohmann_json::nlohmann_json)
else()
    target_include_directories(quantxecute_core SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party)
endif()

include(Warnings)
qx_apply_warnings(quantxecute_core)

if(QX_BUILD_BENCH)
    add_subdirectory(bench)
endif()
