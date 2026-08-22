function(qx_add_test name)
    cmake_parse_arguments(QX_TEST "" "" "SRC" ${ARGN})

    add_executable(${name} ${QX_TEST_SRC})
    target_link_libraries(${name} PRIVATE quantxecute_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE QX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures")

    include(Warnings)
    qx_apply_warnings(${name})
    include(Sanitizers)
    qx_apply_sanitizers(${name})

    add_test(NAME qx.${name} COMMAND ${name})
endfunction()

qx_add_test(types_test SRC types_test.cpp)
qx_add_test(book_test SRC book_test.cpp)
qx_add_test(decoder_test SRC decoder_test.cpp)
qx_add_test(sequence_test SRC sequence_test.cpp)
qx_add_test(execution_test SRC execution_test.cpp)
