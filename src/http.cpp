#include "http.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace http {
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

CurlHandle* curl_init() {
    static CurlGlobal guard;
    return guard.ok() ? static_cast<CurlHandle*>(curl_easy_init()) : nullptr;
}

CURL* raw(CurlHandle* handle) noexcept {
    return reinterpret_cast<CURL*>(handle);
}

struct Sink {
    std::string* body;
    std::size_t max_bytes;
};

std::size_t collect_body(char* data, std::size_t size, std::size_t nmemb,
                         void* userdata) {
    auto* sink = static_cast<Sink*>(userdata);
    const std::size_t bytes = size * nmemb;
    const std::size_t room = sink->max_bytes - sink->body->size();
    sink->body->append(data, std::min(bytes, room));
    return bytes;
}

// CURLOPT_POSTFIELDS keeps the pointer, so `body` must outlive
// curl_easy_perform; CURLOPT_COPYPOSTFIELDS would hide that requirement by
// copying the whole body.
bool configure(CURL* curl, const std::string& url, std::string_view body,
               Sink* sink, char* error_buffer, const Config& config) {
    // CURLOPT_POSTFIELDS must not be NULL, even for an empty body.
    const char* const payload = body.empty() ? "" : body.data();
    return curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer) ==
               CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                            static_cast<curl_off_t>(body.size())) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                            config.connect_timeout_seconds) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_TIMEOUT,
                            config.total_timeout_seconds) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_body) ==
               CURLE_OK &&
           curl_easy_setopt(curl, CURLOPT_WRITEDATA, sink) == CURLE_OK;
}

std::string detail_from_buffer(const char* error_buffer) {
    std::string detail = error_buffer;
    while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r'))
        detail.pop_back();

    return detail;
}

Error error_for(CURLcode rc) noexcept {
    return rc == CURLE_OPERATION_TIMEDOUT ? Error::Timeout : Error::Transport;
}

struct CurlFree {
    void operator()(char* data) const noexcept { curl_free(data); }
};

// Owns the percent-encoded string libcurl allocated; nothing is copied.
struct Escaped {
    std::unique_ptr<char, CurlFree> data;
    std::size_t size = 0;

    [[nodiscard]] std::string_view view() const noexcept {
        return {data.get(), size};
    }
};

std::optional<Escaped> form_escape(CURL* curl, std::string_view text) {
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::nullopt;  // libcurl takes an int length

    char* escaped = curl_easy_escape(curl, text.empty() ? "" : text.data(),
                                     static_cast<int>(text.size()));
    if (escaped == nullptr)
        return std::nullopt;

    return Escaped{std::unique_ptr<char, CurlFree>{escaped},
                   std::strlen(escaped)};
}

bool is_unreserved(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
           c == '~';
}

// The length of `text` once curl_easy_escape has encoded it.
std::size_t encoded_length(std::string_view text) noexcept {
    std::size_t total = 0;
    for (const char c : text)
        total += is_unreserved(c) ? std::size_t{1} : std::size_t{3};
    return total;
}

// True when percent-encoding would change `text`; stops at the first such byte.
bool needs_escape(std::string_view text) noexcept {
    return std::any_of(text.begin(), text.end(),
                       [](char c) { return !is_unreserved(c); });
}

// Appends one form component, escaping it only when that would change it.
bool append_form_value(CURL* curl, std::string& out, std::string_view text) {
    if (!needs_escape(text)) {
        out.append(text);
        return true;
    }

    const auto escaped = form_escape(curl, text);
    if (!escaped)
        return false;

    out.append(escaped->view());
    return true;
}

// Builds "name=value&name=value", percent-encoded, in a single allocation.
std::optional<std::string> form_body(CURL* curl,
                                     std::initializer_list<FormField> fields) {
    std::size_t size = 0;
    for (const auto& field : fields)
        size += encoded_length(field.first) + 1 + encoded_length(field.second);

    if (fields.size() > 1)
        size += fields.size() - 1;  // the '&' separators

    std::string body;
    body.reserve(size);

    bool first = true;
    for (const auto& field : fields) {
        if (!first)
            body.push_back('&');
        first = false;

        if (!append_form_value(curl, body, field.first))
            return std::nullopt;

        body.push_back('=');
        if (!append_form_value(curl, body, field.second))
            return std::nullopt;
    }

    return body;
}

}  // namespace

void CurlDeleter::operator()(CurlHandle* handle) const noexcept {
    curl_easy_cleanup(raw(handle));
}

Client::Client(Config config) : curl_(curl_init()), config_(config) {}

Result Client::post(const std::string& url, std::string_view body) {
    CURL* const curl = raw(curl_.get());
    if (curl == nullptr)
        return Result{
            Error::Initialization, {}, "the libcurl handle is unavailable"};

    Result result;
    Sink sink{&result.response.body, config_.max_response_bytes};
    char error_buffer[CURL_ERROR_SIZE] = {};
    if (!configure(curl, url, body, &sink, error_buffer, config_))
        return Result{
            Error::Initialization, {}, "could not configure the request"};

    const CURLcode perform = curl_easy_perform(curl);
    if (perform != CURLE_OK) {
        std::string detail = detail_from_buffer(error_buffer);
        if (detail.empty())
            detail = curl_easy_strerror(perform);
        return Result{error_for(perform), {}, std::move(detail)};
    }

    auto http_code = 0L;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK)
        return Result{Error::Unknown, {}, "libcurl reported no HTTP status"};

    result.response.status = http_code;
    return result;
}

Result Client::post_form(const std::string& url,
                         std::initializer_list<FormField> fields) {
    CURL* const curl = raw(curl_.get());
    if (curl == nullptr)
        return Result{
            Error::Initialization, {}, "the libcurl handle is unavailable"};

    const auto body = form_body(curl, fields);
    if (!body)
        return Result{Error::Encoding, {}, "could not encode the request"};

    return post(url, *body);
}

}  // namespace http
