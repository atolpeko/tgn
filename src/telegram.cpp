#include "telegram.hpp"

#include <limits>
#include <optional>
#include <cstddef>
#include <curl/curl.h>

namespace {

class CurlGlobal {
public:
    CurlGlobal() : ok_(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {}
    ~CurlGlobal() { if (ok_) curl_global_cleanup(); }

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

size_t discard_body(char*, size_t size, size_t nmemb, void*) {
    return size * nmemb;
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

tg::BotError classify(CURLcode rc, long http_code) noexcept {
    if (rc == CURLE_OPERATION_TIMEDOUT) {
        return tg::BotError::Timeout;
    }
    if (rc != CURLE_OK) {
        return tg::BotError::Transport;
    }

    switch (http_code) {
        case 200:
            return tg::BotError::None;
        case 400:
            return tg::BotError::BadRequest;
        case 401:
            return tg::BotError::Unauthorized;
        case 403:
            return tg::BotError::Forbidden;
        case 404:
            return tg::BotError::Unauthorized;
        case 429:
            return tg::BotError::RateLimited;
        default:
            return http_code >= 500 ? tg::BotError::ServerError
                                    : tg::BotError::Unknown;
    }
}

}  // namespace

namespace tg {

void tg::CurlDeleter::operator()(void* handle) const noexcept {
    curl_easy_cleanup(static_cast<CURL*>(handle));
}

std::ostream& operator<<(std::ostream& os, BotError e) {
    switch (e) {
        case BotError::None:
            return os << "none";
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

BotError BotClient::send(const std::string& message) {
    if (!curl_) {
        return BotError::Initialization;
    }

    const auto escaped_chat_id = form_escape(curl_.get(), chat_id_);
    const auto escaped_text = form_escape(curl_.get(), message);
    if (!escaped_chat_id || !escaped_text) {
        return BotError::Encoding;
    }

    const auto url = "https://api.telegram.org/bot" + token_ + "/sendMessage";
    const auto post_fields =
        "chat_id=" + *escaped_chat_id + "&text=" + *escaped_text;
    if (!configure(url, post_fields)) {
        return BotError::Initialization;
    }

    const CURLcode perform = curl_easy_perform(curl_.get());
    if (perform != CURLE_OK) {
        return classify(perform, 0);
    }

    auto http_code = 0L;
    if (curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code) !=
        CURLE_OK) {
        return BotError::Unknown;
    }
    return classify(CURLE_OK, http_code);
}

bool BotClient::configure(const std::string& url,
                          const std::string& body) {
    CURL* curl = curl_.get();
    return curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str()) ==
               CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                            static_cast<curl_off_t>(body.size())) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                            kConnectTimeoutSeconds) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTotalTimeoutSeconds) ==
               CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_body) ==
               CURLE_OK;
}

}  // namespace tg
