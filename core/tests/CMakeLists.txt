function(qx_add_test name)
    cmake_parse_arguments(QX_TEST "" "" "SRC" ${ARGN})

    add_executable(${name} ${QX_TEST_SRC})
    target_link_libraries(${name} PRIVATE quantxecute_core)

    include(Warnings)
    qx_apply_warnings(${name})
    include(Sanitizers)
    qx_apply_sanitizers(${name})

    add_test(NAME qx.${name} COMMAND ${name})
endfunction()

qx_add_test(types_test SRC types_test.cpp)
