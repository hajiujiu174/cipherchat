cmake_minimum_required(VERSION 3.24)
project(NEXTALK VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NEXTALK_BUILD_GUI "Build the Qt 6 desktop client" OFF)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# 双端共用核心：协议、加密、棘轮
add_library(nextalk-common STATIC
    common/auth/login_proof.cpp
    common/base64.cpp
    common/crypto/crypto.cpp
    common/net/tls_context.cpp
    common/protocol/frame.cpp
    common/protocol/messages.cpp
    common/ratchet/x3dh.cpp
    common/ratchet/double_ratchet.cpp)
target_include_directories(nextalk-common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(nextalk-common PUBLIC
    OpenSSL::Crypto OpenSSL::SSL SQLite::SQLite3 Threads::Threads)
if(MSVC)
    target_compile_options(nextalk-common PRIVATE /W4 /utf-8)
else()
    target_compile_options(nextalk-common PRIVATE -Wall -Wextra)
endif()

# 服务器核心：存储 / 认证 / 路由 / TLS 监听（集成测试复用整套服务器）
add_library(nextalk-server-core STATIC
    server/registration_email.cpp
    server/storage/storage.cpp
    server/auth/auth.cpp
    framework/src/server_services.cpp
    server/router/router.cpp
    server/net/tls_server.cpp)
target_link_libraries(nextalk-server-core PUBLIC nextalk-common)
target_include_directories(nextalk-server-core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/framework/include")
if(WIN32)
    target_link_libraries(nextalk-server-core PUBLIC ws2_32)
endif()

# 客户端核心：TLS 网络层 + 会话编排 + 文件传输（无 GUI 依赖）
add_library(nextalk-client-core STATIC
    client/core/net_client.cpp
    client/core/client.cpp
    client/core/files.cpp)
target_link_libraries(nextalk-client-core PUBLIC nextalk-common)
if(WIN32)
    target_link_libraries(nextalk-client-core PUBLIC ws2_32)
endif()

# 服务器可执行（TLS 监听）在 M2 后半段加入
add_subdirectory(server)

include(CTest)
if(NEXTALK_BUILD_GUI)
    set(CCFW_BUILD_GUI ON CACHE BOOL "Build NEXTALK desktop client" FORCE)
    add_subdirectory(framework)
elseif(BUILD_TESTING)
    # Exercise the same headless runtime as NEXTALK, without registering the
    # historical framework suites or adding a Qt dependency to server tests.
    function(nextalk_import_test_runtime)
        set(BUILD_TESTING OFF)
        set(CCFW_BUILD_GUI OFF)
        set(CCFW_BUILD_GUI_TESTS OFF)
        set(CCFW_BUILD_DELIVERY_TESTS OFF)
        add_subdirectory(framework EXCLUDE_FROM_ALL)
    endfunction()
    nextalk_import_test_runtime()
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
