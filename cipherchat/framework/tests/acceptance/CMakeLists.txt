# 业务验收测试：每个任务卡对应一个同名 CTest，独立可执行。
# M0-02 验证传输层并发修复（SSL 单线程拥有 + 出站队列 + WANT_READ 容忍），
# 复用原项目服务器与 NetClient 源码（单一来源，不复制算法）。

if(NOT CCFW_LINK_EXISTING)
    message(STATUS "M0-02 acceptance needs CCFW_LINK_EXISTING=ON (implementation preset); skipped.")
    return()
endif()

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

ccfw_test_binary(ccfw-M0-02 test_M0_02.cpp)
target_sources(ccfw-M0-02 PRIVATE
    "${CIPHERCHAT_ROOT}/common/net/tls_context.cpp"
    "${CIPHERCHAT_ROOT}/server/storage/storage.cpp"
    "${CIPHERCHAT_ROOT}/server/auth/auth.cpp"
    "${CIPHERCHAT_ROOT}/server/router/router.cpp"
    "${CIPHERCHAT_ROOT}/server/net/tls_server.cpp"
    "${CIPHERCHAT_ROOT}/client/core/net_client.cpp")
target_link_libraries(ccfw-M0-02 PRIVATE
    ccfw_existing ccfw_server
    OpenSSL::SSL OpenSSL::Crypto SQLite3::SQLite3)
if(WIN32)
    target_link_libraries(ccfw-M0-02 PRIVATE ws2_32)
endif()
ccfw_test(M0-02 ccfw-M0-02)
set_tests_properties(M0-02 PROPERTIES TIMEOUT 90 LABELS "acceptance;M0")

# M0-03：只链接 client-core + common（不含 server 翻译单元），证明登录路径
# 已迁移到公共层。
ccfw_test_binary(ccfw-M0-03 test_M0_03.cpp)
ccfw_test_binary(ccfw-ipv6-mtu test_ipv6_mtu.cpp)
target_link_libraries(ccfw-ipv6-mtu PRIVATE ccfw_existing)
if(WIN32)
    target_link_libraries(ccfw-ipv6-mtu PRIVATE ws2_32)
endif()
ccfw_test(M10-IPv6-MTU ccfw-ipv6-mtu)
set_tests_properties(M10-IPv6-MTU PROPERTIES TIMEOUT 30 LABELS "acceptance;M10;core;transport")
target_sources(ccfw-M0-03 PRIVATE
    "${CIPHERCHAT_ROOT}/common/net/tls_context.cpp"
    "${CIPHERCHAT_ROOT}/client/core/net_client.cpp"
    "${CIPHERCHAT_ROOT}/client/core/client.cpp"
    "${CIPHERCHAT_ROOT}/client/core/files.cpp")
target_link_libraries(ccfw-M0-03 PRIVATE ccfw_existing OpenSSL::SSL)
if(WIN32)
    target_link_libraries(ccfw-M0-03 PRIVATE ws2_32)
endif()
ccfw_test(M0-03 ccfw-M0-03)
set_tests_properties(M0-03 PROPERTIES TIMEOUT 15 LABELS "acceptance;M0")

# M1-01：加密原语冻结向量（RFC 5869/7748/8032/7914 + GCM 规范 TC3/TC4）。
ccfw_test_binary(ccfw-M1-01 test_M1_01.cpp)
target_link_libraries(ccfw-M1-01 PRIVATE ccfw_existing)
ccfw_test(M1-01 ccfw-M1-01)
set_tests_properties(M1-01 PROPERTIES TIMEOUT 15 LABELS "acceptance;M1")

# M1-02：棘轮解密认证原子性（候选副本解密、失败零状态变化）。
ccfw_test_binary(ccfw-M1-02 test_M1_02.cpp)
target_link_libraries(ccfw-M1-02 PRIVATE ccfw_existing)
ccfw_test(M1-02 ccfw-M1-02)
set_tests_properties(M1-02 PROPERTIES TIMEOUT 15 LABELS "acceptance;M1")

# M1-03：身份信任与会话建立（SessionService + 内存 ILocalStore 替身）。
ccfw_test_binary(ccfw-M1-03 test_M1_03.cpp)
target_link_libraries(ccfw-M1-03 PRIVATE ccfw_session ccfw_existing)
ccfw_test(M1-03 ccfw-M1-03)
set_tests_properties(M1-03 PROPERTIES TIMEOUT 15 LABELS "acceptance;M1")

