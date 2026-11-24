#include "telegram.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace tg {
namespace {

constexpr std::string_view kApiPrefix = "https://api.telegram.org/bot";
constexpr std::string_view kSendMessage = "/sendMessage";
constexpr std::size_t kMaxDescriptionChars = 300;

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void skip_space(std::string_view json, std::size_t& i) noexcept {
    while (i < json.size() && is_space(json[i])) {
        ++i;
    }
}

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point < 0x80U) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80U | ((code_point >> 12) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((code_point >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

std::uint32_t read_hex4(std::string_view json, std::size_t& i) noexcept {
    constexpr std::uint32_t kReplacement = 0xFFFDU;

    std::uint32_t value = 0;
    for (int digit = 0; digit < 4; ++digit, ++i) {
        if (i >= json.size()) {
            return kReplacement;
        }
        const int hex = hex_value(json[i]);
        if (hex < 0) {
            return kReplacement;
        }
        value = (value << 4) | static_cast<std::uint32_t>(hex);
    }
    return value;
}

std::string read_json_string(std::string_view json, std::size_t& i) {
    std::string out;
    if (i >= json.size() || json[i] != '"') {
        return out;
    }
    ++i;

    while (i < json.size()) {
        const char c = json[i];
        if (c == '"') {
            ++i;
            return out;
        }
        if (c != '\\') {
            out.push_back(c);
            ++i;
            continue;
        }

        ++i;  // consume the backslash
        if (i >= json.size()) {
            break;
        }
        const char escape = json[i];
        ++i;

        switch (escape) {
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                const std::uint32_t high = read_hex4(json, i);
                const bool is_high_surrogate =
                    high >= 0xD800U && high <= 0xDBFFU;
                if (is_high_surrogate && i + 6 <= json.size() &&
                    json[i] == '\\' && json[i + 1] == 'u') {
                    const std::size_t pair_start = i;
                    i += 2;
                    const std::uint32_t low = read_hex4(json, i);
                    if (low >= 0xDC00U && low <= 0xDFFFU) {
                        append_utf8(out, 0x10000U + ((high - 0xD800U) << 10) +
                                             (low - 0xDC00U));
                        break;
                    }
                    i = pair_start;  // not a surrogate pair after all
                }
                // Lone surrogates are not valid UTF-8; show U+FFFD instead.
                const bool is_surrogate = high >= 0xD800U && high <= 0xDFFFU;
                append_utf8(out, is_surrogate ? 0xFFFDU : high);
                break;
            }
            default:  // \" \\ \/ and anything unrecognised
                out.push_back(escape);
                break;
        }
    }

    return out;
}

long read_json_int(std::string_view json, std::size_t& i) noexcept {
    constexpr long kMaxValue = 100000000L;

    bool negative = false;
    if (i < json.size() && json[i] == '-') {
        negative = true;
        ++i;
    }

    long value = 0;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        if (value <= kMaxValue) {
            value = value * 10 + (json[i] - '0');
        }
        ++i;
    }
    if (value > kMaxValue) {
        value = kMaxValue;
    }
    return negative ? -value : value;
}

void parse_error_fields(std::string_view body, SendFailure& failure) {
    std::string pending_key;

    std::size_t i = 0;
    while (i < body.size()) {
        const char c = body[i];

        if (is_space(c)) {
            ++i;
            continue;
        }

        if (c == '"') {
            std::string text = read_json_string(body, i);
            std::size_t after = i;
            skip_space(body, after);
            if (after < body.size() && body[after] == ':') {
                pending_key = std::move(text);
                i = after + 1;
                continue;
            }
            if (pending_key == "description") {
                failure.description = std::move(text);
            }
            pending_key.clear();
            continue;
        }

        if (c == '-' || (c >= '0' && c <= '9')) {
            const long value = read_json_int(body, i);
            if (pending_key == "retry_after") {
                failure.retry_after = value;
            }
            pending_key.clear();
            continue;
        }

        // { } [ ] , and the literals true/false/null end a pending value.
        pending_key.clear();
        ++i;
    }

    if (failure.description.size() > kMaxDescriptionChars) {
        failure.description.resize(kMaxDescriptionChars);
        failure.description += "...";
    }
}

std::optional<tg::BotError> classify(long http_code) noexcept {
    switch (http_code) {
        case 200:
            return std::nullopt;
        case 400:
            return tg::BotError::BadRequest;
        case 401:
            return tg::BotError::Unauthorized;
        case 403:
            return tg::BotError::Forbidden;
        case 404:
            return tg::BotError::NotFound;
        case 429:
            return tg::BotError::RateLimited;
        default:
            return http_code >= 500 ? tg::BotError::ServerError
                                    : tg::BotError::Unknown;
    }
}

tg::BotError to_bot_error(http::Error error) noexcept {
    switch (error) {
        case http::Error::None:
            return tg::BotError::Unknown;  // unreachable: callers check ok()
        case http::Error::Initialization:
            return tg::BotError::Initialization;
        case http::Error::Encoding:
            return tg::BotError::Encoding;
        case http::Error::Transport:
            return tg::BotError::Transport;
        case http::Error::Timeout:
            return tg::BotError::Timeout;
        case http::Error::Unknown:
            return tg::BotError::Unknown;
    }
    return tg::BotError::Unknown;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, BotError e) {
    switch (e) {
        case BotError::Initialization:
            return os << "initialization error";
        case BotError::Encoding:
            return os << "encoding error";
        case BotError::Transport:
            return os << "transport error";
        case BotError::Timeout:
            return os << "timeout";
        case BotError::Unauthorized:
            return os << "unauthorized";
        case BotError::Forbidden:
            return os << "forbidden";
        case BotError::NotFound:
            return os << "not found";
        case BotError::RateLimited:
            return os << "rate limited";
        case BotError::BadRequest:
            return os << "bad request";
        case BotError::ServerError:
            return os << "server error";
        case BotError::Unknown:
            return os << "unknown error";
    }

    return os << "unknown(" << static_cast<int>(e) << ')';
}

BotClient::BotClient(std::string token, std::string chat_id)
    : token_(std::move(token)), chat_id_(std::move(chat_id)) {}

std::optional<SendFailure> BotClient::send(std::string_view message) {
    std::string url;
    url.reserve(kApiPrefix.size() + token_.size() + kSendMessage.size());
    url.append(kApiPrefix).append(token_).append(kSendMessage);

    auto result =
        http_.post_form(url, {{"chat_id", chat_id_}, {"text", message}});
    if (!result.ok()) {
        return SendFailure{to_bot_error(result.error),
                           std::move(result.detail)};
    }

    const auto error_status = classify(result.response.status);
    if (!error_status) {
        return std::nullopt;
    }

    SendFailure failure{*error_status, {}};
    parse_error_fields(result.response.body, failure);
    return failure;
}

}  // namespace tg
