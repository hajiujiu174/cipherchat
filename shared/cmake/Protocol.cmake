include_guard(GLOBAL)

get_filename_component(nextalk_shared_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
add_subdirectory("${nextalk_shared_dir}/../frontend/runtime/chat_core" "${CMAKE_CURRENT_BINARY_DIR}/nextalk-chat-core")

add_library(ccfw_existing STATIC
    "${nextalk_shared_dir}/common/base64.cpp"
    "${nextalk_shared_dir}/common/crypto/crypto.cpp"
    "${nextalk_shared_dir}/common/protocol/frame.cpp"
    "${nextalk_shared_dir}/common/protocol/messages.cpp")
target_compile_features(ccfw_existing PUBLIC cxx_std_20)
target_include_directories(ccfw_existing PUBLIC "${nextalk_shared_dir}" "${nextalk_shared_dir}/third_party")
target_link_libraries(ccfw_existing PUBLIC OpenSSL::Crypto Threads::Threads nextalk_chat_core)
if(MSVC)
    target_compile_options(ccfw_existing PRIVATE /utf-8 /W4)
else()
    target_compile_options(ccfw_existing PRIVATE -Wall -Wextra)
endif()
