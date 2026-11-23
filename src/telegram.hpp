#pragma once

#include <memory>
#include <ostream>
#include <string>

namespace tg {

// Telegram's limit for the text of one message, in UTF-16 code units.
inline constexpr std::size_t kMaxMessageLength = 4096;    

struct CurlDeleter { void operator()(void* handle) const noexcept; };

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

    [[nodiscard]] BotError send(const std::string& message);

private:
    bool configure(const std::string& url, const std::string& body);

    std::unique_ptr<void, CurlDeleter> curl_;  
    std::string token_;
    std::string chat_id_;
};

}  // namespace tg
