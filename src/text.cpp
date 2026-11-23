#include "text.hpp"

std::size_t utf16_length(const std::string& utf8) noexcept {
    std::size_t units = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        std::size_t width = 1;
        if (lead >= 0xF0U) {  // 4-byte sequence: one code point, two units
            width = 4;
        } else if (lead >= 0xE0U) {  // 3-byte sequence
            width = 3;
        } else if (lead >= 0xC0U) {  // 2-byte sequence
            width = 2;
        }
        i += width;
        units += (width == 4) ? 2 : 1;
    }

    return units;
}

void trim_trailing_newlines(std::string& text) noexcept {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
}
