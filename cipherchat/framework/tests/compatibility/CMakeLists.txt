set(legacy_root "${CMAKE_CURRENT_SOURCE_DIR}/baseline-20261007")
ccfw_test_binary(ccfw-M10-legacy-probe legacy_probe.cpp)
target_include_directories(ccfw-M10-legacy-probe BEFORE PRIVATE "${legacy_root}/include" "${legacy_root}/src")
target_sources(ccfw-M10-legacy-probe PRIVATE
    "${legacy_root}/src/account_service.cpp" "${legacy_root}/src/application.cpp"
    "${legacy_root}/src/chat_service.cpp" "${legacy_root}/src/session_service.cpp"
    "${legacy_root}/src/file_transfer.cpp" "${legacy_root}/src/direct_file_channel.cpp"
    "${legacy_root}/src/group_service.cpp" "${legacy_root}/src/local_store.cpp"
    "${legacy_root}/src/secret_protector.cpp" "${legacy_root}/src/transport.cpp"
    "${CIPHERCHAT_ROOT}/client/core/net_client.cpp" "${CIPHERCHAT_ROOT}/common/net/tls_context.cpp")
target_link_libraries(ccfw-M10-legacy-probe PRIVATE ccfw_existing OpenSSL::SSL SQLite3::SQLite3 crypt32 ws2_32)
target_compile_definitions(ccfw-M10-features PRIVATE CCFW_FEATURE_LEGACY_PATH="$<TARGET_FILE:ccfw-M10-legacy-probe>")
add_dependencies(ccfw-M10-features ccfw-M10-legacy-probe)
