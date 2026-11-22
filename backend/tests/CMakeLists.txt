# doctest 单元测试：协议帧、Base64、消息序列化、加密原语、X3DH 与双棘轮、服务器逻辑

add_executable(nextalk-tests
    test_main.cpp
    test_base64.cpp
    test_frame.cpp
    test_messages.cpp
    test_crypto.cpp
    test_ratchet.cpp
    test_registration_email.cpp
    test_smtp_branding.cpp
    test_profile_moments.cpp
    test_server.cpp
    test_integration.cpp)
target_link_libraries(nextalk-tests PRIVATE nextalk-common
                    nextalk-server-core nextalk-client-core ccfw_client_runtime)

add_test(NAME unit COMMAND nextalk-tests)
add_test(NAME registration-email COMMAND nextalk-tests "--test-case=Registration email*,SMTP failure*,Account email*,Resend invalidates*")
set_tests_properties(registration-email PROPERTIES TIMEOUT 15 LABELS "core;registration-email")

add_executable(nextalk-network-tests test_network_addresses.cpp)
target_link_libraries(nextalk-network-tests PRIVATE nextalk-server-core nextalk-client-core ccfw_client_runtime)
add_test(NAME network-addresses COMMAND nextalk-network-tests)
set_tests_properties(network-addresses PROPERTIES TIMEOUT 30)

add_test(NAME profile-moments COMMAND nextalk-tests "--test-case=Profile moments*")
set_tests_properties(profile-moments PROPERTIES TIMEOUT 30 LABELS "core;profile;moments")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME package-feed COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_update_feed_builder.py")
set_tests_properties(package-feed PROPERTIES TIMEOUT 30)
