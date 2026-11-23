#pragma once

#include <cstddef>
#include <string>

namespace text {

[[nodiscard]] std::size_t utf16_length(const std::string& utf8) noexcept;

void trim_trailing_newlines(std::string& text) noexcept;

}  // namespace text
