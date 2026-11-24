#pragma once

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace http {

struct CurlHandle;

struct CurlDeleter {
    void operator()(CurlHandle* handle) const noexcept;
};

enum class Error {
    None,
    Initialization,
    Encoding,
    Transport,
    Timeout,
    Unknown
};

using FormField = std::pair<std::string_view, std::string_view>;

struct Config {
    long connect_timeout_seconds = 10;
    long total_timeout_seconds = 30;

    // Bodies larger than this are truncated, not failed.
    std::size_t max_response_bytes = 8 * 1024;
};

struct Response {
    long status = 0;
    std::string body;
};

struct Result {
    Error error = Error::None;
    Response response;   // when error == Error::None
    std::string detail;  // when error != Error::None

    [[nodiscard]] bool ok() const noexcept { return error == Error::None; }
};

class Client {
public:
    explicit Client(Config config = {});
    ~Client() = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept = default;
    Client& operator=(Client&&) noexcept = default;

    [[nodiscard]] Result post(const std::string& url, std::string_view body);

    [[nodiscard]] Result post_form(const std::string& url,
                                   std::initializer_list<FormField> fields);

private:
    std::unique_ptr<CurlHandle, CurlDeleter> curl_;
    Config config_;
};

}  // namespace http
