#pragma once

#include <cstddef>
#include <string>

// Telegram's limit for the text of one message, in UTF-16 code units.
inline constexpr std::size_t kMaxMessageLength = 4096;

[[nodiscard]] std::size_t utf16_length(const std::string& utf8) noexcept;

void trim_trailing_newlines(std::string& text) noexcept;
