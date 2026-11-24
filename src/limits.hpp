#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lim {

// Telegram's limit for the text of one message, in UTF-16 code units.
inline constexpr std::size_t kMaxMessageLength = 4096;

// Upper bound on the UTF-8 bytes that encode kMaxMessageLength.
inline constexpr std::size_t kMaxMessageBytes = 3 * kMaxMessageLength;

// Returns the length of `utf8` in UTF-16 code units
// or nullopt when `utf8` is not valid UTF-8.
[[nodiscard]] std::optional<std::size_t> utf16_length(
    std::string_view utf8) noexcept;

void trim_trailing_newlines(std::string& text) noexcept;

}  // namespace lim