# M2-01：TLS 服务身份验证（内存 CA + 最小监听替身）。
ccfw_test_binary(ccfw-M2-01 test_M2_01.cpp)
target_sources(ccfw-M2-01 PRIVATE
    "${CIPHERCHAT_ROOT}/common/net/tls_context.cpp"
    "${CIPHERCHAT_ROOT}/client/core/net_client.cpp")
target_link_libraries(ccfw-M2-01 PRIVATE ccfw_existing OpenSSL::SSL)
if(WIN32)
    target_link_libraries(ccfw-M2-01 PRIVATE ws2_32)
endif()
ccfw_test(M2-01 ccfw-M2-01)
set_tests_properties(M2-01 PROPERTIES TIMEOUT 60 LABELS "acceptance;M2")

# M2-02: login challenge lifecycle through the real server router and storage.
ccfw_test_binary(ccfw-M2-02 test_M2_02.cpp)
target_sources(ccfw-M2-02 PRIVATE
    "${CIPHERCHAT_ROOT}/server/storage/storage.cpp"
    "${CIPHERCHAT_ROOT}/server/auth/auth.cpp"
    "${CIPHERCHAT_ROOT}/server/router/router.cpp")
target_link_libraries(ccfw-M2-02 PRIVATE ccfw_server ccfw_existing SQLite3::SQLite3)
ccfw_test(M2-02 ccfw-M2-02)
set_tests_properties(M2-02 PROPERTIES TIMEOUT 15 LABELS "acceptance;M2")

ccfw_test_binary(ccfw-M2-03 test_M2_03.cpp
    "${CIPHERCHAT_ROOT}/server/storage/storage.cpp"
    "${CIPHERCHAT_ROOT}/server/auth/auth.cpp"
    "${CIPHERCHAT_ROOT}/server/router/router.cpp")
target_link_libraries(ccfw-M2-03 PRIVATE ccfw_server ccfw_existing SQLite3::SQLite3)
ccfw_test(M2-03 ccfw-M2-03)
set_tests_properties(M2-03 PROPERTIES TIMEOUT 30 LABELS "acceptance;M2")

ccfw_test_binary(ccfw-M3-04 test_M3_04.cpp)
target_link_libraries(ccfw-M3-04 PRIVATE ccfw_storage)
ccfw_test(M3-04 ccfw-M3-04)
set_tests_properties(M3-04 PROPERTIES TIMEOUT 15 LABELS "acceptance;M3")

ccfw_test_binary(ccfw-M3-03 test_M3_03.cpp)
target_link_libraries(ccfw-M3-03 PRIVATE ccfw_storage)
ccfw_test(M3-03 ccfw-M3-03)
set_tests_properties(M3-03 PROPERTIES TIMEOUT 20 LABELS "acceptance;M3")

ccfw_test_binary(ccfw-M3-01 test_M3_01.cpp)
target_link_libraries(ccfw-M3-01 PRIVATE ccfw_transport)
ccfw_test(M3-01 ccfw-M3-01)
set_tests_properties(M3-01 PROPERTIES TIMEOUT 30 LABELS "acceptance;M3")

ccfw_test_binary(ccfw-M3-02 test_M3_02.cpp
    "${CIPHERCHAT_ROOT}/server/storage/storage.cpp"
    "${CIPHERCHAT_ROOT}/server/auth/auth.cpp"
    "${CIPHERCHAT_ROOT}/server/router/router.cpp")
target_link_libraries(ccfw-M3-02 PRIVATE ccfw_account ccfw_server)
ccfw_test(M3-02 ccfw-M3-02)
set_tests_properties(M3-02 PROPERTIES TIMEOUT 30 LABELS "acceptance;M3")

add_library(ccfw_test_server STATIC
    "${CIPHERCHAT_ROOT}/server/storage/storage.cpp"
    "${CIPHERCHAT_ROOT}/server/auth/auth.cpp"
    "${CIPHERCHAT_ROOT}/server/router/router.cpp"
    "${CIPHERCHAT_ROOT}/server/net/tls_server.cpp")
target_link_libraries(ccfw_test_server PUBLIC ccfw_server ccfw_transport)
ccfw_test_binary(ccfw-M4-01 test_M4_01.cpp)
target_link_libraries(ccfw-M4-01 PRIVATE ccfw_application ccfw_test_server)
ccfw_test(M4-01 ccfw-M4-01)
set_tests_properties(M4-01 PROPERTIES TIMEOUT 90 LABELS "acceptance;M4")

