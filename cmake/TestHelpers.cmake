function(qx_add_test name)
    cmake_parse_arguments(QX_TEST "" "" "SRC" ${ARGN})

    add_executable(${name} ${QX_TEST_SRC})
    target_link_libraries(${name} PRIVATE ${QX_TEST_LINK_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE QX_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/core/fixtures")

    include(Warnings)
    qx_apply_warnings(${name})
    include(Sanitizers)
    qx_apply_sanitizers(${name})

    add_test(NAME qx.${name} COMMAND ${name})
endfunction()
