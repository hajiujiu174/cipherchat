cmake_minimum_required(VERSION 3.24)
project(NEXTALKServer VERSION 0.3.26.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/ServerCore.cmake)
add_subdirectory(server)

include(CTest)
if(BUILD_TESTING)
    set(runtime_dir "${CMAKE_CURRENT_SOURCE_DIR}/../frontend/runtime")
    set(shared_dir "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
    # The backend test suite also exercises the historical crypto vectors.
    add_library(nextalk-common STATIC
        "${shared_dir}/common/ratchet/x3dh.cpp"
        "${shared_dir}/common/ratchet/double_ratchet.cpp")
    target_link_libraries(nextalk-common PUBLIC ccfw_existing)
    add_library(nextalk-client-core STATIC
        "${runtime_dir}/client/core/net_client.cpp")
    target_include_directories(nextalk-client-core PUBLIC "${runtime_dir}")
    target_link_libraries(nextalk-client-core PUBLIC nextalk-common nextalk-server-core)

    # 把客户端运行时引入服务端测试构建，避免再构建附带界面和测试。
    function(import_test_runtime)
        set(BUILD_TESTING OFF)
        set(CCFW_BUILD_GUI OFF)
        set(CCFW_BUILD_GUI_TESTS OFF)
        set(CCFW_BUILD_DELIVERY_TESTS OFF)
        add_subdirectory("${runtime_dir}" runtime EXCLUDE_FROM_ALL)
    endfunction()
    import_test_runtime()
    add_subdirectory(tests)
endif()
