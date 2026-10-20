# doctest 单元测试：协议帧、Base64、消息序列化、加密原语、X3DH 与双棘轮、服务器逻辑

add_executable(cipherchat-tests
    test_main.cpp
    test_base64.cpp
    test_frame.cpp
    test_messages.cpp
    test_crypto.cpp
    test_ratchet.cpp
    test_registration_email.cpp
    test_profile_moments.cpp
    test_server.cpp
    test_integration.cpp)
target_link_libraries(cipherchat-tests PRIVATE cipherchat-common
                    cipherchat-server-core cipherchat-client-core)

add_test(NAME unit COMMAND cipherchat-tests)
add_test(NAME registration-email COMMAND cipherchat-tests "--test-case=Registration email*,SMTP failure*,Account email*,Resend invalidates*")
set_tests_properties(registration-email PROPERTIES TIMEOUT 15 LABELS "core;registration-email")

add_executable(cipherchat-network-tests test_network_addresses.cpp)
target_link_libraries(cipherchat-network-tests PRIVATE cipherchat-server-core cipherchat-client-core)
add_test(NAME network-addresses COMMAND cipherchat-network-tests)
set_tests_properties(network-addresses PROPERTIES TIMEOUT 30)

add_test(NAME profile-moments COMMAND cipherchat-tests "--test-case=Profile moments*")
set_tests_properties(profile-moments PROPERTIES TIMEOUT 30 LABELS "core;profile;moments")
