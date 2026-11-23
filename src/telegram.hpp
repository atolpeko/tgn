#pragma once

#include <curl/curl.h>

#include <memory>
#include <ostream>
#include <string>

namespace tg {

enum class BotError {
    None,
    Initialization,
    Encoding,
    Transport,
    Timeout,
    Unauthorized,
    Forbidden,
    RateLimited,
    BadRequest,
    ServerError,
    Unknown
};

std::ostream& operator<<(std::ostream& os, BotError e);

class BotClient {
public:
    BotClient(std::string token, std::string chat_id);
    ~BotClient() = default;
    BotClient(const BotClient&) = delete;
    BotClient& operator=(const BotClient&) = delete;
    BotClient(BotClient&&) noexcept = default;
    BotClient& operator=(BotClient&&) noexcept = default;

    [[nodiscard]] BotError send(const std::string& message) const;

private:
    bool configure(const std::string& url, const std::string& body) const;

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl_;
    std::string token_;
    std::string chat_id_;
};

}  // namespace tg
