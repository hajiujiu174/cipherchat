# 服务器可执行：TLS 监听 + 路由

add_executable(cipherchat-server
    main.cpp smtp_mailer.cpp)
find_package(CURL REQUIRED)
target_link_libraries(cipherchat-server PRIVATE CURL::libcurl)
target_link_libraries(cipherchat-server PRIVATE cipherchat-server-core)
if(WIN32)
    target_link_libraries(cipherchat-server PRIVATE ws2_32)
endif()