ccfw_test_binary(ccfw-M4-02 test_M4_02.cpp)
target_link_libraries(ccfw-M4-02 PRIVATE ccfw_application ccfw_test_server)
ccfw_test(M4-02 ccfw-M4-02)
set_tests_properties(M4-02 PROPERTIES TIMEOUT 90 LABELS "acceptance;M4")

ccfw_test_binary(ccfw-process-client process_client.cpp)
target_link_libraries(ccfw-process-client PRIVATE ccfw_application)
ccfw_test_binary(ccfw-M4-03 test_M4_03.cpp)
target_link_libraries(ccfw-M4-03 PRIVATE ccfw_application ccfw_test_server)
target_compile_definitions(ccfw-M4-03 PRIVATE CCFW_PROCESS_CLIENT_PATH="$<TARGET_FILE:ccfw-process-client>")
add_dependencies(ccfw-M4-03 ccfw-process-client)
ccfw_test(M4-03 ccfw-M4-03)
set_tests_properties(M4-03 PROPERTIES TIMEOUT 90 LABELS "acceptance;M4")

ccfw_test_binary(ccfw-M3-05 test_M3_05.cpp)
target_link_libraries(ccfw-M3-05 PRIVATE ccfw_application ccfw_test_server)
ccfw_test(M3-05 ccfw-M3-05)
set_tests_properties(M3-05 PROPERTIES TIMEOUT 30 LABELS "acceptance;M3")

ccfw_test_binary(ccfw-M5-02 test_M5_02.cpp)
target_link_libraries(ccfw-M5-02 PRIVATE ccfw_application ccfw_test_server)
ccfw_test(M5-02 ccfw-M5-02)
set_tests_properties(M5-02 PROPERTIES TIMEOUT 30 LABELS "acceptance;M5")

ccfw_test_binary(ccfw-M5-01 test_M5_01.cpp)
target_link_libraries(ccfw-M5-01 PRIVATE ccfw_application ccfw_test_server)
ccfw_test(M5-01 ccfw-M5-01)
set_tests_properties(M5-01 PROPERTIES TIMEOUT 45 LABELS "acceptance;M5")

ccfw_test_binary(ccfw-M5-03 test_M5_03.cpp)
target_link_libraries(ccfw-M5-03 PRIVATE ccfw_application ccfw_test_server)
target_compile_definitions(ccfw-M5-03 PRIVATE CCFW_PROCESS_CLIENT_PATH="$<TARGET_FILE:ccfw-process-client>")
add_dependencies(ccfw-M5-03 ccfw-process-client)
ccfw_test(M5-03 ccfw-M5-03)
set_tests_properties(M5-03 PROPERTIES TIMEOUT 60 LABELS "acceptance;M5")

ccfw_test_binary(ccfw-M7-02 test_M7_02_runtime.cpp)
target_link_libraries(ccfw-M7-02 PRIVATE ccfw_client_runtime ccfw_test_server)
ccfw_test(M7-02 ccfw-M7-02)
set_tests_properties(M7-02 PROPERTIES TIMEOUT 60 LABELS "acceptance;M7;migration")

ccfw_test_binary(ccfw-M9-01 test_M9_01.cpp)
target_link_libraries(ccfw-M9-01 PRIVATE ccfw_updates ccfw_application ccfw_test_server)
ccfw_test(M9-01 ccfw-M9-01)
set_tests_properties(M9-01 PROPERTIES TIMEOUT 60 LABELS "acceptance;M9;updates;core")
ccfw_test_binary(ccfw-M10-updates test_M10_updates.cpp)
target_link_libraries(ccfw-M10-updates PRIVATE ccfw_updates ccfw_application ccfw_test_server)
ccfw_test(M10-updates ccfw-M10-updates)
set_tests_properties(M10-updates PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "acceptance;M10;updates;core")
ccfw_test_binary(ccfw-M9-02 test_M9_02.cpp)
target_link_libraries(ccfw-M9-02 PRIVATE ccfw_application ccfw_test_server)
ccfw_test(M9-02 ccfw-M9-02)
set_tests_properties(M9-02 PROPERTIES TIMEOUT 30 LABELS "acceptance;M9;contacts;core")

# Keep Qt runtime ordering and test registration in one place.
function(ccfw_gui_test name target timeout labels)
    ccfw_test(${name} ${target} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout} LABELS "${labels}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:D:/msys64/mingw64/bin;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/compiler-runtime")
endfunction()

