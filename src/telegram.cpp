#include "telegram.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace tg {
namespace {

class CurlGlobal {
public:
    CurlGlobal() : ok_(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {}
    ~CurlGlobal() {
        if (ok_)
            curl_global_cleanup();
    }

    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;

    bool ok() const { return ok_; }

private:
    bool ok_;
};

CURL* curl_init() {
    static CurlGlobal guard;
    return guard.ok() ? curl_easy_init() : nullptr;
}

constexpr long kConnectTimeoutSeconds = 10;
constexpr long kTotalTimeoutSeconds = 30;
constexpr std::size_t kMaxResponseBytes = 8 * 1024;
constexpr std::size_t kMaxDescriptionChars = 300;

std::size_t collect_body(char* data, std::size_t size, std::size_t nmemb,
                         void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    const std::size_t bytes = size * nmemb;
    const std::size_t room = kMaxResponseBytes - body->size();
    body->append(data, std::min(bytes, room));
    return bytes;  
}

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

std::optional<std::string> form_escape(CURL* curl, const std::string& text) {
    if (text.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;  // libcurl takes an int length
    }

    char* escaped =
        curl_easy_escape(curl, text.c_str(), static_cast<int>(text.size()));
    if (escaped == nullptr) {
        return std::nullopt;
    }

    std::string encoded{escaped};
    curl_free(escaped);
    return encoded;
}

bool configure(CURL* curl, const std::string& url, const std::string& body,
               std::string* response, char* error_buffer) {
    return curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer) ==
               CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str()) ==
               CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                            static_cast<curl_off_t>(body.size())) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                            kConnectTimeoutSeconds) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTotalTimeoutSeconds) ==
               CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_body) ==
               CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_WRITEDATA, response) == CURLE_OK;
}

std::string detail_from_buffer(const char* error_buffer) {
    std::string detail = error_buffer;
    while (!detail.empty() &&
           (detail.back() == '\n' || detail.back() == '\r')) {
        detail.pop_back();
    }
    return detail;
}

std::optional<tg::BotError> classify(CURLcode rc, long http_code) noexcept {
    if (rc == CURLE_OPERATION_TIMEDOUT) {
        return tg::BotError::Timeout;
    }
    if (rc != CURLE_OK) {
        return tg::BotError::Transport;
    }

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

}  // namespace

void tg::CurlDeleter::operator()(void* handle) const noexcept {
    curl_easy_cleanup(static_cast<CURL*>(handle));
}

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
    : curl_(curl_init()),
      token_(std::move(token)),
      chat_id_(std::move(chat_id)) {}

std::optional<SendFailure> BotClient::send(const std::string& message) {
    if (!curl_) {
        return SendFailure{BotError::Initialization,
                           "the libcurl handle is unavailable"};
    }

    const auto escaped_chat_id = form_escape(curl_.get(), chat_id_);
    const auto escaped_text = form_escape(curl_.get(), message);
    if (!escaped_chat_id || !escaped_text) {
        return SendFailure{BotError::Encoding, "could not encode the request"};
    }

    const auto url = "https://api.telegram.org/bot" + token_ + "/sendMessage";
    const auto post_fields =
        "chat_id=" + *escaped_chat_id + "&text=" + *escaped_text;

    std::string response;
    char error_buffer[CURL_ERROR_SIZE] = {};
    if (!configure(curl_.get(), url, post_fields, &response, error_buffer)) {
        return SendFailure{BotError::Initialization,
                           "could not configure the request"};
    }

    const CURLcode perform = curl_easy_perform(curl_.get());
    if (perform != CURLE_OK) {
        std::string detail = detail_from_buffer(error_buffer);
        if (detail.empty()) {
            detail = curl_easy_strerror(perform);
        }
        return SendFailure{classify(perform, 0).value_or(BotError::Transport),
                           std::move(detail)};
    }

    auto http_code = 0L;
    if (curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code) !=
        CURLE_OK) {
        return SendFailure{BotError::Unknown,
                           "libcurl reported no HTTP status"};
    }

    const auto status = classify(CURLE_OK, http_code);
    if (!status) {
        return std::nullopt;  
    }

    SendFailure failure{*status, {}};
    parse_error_fields(response, failure);
    return failure;
}

}  // namespace tg
