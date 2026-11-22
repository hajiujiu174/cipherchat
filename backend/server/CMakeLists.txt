# 服务器可执行：TLS 监听 + 路由

add_executable(nextalk-server
    main.cpp smtp_mailer.cpp)
find_package(CURL REQUIRED)
target_link_libraries(nextalk-server PRIVATE CURL::libcurl)
target_link_libraries(nextalk-server PRIVATE nextalk-server-core)
if(WIN32)
    target_link_libraries(nextalk-server PRIVATE ws2_32)
endif()
