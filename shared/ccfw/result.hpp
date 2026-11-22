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
    // 构造带成功值的 Result，供调用者区分成功和错误。
    static Result success(T value) { return Result(std::move(value)); }
    // 返回操作结果使用的失败结果。
    static Result failure(Error error) { return Result(std::move(error)); }
    // 返回当前操作是否成功。
    bool ok() const noexcept { return std::holds_alternative<T>(data_); }
    // 允许在条件判断中直接检查结果是否成功。
    explicit operator bool() const noexcept { return ok(); }
    // 返回成功结果中保存的值。
    T& value() { return std::get<T>(data_); }
    // 返回成功结果中保存的值。
    const T& value() const { return std::get<T>(data_); }
    // 返回操作结果的失败时的错误信息。
    const Error& error() const { return std::get<Error>(data_); }
private:
    // 构造操作结果，初始化后续操作需要的状态或依赖。
    explicit Result(T value) : data_(std::move(value)) {}
    // 构造操作结果，初始化后续操作需要的状态或依赖。
    explicit Result(Error error) : data_(std::move(error)) {}
    std::variant<T, Error> data_;
};
// 判断result 模块的是否仍待处理。
template<class T = Unit>
Result<T> pending(const char* task) {
    return Result<T>::failure({ErrorCode::NotImplemented, task, "功能尚未实现"});
}
} // namespace ccfw
