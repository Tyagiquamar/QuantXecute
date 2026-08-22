set(QX_FEED_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FeedClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ReconnectPolicy.cpp)

add_library(quantxecute_feed STATIC ${QX_FEED_SRC})
add_library(qx::feed ALIAS quantxecute_feed)

target_include_directories(quantxecute_feed PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quantxecute_feed PUBLIC qx::core)

# Production sources are compiled under sanitizer instrumentation as well;
# tests alone would not cover this library's memory accesses.
include(Sanitizers)
qx_apply_warnings(quantxecute_feed)
qx_apply_sanitizers(quantxecute_feed)

if(QX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
