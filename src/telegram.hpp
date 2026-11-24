#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

#include "http.hpp"

namespace tg {

enum class BotError {
    Initialization,
    Encoding,
    Transport,
    Timeout,
    Unauthorized,
    Forbidden,
    NotFound,
    RateLimited,
    BadRequest,
    ServerError,
    Unknown
};

std::ostream& operator<<(std::ostream& os, BotError e);

struct SendFailure {
    BotError error;
    std::string description;
    long retry_after = 0;
};

class BotClient {
public:
    BotClient(std::string token, std::string chat_id);
    ~BotClient() = default;
    BotClient(const BotClient&) = delete;
    BotClient& operator=(const BotClient&) = delete;
    BotClient(BotClient&&) noexcept = default;
    BotClient& operator=(BotClient&&) noexcept = default;

    [[nodiscard]] std::optional<SendFailure> send(std::string_view message);

private:
    http::Client http_;
    std::string token_;
    std::string chat_id_;
};

}  // namespace tg
