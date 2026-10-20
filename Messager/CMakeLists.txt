cmake_minimum_required(VERSION 3.21)
project(Messager VERSION 0.3.16.1 LANGUAGES CXX)
set_source_files_properties(resources/app.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
add_library(messager_lib
    src/core/types.h
    src/services/chatservice.h
    src/services/cipherchatmapping.h src/services/cipherchatmapping.cpp
    src/services/mockservice.h src/services/mockservice.cpp
    src/services/loginpreferences.h src/services/loginpreferences.cpp
    src/services/localfiles.h src/services/localfiles.cpp
    src/services/gomokuwire.h src/services/gomokucontroller.h src/services/gomokucontroller.cpp
    src/controllers/appcontroller.h src/controllers/appcontroller.cpp
    src/models/conversationmodel.h src/models/conversationmodel.cpp
    src/ui/theme.h src/ui/theme.cpp
    src/ui/pencilskin.h src/ui/pencilskin.cpp
    src/ui/components.h src/ui/components.cpp
    src/ui/pagewidgets.h src/ui/pagewidgets.cpp
    src/ui/accountdialog.h src/ui/accountdialog.cpp
    src/ui/emailLoginPage.cpp
    src/ui/groupmemberpicker.h src/ui/groupmemberpicker.cpp
    src/ui/profilepage.h src/ui/profilepage.cpp
    src/ui/momentspage.h src/ui/momentspage.cpp
    src/ui/miniappspage.h src/ui/miniappspage.cpp
    src/ui/scientificcalculator.h src/ui/scientificcalculator.cpp
    src/ui/gomokupage.h src/ui/gomokupage.cpp
    src/ui/pages.cpp src/ui/logindialog.cpp src/ui/liveui.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp)
target_include_directories(messager_lib PUBLIC src)
target_link_libraries(messager_lib PUBLIC Qt6::Widgets)
if(WIN32)
    target_link_libraries(messager_lib PRIVATE crypt32 shell32 ole32)
endif()
if(MSVC)
    target_compile_options(messager_lib PUBLIC /utf-8)
endif()
qt_add_executable(Messager WIN32 src/main.cpp resources.qrc)
target_link_libraries(Messager PRIVATE messager_lib)
if(WIN32)
    target_sources(Messager PRIVATE resources/app.rc)
endif()
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(messager_tests tests/tst_messager.cpp resources.qrc)
    target_link_libraries(messager_tests PRIVATE messager_lib Qt6::Test)
    add_test(NAME messager_ui COMMAND messager_tests -platform offscreen -o "${CMAKE_BINARY_DIR}/ui-test-results.txt,txt")
    qt_add_executable(messager_miniapps_tests tests/tst_miniapps.cpp resources.qrc)
    target_link_libraries(messager_miniapps_tests PRIVATE messager_lib Qt6::Test)
    add_test(NAME messager_miniapps COMMAND messager_miniapps_tests -platform offscreen)
endif()
install(TARGETS Messager RUNTIME DESTINATION bin)

# The frontend remains C++17; only the optional native backend bridge uses C++20.
option(MESSAGER_WITH_CIPHERCHAT "Build the adapter against the existing native CipherChat runtime" ON)
set(CIPHERCHAT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cipherchat" CACHE PATH "Existing CipherChat source checkout")
if(MESSAGER_WITH_CIPHERCHAT)
    if(CMAKE_VERSION VERSION_LESS 3.24)
        message(FATAL_ERROR "CipherChat native runtime requires CMake 3.24 or newer")
    endif()
    function(messager_import_cipherchat)
        set(BUILD_TESTING OFF)
        set(CCFW_BUILD_GUI OFF)
        set(CCFW_BUILD_GUI_TESTS OFF)
        set(CCFW_BUILD_DELIVERY_TESTS OFF)
        add_subdirectory("${CIPHERCHAT_SOURCE_DIR}/framework" "${CMAKE_BINARY_DIR}/cipherchat" EXCLUDE_FROM_ALL)
    endfunction()
    messager_import_cipherchat()
    add_library(messager_cipherchat src/services/cipherchatgateway.h src/services/cipherchatgateway.cpp
        src/services/cipherchatservice.h src/services/cipherchatservice.cpp)
    set_target_properties(messager_cipherchat PROPERTIES CXX_STANDARD 20)
    target_include_directories(messager_cipherchat PUBLIC src)
    target_link_libraries(messager_cipherchat PRIVATE ccfw_rhine_json messager_lib PUBLIC Qt6::Core)
    target_link_libraries(Messager PRIVATE messager_cipherchat)
    target_compile_definitions(Messager PRIVATE MESSAGER_HAS_CIPHERCHAT=1)
    target_sources(Messager PRIVATE "${CIPHERCHAT_SOURCE_DIR}/framework/gui/update_controller.cpp"
        "${CIPHERCHAT_SOURCE_DIR}/framework/gui/update_controller.hpp")
    target_include_directories(Messager PRIVATE "${CIPHERCHAT_SOURCE_DIR}/framework")
    target_link_libraries(Messager PRIVATE ccfw_updates)
    if(WIN32)
        add_executable(messager-update-launcher WIN32 "${CIPHERCHAT_SOURCE_DIR}/framework/update/launcher.cpp")
        target_compile_definitions(messager-update-launcher PRIVATE CCFW_MESSAGER_LAUNCHER=1)
        target_link_libraries(messager-update-launcher PRIVATE shell32)
        target_sources(messager-update-launcher PRIVATE resources/app.rc)
        target_sources(cipherchat-updater PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc" TARGET_DIRECTORY cipherchat-updater PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
        set_target_properties(messager-update-launcher PROPERTIES CXX_STANDARD 20)
        if(MINGW)
            target_link_options(messager-update-launcher PRIVATE -municode -static -static-libgcc -static-libstdc++)
        endif()
    endif()
    if(BUILD_TESTING)
        qt_add_executable(cipherchat_adapter_tests tests/tst_cipherchat.cpp)
        target_link_libraries(cipherchat_adapter_tests PRIVATE messager_cipherchat messager_lib Qt6::Test)
        add_test(NAME cipherchat_adapter COMMAND cipherchat_adapter_tests -platform offscreen -o "${CMAKE_BINARY_DIR}/adapter-test-results.txt,txt")
        add_library(messager_test_server STATIC
            "${CIPHERCHAT_SOURCE_DIR}/server/storage/storage.cpp"
            "${CIPHERCHAT_SOURCE_DIR}/server/auth/auth.cpp"
            "${CIPHERCHAT_SOURCE_DIR}/server/router/router.cpp"
            "${CIPHERCHAT_SOURCE_DIR}/server/net/tls_server.cpp")
        set_target_properties(messager_test_server PROPERTIES CXX_STANDARD 20)
        target_link_libraries(messager_test_server PUBLIC ccfw_server ccfw_transport)
        qt_add_executable(messager_live_tests tests/tst_live.cpp resources.qrc)
        set_target_properties(messager_live_tests PROPERTIES CXX_STANDARD 20)
        target_link_libraries(messager_live_tests PRIVATE messager_cipherchat messager_lib messager_test_server Qt6::Test)
        add_test(NAME messager_live COMMAND messager_live_tests -platform offscreen -o "${CMAKE_BINARY_DIR}/live-test-results.txt,txt")
        set_tests_properties(messager_live PROPERTIES TIMEOUT 150)
        qt_add_executable(messager_update_tests tests/tst_updates.cpp resources.qrc
            "${CIPHERCHAT_SOURCE_DIR}/framework/gui/update_controller.cpp"
            "${CIPHERCHAT_SOURCE_DIR}/framework/gui/update_controller.hpp")
        set_target_properties(messager_update_tests PROPERTIES CXX_STANDARD 20)
        target_include_directories(messager_update_tests PRIVATE "${CIPHERCHAT_SOURCE_DIR}/framework")
        target_link_libraries(messager_update_tests PRIVATE messager_lib messager_test_server ccfw_updates Qt6::Test)
        target_compile_definitions(messager_update_tests PRIVATE UPDATER_PATH="$<TARGET_FILE:cipherchat-updater>")
        add_dependencies(messager_update_tests cipherchat-updater)
        add_test(NAME messager_updates COMMAND messager_update_tests -platform offscreen -o "${CMAKE_BINARY_DIR}/update-test-results.txt,txt")
        set_tests_properties(messager_updates PROPERTIES TIMEOUT 120)
        add_executable(cipherchat_update_regression "${CIPHERCHAT_SOURCE_DIR}/framework/tests/acceptance/test_M10_updates.cpp")
        set_target_properties(cipherchat_update_regression PROPERTIES CXX_STANDARD 20)
        target_link_libraries(cipherchat_update_regression PRIVATE messager_test_server ccfw_updates)
        add_test(NAME cipherchat_update_regression COMMAND cipherchat_update_regression)
        set_tests_properties(cipherchat_update_regression PROPERTIES TIMEOUT 120)
    endif()
endif()
option(MESSAGER_BUILD_DEMO_POPULATOR "Build the explicit operator tool for populating demo accounts" OFF)
if(MESSAGER_WITH_CIPHERCHAT AND MESSAGER_BUILD_DEMO_POPULATOR)
    add_executable(messager_demo_populator tools/demo-populator/main.cpp resources.qrc)
    target_link_libraries(messager_demo_populator PRIVATE messager_cipherchat messager_lib Qt6::Widgets)
endif()
