include(FetchContent)

# Production OKX transport needs a maintained TLS-capable WebSocket client.
# IXWebSocket is already a project dependency for the optional desktop
# console; reuse it here rather than hand-rolling protocol code.
if(NOT TARGET ixwebsocket::ixwebsocket)
    set(USE_TLS ON CACHE BOOL "" FORCE)
    set(USE_ZLIB OFF CACHE BOOL "" FORCE)
    set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(ixwebsocket
        GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
        GIT_TAG v11.4.5)
    FetchContent_MakeAvailable(ixwebsocket)
endif()

set(QX_FEED_OKX_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/../feed/src/OkxWebSocketSource.cpp)

add_library(quantxecute_feed_okx STATIC ${QX_FEED_OKX_SRC})
add_library(qx::feed_okx ALIAS quantxecute_feed_okx)

target_include_directories(quantxecute_feed_okx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../feed/include)

target_link_libraries(quantxecute_feed_okx PUBLIC
    qx::feed
    ixwebsocket::ixwebsocket)

target_compile_definitions(quantxecute_feed_okx PRIVATE IXWEBSOCKET=1)

include(Warnings)
qx_apply_warnings(quantxecute_feed_okx)
include(Sanitizers)
qx_apply_sanitizers(quantxecute_feed_okx)

# REST/WS surface as its own linkable unit so the engine executable AND
# tests can both consume it without recompiling sources by hand.
set(QX_SERVER_API_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ApiServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebSocket.cpp)

add_library(quantxecute_server_api STATIC ${QX_SERVER_API_SRC})
add_library(qx::server_api ALIAS quantxecute_server_api)

target_include_directories(quantxecute_server_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(quantxecute_server_api PUBLIC qx::feed)

include(Warnings)
qx_apply_warnings(quantxecute_server_api)
include(Sanitizers)
qx_apply_sanitizers(quantxecute_server_api)

add_executable(quantxecute-server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HttpServer.cpp)

target_include_directories(quantxecute-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(quantxecute-server PRIVATE
    quantxecute_server_api
    quantxecute_feed
    quantxecute_feed_okx)

find_package(Threads REQUIRED)
target_link_libraries(quantxecute-server PRIVATE Threads::Threads)

include(Warnings)
qx_apply_warnings(quantxecute-server)
include(Sanitizers)
qx_apply_sanitizers(quantxecute-server)

# Optional deterministic live-smoke tool: connects to public OKX, verifies a
# snapshot and incremental continuity, exits non-zero on failure. Never used
# by automated CI (which must stay hermetic); intended for manual runs.
add_executable(quantxecute-live-smoke
    ${CMAKE_CURRENT_SOURCE_DIR}/src/live_smoke.cpp)

target_link_libraries(quantxecute-live-smoke PRIVATE
    quantxecute_feed
    quantxecute_feed_okx
    Threads::Threads)

qx_apply_warnings(quantxecute-live-smoke)
qx_apply_sanitizers(quantxecute-live-smoke)

if(QX_BUILD_TESTS)
    include(TestHelpers)

    set(QX_SERVER_TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tests/api_server_test.cpp)
    add_executable(api_server_test ${QX_SERVER_TEST_SRC})
    target_include_directories(api_server_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(api_server_test PRIVATE
        quantxecute_server_api
        quantxecute_feed_okx
        Threads::Threads)
    include(Warnings)
    qx_apply_warnings(api_server_test)
    include(Sanitizers)
    qx_apply_sanitizers(api_server_test)
    add_test(NAME qx.api_server_test COMMAND api_server_test)

    # Deterministic OkxWebSocketSource transport coverage: the real IXWebSocket
    # machinery driven against an in-process loopback stub server, so Open /
    # Message / Close / Error callbacks, the liveness worker, upgrade-notice
    # teardown and stop()/reconnect ordering are exercised under sanitizers
    # without any external network dependency.
    set(QX_TEST_LINK_LIBS quantxecute_server_api quantxecute_feed_okx Threads::Threads)
    qx_add_test(okx_source_test SRC ${CMAKE_CURRENT_SOURCE_DIR}/tests/okx_source_test.cpp)
endif()