# A stable process identity for Windows regressions. Each CTest still gets a
# fresh process; only its entry point is shared, so Qt and core stay separate.
if(WIN32)
    add_executable(ccfw-test-host "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/test_host.cpp")
    set_target_properties(ccfw-test-host PROPERTIES OUTPUT_NAME nextalk-tests
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/test-host")
    if(MINGW)
        target_link_options(ccfw-test-host PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
endif()

# 创建客户端框架的测试目标并配置测试链接依赖。
function(ccfw_test_binary name)
    if(WIN32)
        add_library(${name} MODULE ${ARGN} "${NEXTALK_RUNTIME_DIR}/tests/support/module_entry.cpp")
        target_compile_definitions(${name} PRIVATE main=ccfw_module_main
            CCFW_TEST_HOST_PATH="$<TARGET_FILE:ccfw-test-host>")
        set_target_properties(${name} PROPERTIES PREFIX "")
        add_dependencies(${name} ccfw-test-host)
    else()
        add_executable(${name} ${ARGN})
    endif()
    target_link_libraries(${name} PRIVATE ccfw_contracts)
endfunction()

# 将已定义的客户端框架测试目标注册给 CTest。
function(ccfw_test name target)
    if(WIN32)
        add_test(NAME ${name} COMMAND ccfw-test-host --module "$<TARGET_FILE:${target}>" ${ARGN})
    else()
        add_test(NAME ${name} COMMAND ${target} ${ARGN})
    endif()
endfunction()
