#pragma once
#include <string>
#include <utility>
#include <variant>

namespace ccfw {
enum class ErrorCode {
    NotImplemented, InvalidArgument, InvalidState, NotConnected,
    AuthenticationFailed, IdentityChanged, CryptoFailure, StorageFailure,
    Conflict, Timeout, Cancelled, ProtocolError, Forbidden, NotFound, AlreadyProcessed
};
struct Error {
    ErrorCode code;
    std::string task;    // 例如 M1-03；实现后业务错误仍保留来源任务
    std::string message; // 不得包含口令、私钥、明文正文
};
struct Unit {};
template<class T = Unit> class Result {
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(Error error) { return Result(std::move(error)); }
    bool ok() const noexcept { return std::holds_alternative<T>(data_); }
    explicit operator bool() const noexcept { return ok(); }
    T& value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }
    const Error& error() const { return std::get<Error>(data_); }
private:
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(Error error) : data_(std::move(error)) {}
    std::variant<T, Error> data_;
};
template<class T = Unit>
Result<T> pending(const char* task) {
    return Result<T>::failure({ErrorCode::NotImplemented, task, "功能尚未实现"});
}
} // namespace ccfw