function(ccfw_scaled_gui_tests prefix target timeout labels)
    foreach(scale IN ITEMS 1 1.25 1.5)
        ccfw_gui_test(${prefix}-${scale} ${target} ${timeout} "${labels}" ${ARGN})
        set_tests_properties(${prefix}-${scale} PROPERTIES RUN_SERIAL TRUE
            ENVIRONMENT "QT_SCALE_FACTOR=${scale};QT_SCREEN_SCALE_FACTORS=1;QT_FONT_DPI=96")
    endforeach()
endfunction()

if(CCFW_BUILD_GUI_TESTS)
    set(CCFW_UI_EVIDENCE_ROOT "${CMAKE_BINARY_DIR}/ui-evidence" CACHE PATH "Output directory for GUI test evidence")
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Test)
    if(WIN32)
        ccfw_test_binary(ccfw-ui-updates ../ui/test_updates.cpp)
        target_link_libraries(ccfw-ui-updates PRIVATE ccfw_gui ccfw_test_server Qt6::Test)
        target_compile_definitions(ccfw-ui-updates PRIVATE CCFW_UPDATER_PATH="$<TARGET_FILE:cipherchat-updater>")
        add_dependencies(ccfw-ui-updates cipherchat-updater)
        ccfw_gui_test(UI-updates ccfw-ui-updates 35 "gui;ui;updates")
    endif()
    add_library(ccfw_qt_test_adapter STATIC ../ui/qt_client_page.cpp)
    target_link_libraries(ccfw_qt_test_adapter PUBLIC ccfw_gui Qt6::Test)
    ccfw_test_binary(ccfw-ui-navigation ../ui/test_navigation.cpp)
    ccfw_test_binary(ccfw-ui-designs ../ui/test_designs.cpp)
    ccfw_test_binary(ccfw-ui-colors ../ui/test_colors.cpp)
    ccfw_test_binary(ccfw-ui-appearance ../ui/test_appearance.cpp)
    target_link_libraries(ccfw-ui-appearance PRIVATE ccfw_gui Qt6::Test)
    ccfw_gui_test(UI-appearance ccfw-ui-appearance 30 "gui;ui;layout")
    ccfw_test_binary(ccfw-ui-settings-palette ../ui/test_settings_palette.cpp)
    target_link_libraries(ccfw-ui-settings-palette PRIVATE ccfw_gui Qt6::Test)
    ccfw_gui_test(UI-settings-palette ccfw-ui-settings-palette 60 "gui;ui;settings;theme")
    set_tests_properties(UI-settings-palette PROPERTIES RUN_SERIAL TRUE
        ENVIRONMENT "QT_SCALE_FACTOR=1.5;QT_SCREEN_SCALE_FACTORS=1;QT_FONT_DPI=96")
    target_link_libraries(ccfw-ui-colors PRIVATE ccfw_gui Qt6::Test)
    ccfw_gui_test(UI-theme-colors ccfw-ui-colors 30 "gui;ui")
    target_link_libraries(ccfw-ui-designs PRIVATE ccfw_gui Qt6::Test)
    ccfw_gui_test(UI-design-switching ccfw-ui-designs 20 "gui;ui;layout")
    target_link_libraries(ccfw-ui-navigation PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    ccfw_gui_test(UI-adapter-navigation ccfw-ui-navigation 15 "gui;ui")
    ccfw_test_binary(ccfw-gui-driver gui_driver.cpp)
    target_link_libraries(ccfw-gui-driver PRIVATE ccfw_qt_test_adapter)
    ccfw_test_binary(ccfw-M4-04 test_M4_04.cpp)
    target_link_libraries(ccfw-M4-04 PRIVATE ccfw_application ccfw_test_server)
    target_compile_definitions(ccfw-M4-04 PRIVATE CCFW_PROCESS_CLIENT_PATH="$<TARGET_FILE:ccfw-gui-driver>")
    add_dependencies(ccfw-M4-04 ccfw-gui-driver)
    ccfw_gui_test(M4-04 ccfw-M4-04 75 "acceptance;M4;gui;ui")
    ccfw_gui_test(M4-04-layout ccfw-M4-04 75 "acceptance;M4;gui;layout")
    set_tests_properties(M4-04-layout PROPERTIES ENVIRONMENT "CCFW_GUI_LAYOUT_CHECKS=1")
    ccfw_test_binary(ccfw-M7-02-gui test_M7_02.cpp)
    target_link_libraries(ccfw-M7-02-gui PRIVATE ccfw_application ccfw_test_server)
    target_compile_definitions(ccfw-M7-02-gui PRIVATE CCFW_PROCESS_CLIENT_PATH="$<TARGET_FILE:ccfw-gui-driver>")
    add_dependencies(ccfw-M7-02-gui ccfw-gui-driver)
    ccfw_gui_test(M7-02-gui ccfw-M7-02-gui 60 "acceptance;M7;gui;ui;migration")
endif()

if(CCFW_BUILD_DELIVERY_TESTS AND WIN32)
    ccfw_test_binary(ccfw-M6-01 test_M6_01.cpp)
    target_link_libraries(ccfw-M6-01 PRIVATE ccfw_application ccfw_test_server user32)
    target_compile_definitions(ccfw-M6-01 PRIVATE CCFW_DELIVERY_ROOT="${CIPHERCHAT_ROOT}")
    ccfw_test(M6-01 ccfw-M6-01)
    set_tests_properties(M6-01 PROPERTIES TIMEOUT 60 LABELS "acceptance;M6;delivery")
    find_program(CCFW_POWERSHELL NAMES pwsh powershell REQUIRED)
    add_test(NAME M9-03 COMMAND "${CCFW_POWERSHELL}" -NoProfile -File "${CIPHERCHAT_ROOT}/scripts/test-installer.ps1")
    set_tests_properties(M9-03 PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE
        WORKING_DIRECTORY "${CIPHERCHAT_ROOT}" LABELS "acceptance;M9;delivery;installer")
endif()

ccfw_test_binary(ccfw-M7-01 test_M7_01.cpp)
ccfw_test_binary(ccfw-file-cache test_file_cache.cpp)
target_link_libraries(ccfw-file-cache PRIVATE ccfw_application ccfw_test_server)
ccfw_test(File-cache ccfw-file-cache)
set_tests_properties(File-cache PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE LABELS "acceptance;M10;files;cache;core")
target_link_libraries(ccfw-M7-01 PRIVATE ccfw_application ccfw_test_server)
ccfw_test(M7-01 ccfw-M7-01)
set_tests_properties(M7-01 PROPERTIES TIMEOUT 60 LABELS "acceptance;M7;files")

ccfw_test_binary(ccfw-M8-01 test_M8_01.cpp)
target_link_libraries(ccfw-M8-01 PRIVATE ccfw_files ccfw_chat)
ccfw_test(M8-01 ccfw-M8-01)
set_tests_properties(M8-01 PROPERTIES TIMEOUT 60 LABELS "acceptance;M8;files;p2p")

if(CCFW_BUILD_GUI_TESTS)
    ccfw_test_binary(ccfw-ui-contacts ../ui/test_contacts.cpp)
    if(CCFW_BUILD_RHINE_NATIVE)
        ccfw_test_binary(ccfw-ui-hot-switch ../ui/test_hot_switch.cpp)
        target_link_libraries(ccfw-ui-hot-switch PRIVATE ccfw_qt_test_adapter ccfw_test_server)
        ccfw_gui_test(UI-hot-switch ccfw-ui-hot-switch 95 "gui;ui;hot-switch")
    endif()
    target_link_libraries(ccfw-ui-contacts PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    ccfw_gui_test(UI-contacts ccfw-ui-contacts 45 "acceptance;M9;gui;ui;layout")
    ccfw_test_binary(ccfw-M8-preferences test_M8_preferences.cpp)
    target_link_libraries(ccfw-M8-preferences PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    ccfw_gui_test(M8-preferences ccfw-M8-preferences 20 "acceptance;M8;gui;ui")
endif()

# Every non-presentation acceptance test belongs to the headless business suite.
ccfw_test_binary(ccfw-uid test_uid.cpp)
target_link_libraries(ccfw-uid PRIVATE ccfw_application ccfw_test_server)
ccfw_test(UID-core ccfw-uid)
set_tests_properties(UID-core PROPERTIES TIMEOUT 60 LABELS "acceptance;M10;core;uid")
ccfw_test_binary(ccfw-M10-features test_M10_features.cpp)
target_link_libraries(ccfw-M10-features PRIVATE ccfw_delivery ccfw_application ccfw_test_server)
if(WIN32)
    add_subdirectory(../compatibility compatibility)
endif()
ccfw_test(M10-features ccfw-M10-features)
set_tests_properties(M10-features PROPERTIES TIMEOUT 90 LABELS "acceptance;M10;core;features")
if(CCFW_BUILD_GUI_TESTS)
    ccfw_test_binary(ccfw-ui-contact-sections ../ui/test_contact_sections.cpp)
    ccfw_test_binary(ccfw-ui-pencil-skin ../ui/test_pencil_skin.cpp)
    target_link_libraries(ccfw-ui-pencil-skin PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    target_compile_definitions(ccfw-ui-pencil-skin PRIVATE CCFW_PENCIL_TEST_ROOT="${CCFW_UI_EVIDENCE_ROOT}/pencil-skin")
    ccfw_scaled_gui_tests(UI-pencil-skin ccfw-ui-pencil-skin 180 "gui;ui;skin;pencil;codex")
    target_link_libraries(ccfw-ui-contact-sections PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    target_compile_definitions(ccfw-ui-contact-sections PRIVATE CCFW_CONTACT_SECTIONS_ROOT="${CCFW_UI_EVIDENCE_ROOT}/contact-sections")
    ccfw_scaled_gui_tests(UI-contact-sections ccfw-ui-contact-sections 180 "gui;ui;contacts;codex")
    ccfw_test_binary(ccfw-ui-file-queue ../ui/test_file_queue.cpp)
    target_link_libraries(ccfw-ui-file-queue PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    target_compile_definitions(ccfw-ui-file-queue PRIVATE CCFW_FILE_TEST_ROOT="${CCFW_UI_EVIDENCE_ROOT}/file-queue")
    ccfw_scaled_gui_tests(UI-file-queue ccfw-ui-file-queue 180 "gui;ui;files;codex")
    ccfw_test_binary(ccfw-ui-uid-notes ../ui/test_uid_notes.cpp)
    target_link_libraries(ccfw-ui-uid-notes PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    target_compile_definitions(ccfw-ui-uid-notes PRIVATE CCFW_UID_TEST_ROOT="${CCFW_UI_EVIDENCE_ROOT}/uid-notes")
    ccfw_scaled_gui_tests(UI-uid-notes ccfw-ui-uid-notes 180 "gui;ui;uid;notes;codex")
    ccfw_test_binary(ccfw-ui-qq-workspace ../ui/test_qq_workspace.cpp)
    target_link_libraries(ccfw-ui-qq-workspace PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    target_compile_definitions(ccfw-ui-qq-workspace PRIVATE CCFW_QQ_TEST_ROOT="${CMAKE_BINARY_DIR}/qq")
    ccfw_scaled_gui_tests(UI-qq-workspace ccfw-ui-qq-workspace 180 "gui;ui;codex;usability")
    ccfw_test_binary(ccfw-ui-login-preferences ../ui/test_login_preferences.cpp)
    target_link_libraries(ccfw-ui-login-preferences PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    ccfw_scaled_gui_tests(UI-codex-login ccfw-ui-login-preferences 90 "gui;ui;login;codex")
endif()
if(CCFW_BUILD_GUI_TESTS AND CCFW_BUILD_RHINE_NATIVE)
    ccfw_test_binary(ccfw-rhine-usability ../ui/test_rhine_usability.cpp)
    target_link_libraries(ccfw-rhine-usability PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    ccfw_gui_test(UI-rhine-usability ccfw-rhine-usability 180 "gui;ui;layout;usability" "--test-case-exclude=*guest login*")
    set_tests_properties(UI-rhine-usability PROPERTIES RUN_SERIAL TRUE)
    ccfw_scaled_gui_tests(UI-rhine-login ccfw-rhine-usability 60 "gui;ui;layout;login" "--test-case=*guest login*")
    ccfw_test_binary(ccfw-M10-workspace ../ui/test_M10_workspace.cpp)
    target_link_libraries(ccfw-M10-workspace PRIVATE ccfw_qt_test_adapter ccfw_test_server)
    target_compile_definitions(ccfw-M10-workspace PRIVATE CCFW_M10_BASELINE_QML="${CMAKE_CURRENT_SOURCE_DIR}/../compatibility/baseline-20261007/ui/Main.qml")
    ccfw_scaled_gui_tests(M10-workspace ccfw-M10-workspace 100 "acceptance;M10;gui;ui;layout;features")
endif()

get_property(acceptance_tests DIRECTORY PROPERTY TESTS)
foreach(test_name IN LISTS acceptance_tests)
    get_property(test_labels TEST ${test_name} PROPERTY LABELS)
    if(NOT "gui" IN_LIST test_labels AND NOT "delivery" IN_LIST test_labels)
        set_property(TEST ${test_name} APPEND PROPERTY LABELS core)
    endif()
endforeach()

