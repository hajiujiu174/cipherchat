include_guard(GLOBAL)

get_filename_component(nextalk_backend_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${nextalk_backend_dir}/../shared/cmake/Protocol.cmake")
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

# Shared by the server binary and integration tests.
add_library(ccfw_server STATIC
    "${nextalk_backend_dir}/server/storage/server_services.cpp"
    "${nextalk_backend_dir}/server/registration_email.cpp")
target_include_directories(ccfw_server PUBLIC "${nextalk_backend_dir}" "${nextalk_backend_dir}/include")
target_link_libraries(ccfw_server PUBLIC ccfw_existing SQLite3::SQLite3)

add_library(nextalk-server-core STATIC
    "${nextalk_backend_dir}/../shared/common/net/tls_context.cpp"
    "${nextalk_backend_dir}/server/storage/storage.cpp"
    "${nextalk_backend_dir}/server/router/router.cpp"
    "${nextalk_backend_dir}/server/auth/session.cpp"
    "${nextalk_backend_dir}/server/chat/delivery.cpp"
    "${nextalk_backend_dir}/server/files/transfer.cpp"
    "${nextalk_backend_dir}/server/social/contacts.cpp"
    "${nextalk_backend_dir}/server/groups/membership.cpp"
    "${nextalk_backend_dir}/server/games/relay.cpp"
    "${nextalk_backend_dir}/server/net/tls_server.cpp")
target_link_libraries(nextalk-server-core PUBLIC ccfw_server OpenSSL::SSL)
if(WIN32)
    target_link_libraries(nextalk-server-core PUBLIC ws2_32)
endif()
